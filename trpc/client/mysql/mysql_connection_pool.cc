//
//
// Tencent is pleased to support the open source community by making tRPC available.
//
// Copyright (C) 2023 Tencent.
// All rights reserved.
//
// If you have downloaded a copy of the tRPC source code from Tencent,
// please note that tRPC source code is licensed under the  Apache 2.0 License,
// A copy of the Apache 2.0 License is included in this file.
//
//

#include "trpc/client/mysql/mysql_connection_pool.h"

#include <algorithm>

#include "mysql/mysql.h"

#include "trpc/util/log/logging.h"

namespace trpc::mysql {

MysqlConnectionPool::MysqlConnectionPool(const MysqlConnectionConfig& config)
    : config_(config) {
}

MysqlConnectionPool::~MysqlConnectionPool() {
  Stop();
}

bool MysqlConnectionPool::Start() {
  if (config_.max_connections == 0) {
    TRPC_FMT_ERROR("Invalid max_connections: 0");
    return false;
  }
  
  // Create initial connections
  for (uint32_t i = 0; i < config_.min_idle_connections; ++i) {
    auto conn = CreateConnection();
    if (!conn) {
      TRPC_FMT_ERROR("Failed to create initial connection {}", i);
      return false;
    }
    idle_connections_.push_back(std::move(conn));
  }
  
  TRPC_FMT_INFO("MySQL connection pool started with {} initial connections",
                idle_connections_.size());
  
  // Start health check thread
  stopped_.store(false);
  health_check_thread_ = std::thread([this]() { HealthCheckLoop(); });
  
  return true;
}

void MysqlConnectionPool::Stop() {
  if (stopped_.load()) {
    return;
  }
  
  stopped_.store(true);
  cv_.notify_all();
  
  if (health_check_thread_.joinable()) {
    health_check_thread_.join();
  }
  
  std::lock_guard<std::mutex> lock(mutex_);
  idle_connections_.clear();
  active_connections_.clear();
  
  TRPC_FMT_INFO("MySQL connection pool stopped");
}

MysqlConnectionPtr MysqlConnectionPool::GetConnection(uint32_t timeout_ms) {
  std::unique_lock<std::mutex> lock(mutex_);
  
  auto deadline = std::chrono::steady_clock::now() +
                  std::chrono::milliseconds(timeout_ms);
  
  while (!stopped_.load()) {
    // 1. Try to get from idle connections
    if (!idle_connections_.empty()) {
      auto conn = idle_connections_.front();
      idle_connections_.pop_front();
      
      // Check if connection is still valid
      if (conn->IsValid()) {
        conn->UpdateLastUsedTime();
        active_connections_.insert(conn);
        return conn;
      }
      // Connection is invalid, try next one
      continue;
    }
    
    // 2. Create new connection if under limit
    size_t total = idle_connections_.size() + active_connections_.size();
    if (total < config_.max_connections) {
      lock.unlock();
      auto conn = CreateConnection();
      lock.lock();
      
      if (conn) {
        conn->UpdateLastUsedTime();
        active_connections_.insert(conn);
        return conn;
      }
    }
    
    // 3. Wait for available connection
    if (cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
      TRPC_FMT_WARN("Get connection timeout after {}ms", timeout_ms);
      return nullptr;
    }
  }
  
  return nullptr;
}

void MysqlConnectionPool::ReturnConnection(MysqlConnectionPtr conn) {
  if (!conn) {
    return;
  }
  
  std::lock_guard<std::mutex> lock(mutex_);
  
  auto it = active_connections_.find(conn);
  if (it != active_connections_.end()) {
    active_connections_.erase(it);
    
    // Reset connection state and return to idle pool
    conn->Reset();
    conn->UpdateLastUsedTime();
    idle_connections_.push_back(conn);
    
    cv_.notify_one();
  }
}

MysqlConnectionPool::PoolStats MysqlConnectionPool::GetStats() const {
  std::lock_guard<std::mutex> lock(mutex_);
  
  PoolStats stats;
  stats.idle_count = idle_connections_.size();
  stats.active_count = active_connections_.size();
  stats.total_count = stats.idle_count + stats.active_count;
  
  return stats;
}

MysqlConnectionPtr MysqlConnectionPool::CreateConnection() {
  MYSQL* mysql = mysql_init(nullptr);
  if (!mysql) {
    TRPC_FMT_ERROR("mysql_init failed");
    return nullptr;
  }
  
  // Set connection options
  unsigned int connect_timeout = config_.connect_timeout_ms / 1000;
  unsigned int read_timeout = config_.read_timeout_ms / 1000;
  unsigned int write_timeout = config_.write_timeout_ms / 1000;
  
  mysql_options(mysql, MYSQL_OPT_CONNECT_TIMEOUT, &connect_timeout);
  mysql_options(mysql, MYSQL_OPT_READ_TIMEOUT, &read_timeout);
  mysql_options(mysql, MYSQL_OPT_WRITE_TIMEOUT, &write_timeout);
  
  // Set character set
  mysql_options(mysql, MYSQL_SET_CHARSET_NAME, config_.charset.c_str());
  
  // Enable auto-reconnect
  bool reconnect = true;
  mysql_options(mysql, MYSQL_OPT_RECONNECT, &reconnect);
  
  // Connect to MySQL server
  if (!mysql_real_connect(mysql, config_.host.c_str(), config_.user.c_str(),
                          config_.password.c_str(), config_.database.c_str(),
                          config_.port, nullptr, 0)) {
    TRPC_FMT_ERROR("mysql_real_connect failed: {}", mysql_error(mysql));
    mysql_close(mysql);
    return nullptr;
  }
  
  TRPC_FMT_DEBUG("Created MySQL connection to {}:{}/{}", config_.host,
                 config_.port, config_.database);
  
  return std::make_shared<MysqlConnection>(mysql);
}

void MysqlConnectionPool::HealthCheckLoop() {
  const uint32_t check_interval_ms = 10000;  // Check every 10 seconds
  
  while (!stopped_.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(check_interval_ms));
    
    if (stopped_.load()) {
      break;
    }
    
    CloseIdleConnections();
    EnsureMinIdleConnections();
  }
}

void MysqlConnectionPool::CloseIdleConnections() {
  std::lock_guard<std::mutex> lock(mutex_);
  
  auto now = std::chrono::steady_clock::now();
  
  auto it = idle_connections_.begin();
  while (it != idle_connections_.end()) {
    auto conn = *it;
    
    // Check idle time
    auto idle_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - conn->GetLastUsedTime());
    
    bool should_remove = false;
    
    if (idle_duration.count() > static_cast<int64_t>(config_.max_idle_time_ms)) {
      TRPC_FMT_DEBUG("Closing idle connection (idle time: {}ms)",
                     idle_duration.count());
      should_remove = true;
    } else if (!conn->IsValid()) {
      TRPC_FMT_WARN("Closing invalid connection");
      should_remove = true;
    }
    
    if (should_remove) {
      it = idle_connections_.erase(it);
    } else {
      ++it;
    }
  }
}

void MysqlConnectionPool::EnsureMinIdleConnections() {
  std::lock_guard<std::mutex> lock(mutex_);
  
  while (idle_connections_.size() < config_.min_idle_connections) {
    size_t total = idle_connections_.size() + active_connections_.size();
    if (total >= config_.max_connections) {
      break;
    }
    
    auto conn = CreateConnection();
    if (!conn) {
      TRPC_FMT_WARN("Failed to create connection for min idle pool");
      break;
    }
    
    idle_connections_.push_back(conn);
  }
}

}  // namespace trpc::mysql
