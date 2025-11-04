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

#include "trpc/client/mysql/mysql_connection.h"

#include <cppconn/statement.h>
#include <cppconn/resultset.h>
#include <cppconn/exception.h>

#include "trpc/util/log/logging.h"

namespace trpc::mysql {

MysqlConnection::MysqlConnection(sql::Connection* conn) 
    : conn_(conn),
      last_used_time_(std::chrono::steady_clock::now()) {
}

MysqlConnection::~MysqlConnection() {
  if (conn_) {
    conn_->close();
    delete conn_;
    conn_ = nullptr;
  }
}

bool MysqlConnection::IsValid() const {
  if (!conn_) {
    return false;
  }
  
  try {
    // Use isValid() to check if connection is alive
    if (!conn_->isValid()) {
      TRPC_FMT_WARN("MySQL connection is invalid");
      return false;
    }
    
    // Try to create a simple statement to verify connection
    std::unique_ptr<sql::Statement> stmt(conn_->createStatement());
    std::unique_ptr<sql::ResultSet> res(stmt->executeQuery("SELECT 1"));
    return true;
  } catch (const sql::SQLException& e) {
    TRPC_FMT_WARN("MySQL connection validation failed: {}", e.what());
    return false;
  }
}

void MysqlConnection::Reset() {
  if (conn_) {
    UpdateLastUsedTime();
  }
}

uint64_t MysqlConnection::GetConnectionId() const {
  if (conn_) {
    try {
      std::unique_ptr<sql::Statement> stmt(conn_->createStatement());
      std::unique_ptr<sql::ResultSet> res(stmt->executeQuery("SELECT CONNECTION_ID()"));
      if (res->next()) {
        return res->getUInt64(1);
      }
    } catch (const sql::SQLException& e) {
      TRPC_FMT_WARN("Failed to get connection ID: {}", e.what());
    }
  }
  return 0;
}

}  // namespace trpc::mysql
