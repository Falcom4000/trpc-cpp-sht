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


#include <cppconn/driver.h>
#include <cppconn/exception.h>

#include "trpc/util/log/logging.h"

namespace trpc::mysql {

// -------- Handle impl --------
void MysqlConnectionPool::Handle::Reset() {
  if (pool_ && conn_) {
    pool_->ReturnConnection(std::move(conn_));
    pool_ = nullptr;
  }
}
MysqlConnectionPool::Handle::~Handle() { Reset(); }

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
  total_connections_.store(idle_connections_.size(), std::memory_order_release);
  
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
  total_connections_.store(0, std::memory_order_release);
  
  TRPC_FMT_INFO("MySQL connection pool stopped");
}

MysqlConnectionPool::Handle MysqlConnectionPool::Borrow(uint32_t timeout_ms) {
  MysqlConnectionPtr conn = GetConnection(timeout_ms);
  return Handle(this, std::move(conn));
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
        return conn;
      }
      // Connection is invalid, try next one
      // Adjust total as this connection will be destroyed when shared_ptr goes out of scope
      total_connections_.fetch_sub(1, std::memory_order_acq_rel);
      continue;
    }
    
    // 2. Create new connection if under limit
    size_t total = total_connections_.load(std::memory_order_acquire);
    if (total < config_.max_connections) {
      lock.unlock();
      auto conn = CreateConnection();
      lock.lock();
      
      if (conn) {
        total_connections_.fetch_add(1, std::memory_order_acq_rel);
        conn->UpdateLastUsedTime();
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
  
  // Reset connection state and return to idle pool
  conn->Reset();
  conn->UpdateLastUsedTime();
  idle_connections_.push_back(std::move(conn));
  
  cv_.notify_one();
}

MysqlConnectionPool::PoolStats MysqlConnectionPool::GetStats() const {
  std::lock_guard<std::mutex> lock(mutex_);
  
  PoolStats stats;
  stats.idle_count = idle_connections_.size();
  stats.total_count = total_connections_.load(std::memory_order_acquire);
  stats.active_count = stats.total_count >= stats.idle_count
                           ? stats.total_count - stats.idle_count
                           : 0;
  
  return stats;
}

MysqlConnectionPtr MysqlConnectionPool::CreateConnection() {
  try {
    sql::Driver* driver = get_driver_instance();
    if (!driver) {
      TRPC_FMT_ERROR("Failed to get MySQL driver instance");
      return nullptr;
    }
    
    // Create connection properties
    sql::ConnectOptionsMap connection_properties;
    connection_properties["hostName"] = config_.host;
    connection_properties["port"] = static_cast<int>(config_.port);
    connection_properties["userName"] = config_.user;
    connection_properties["password"] = config_.password;
    connection_properties["schema"] = config_.database;
    connection_properties["OPT_CONNECT_TIMEOUT"] = static_cast<int>(config_.connect_timeout_ms / 1000);
    connection_properties["OPT_READ_TIMEOUT"] = static_cast<int>(config_.read_timeout_ms / 1000);
    connection_properties["OPT_WRITE_TIMEOUT"] = static_cast<int>(config_.write_timeout_ms / 1000);
    connection_properties["OPT_RECONNECT"] = true;
    connection_properties["OPT_CHARSET_NAME"] = config_.charset;
    
    // Create connection using ConnectOptionsMap
    sql::Connection* conn = driver->connect(connection_properties);
    if (!conn) {
      TRPC_FMT_ERROR("Failed to create MySQL connection");
      return nullptr;
    }
    
    // Set default schema
    if (!config_.database.empty()) {
      conn->setSchema(config_.database);
    }
    
    TRPC_FMT_DEBUG("Created MySQL connection to {}:{}/{}", config_.host,
                   config_.port, config_.database);
    
    return std::make_shared<MysqlConnection>(conn);
  } catch (const sql::SQLException& e) {
    TRPC_FMT_ERROR("Failed to create MySQL connection: {} (Error code: {}, SQLState: {})",
                   e.what(), e.getErrorCode(), e.getSQLState());
    return nullptr;
  } catch (const std::exception& e) {
    TRPC_FMT_ERROR("Failed to create MySQL connection: {}", e.what());
    return nullptr;
  }
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
      total_connections_.fetch_sub(1, std::memory_order_acq_rel);
    } else {
      ++it;
    }
  }
}

void MysqlConnectionPool::EnsureMinIdleConnections() {
  std::lock_guard<std::mutex> lock(mutex_);
  
  while (idle_connections_.size() < config_.min_idle_connections) {
    size_t total = total_connections_.load(std::memory_order_acquire);
    if (total >= config_.max_connections) {
      break;
    }
    
    auto conn = CreateConnection();
    if (!conn) {
      TRPC_FMT_WARN("Failed to create connection for min idle pool");
      break;
    }
    
    total_connections_.fetch_add(1, std::memory_order_acq_rel);
    idle_connections_.push_back(conn);
    cv_.notify_one();
  }
}

}  // namespace trpc::mysql
