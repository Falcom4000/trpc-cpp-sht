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

#include <cstdint>
#include <string>
#include <vector>

namespace trpc::mysql {

/// @brief MySQL result set containing query results
struct MysqlResultSet {
  /// Field/column names
  std::vector<std::string> field_names;
  
  /// Data rows, each row is a vector of field values (as strings)
  std::vector<std::vector<std::string>> rows;
  
  /// Number of rows affected by INSERT/UPDATE/DELETE
  uint64_t affected_rows = 0;
  
  /// Last inserted auto-increment ID
  uint64_t insert_id = 0;
  
  /// Number of fields in result set
  uint32_t field_count = 0;
  
  /// Clear all data
  void Clear() {
    field_names.clear();
    rows.clear();
    affected_rows = 0;
    insert_id = 0;
    field_count = 0;
  }
};

/// @brief MySQL request for internal use
struct MysqlRequest {
  /// SQL statement to execute
  std::string sql;
  
  /// Parameters for prepared statements
  std::vector<std::string> params;
  
  /// Whether this is a prepared statement
  bool is_prepared = false;
};

/// @brief MySQL connection configuration
struct MysqlConnectionConfig {
  /// MySQL server host
  std::string host = "127.0.0.1";
  
  /// MySQL server port
  uint16_t port = 3306;
  
  /// Username
  std::string user;
  
  /// Password
  std::string password;
  
  /// Database name
  std::string database;
  
  /// Character set
  std::string charset = "utf8mb4";
  
  /// Connection timeout in milliseconds
  uint32_t connect_timeout_ms = 3000;
  
  /// Read timeout in milliseconds
  uint32_t read_timeout_ms = 30000;
  
  /// Write timeout in milliseconds
  uint32_t write_timeout_ms = 30000;
  
  /// Maximum number of connections in pool
  uint32_t max_connections = 100;
  
  /// Minimum number of idle connections
  uint32_t min_idle_connections = 5;
  
  /// Maximum idle time before connection is closed (milliseconds)
  uint32_t max_idle_time_ms = 60000;
  
  /// Number of executor threads
  uint32_t executor_thread_num = 8;
};

}  // namespace trpc::mysql
