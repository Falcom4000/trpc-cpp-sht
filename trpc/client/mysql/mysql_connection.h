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

#include <chrono>
#include <memory>
#include <string>

#include <cppconn/connection.h>
#include <cppconn/driver.h>

#include "trpc/client/mysql/mysql_common.h"

namespace trpc::mysql {

/// @brief Wrapper for MySQL connection (sql::Connection* handle)
class MysqlConnection {
 public:
  /// @brief Constructor with MySQL Connector/C++ connection
  explicit MysqlConnection(sql::Connection* conn);
  
  /// @brief Destructor, closes the connection
  ~MysqlConnection();
  
  // Non-copyable
  MysqlConnection(const MysqlConnection&) = delete;
  MysqlConnection& operator=(const MysqlConnection&) = delete;
  
  /// @brief Get the raw MySQL connection
  sql::Connection* GetRawConnection() { return conn_; }
  
  /// @brief Check if connection is valid (ping test)
  bool IsValid() const;
  
  /// @brief Reset connection state
  void Reset();
  
  /// @brief Get last used time
  std::chrono::steady_clock::time_point GetLastUsedTime() const {
    return last_used_time_;
  }
  
  /// @brief Update last used time to now
  void UpdateLastUsedTime() {
    last_used_time_ = std::chrono::steady_clock::now();
  }
  
  /// @brief Get connection ID
  uint64_t GetConnectionId() const;
  
 private:
  sql::Connection* conn_{nullptr};
  std::chrono::steady_clock::time_point last_used_time_;
};

using MysqlConnectionPtr = std::shared_ptr<MysqlConnection>;

}  // namespace trpc::mysql
