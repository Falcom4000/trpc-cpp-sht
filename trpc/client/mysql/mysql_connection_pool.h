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

#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>

#include "trpc/client/mysql/mysql_common.h"
#include "trpc/client/mysql/mysql_connection.h"

namespace trpc::mysql {

/// @brief MySQL connection pool for managing database connections
class MysqlConnectionPool {
 public:
  /// @brief Constructor with configuration
  explicit MysqlConnectionPool(const MysqlConnectionConfig& config);
  
  /// @brief Destructor
  ~MysqlConnectionPool();
  
  // Non-copyable and non-movable
  MysqlConnectionPool(const MysqlConnectionPool&) = delete;
  MysqlConnectionPool& operator=(const MysqlConnectionPool&) = delete;
  MysqlConnectionPool(MysqlConnectionPool&&) = delete;
  MysqlConnectionPool& operator=(MysqlConnectionPool&&) = delete;
  
  /// @brief Start the connection pool (creates initial connections)
  /// @return true on success, false on failure
  bool Start();
  
  /// @brief Stop the connection pool
  void Stop();
  
  /// @brief Get a connection from the pool
  /// @param timeout_ms Timeout in milliseconds
  /// @return Connection pointer, nullptr on timeout or error
  MysqlConnectionPtr GetConnection(uint32_t timeout_ms = 3000);
  
  /// @brief Return a connection to the pool
  /// @param conn Connection to return
  void ReturnConnection(MysqlConnectionPtr conn);
  
  /// @brief Get pool statistics
  struct PoolStats {
    size_t idle_count;
    size_t active_count;
    size_t total_count;
  };
  
  PoolStats GetStats() const;
  
 private:
  /// @brief Create a new MySQL connection
  MysqlConnectionPtr CreateConnection();
  
  /// @brief Health check loop (runs in separate thread)
  void HealthCheckLoop();
  
  /// @brief Close idle connections that exceed max idle time
  void CloseIdleConnections();
  
  /// @brief Ensure minimum idle connections
  void EnsureMinIdleConnections();
  
 private:
  MysqlConnectionConfig config_;
  
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  
  std::deque<MysqlConnectionPtr> idle_connections_;
  std::unordered_set<MysqlConnectionPtr> active_connections_;
  
  std::atomic<bool> stopped_{false};
  std::thread health_check_thread_;
};

using MysqlConnectionPoolPtr = std::unique_ptr<MysqlConnectionPool>;

}  // namespace trpc::mysql
