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

#include "trpc/util/log/logging.h"

namespace trpc::mysql {

MysqlConnection::MysqlConnection(MYSQL* mysql) 
    : mysql_(mysql),
      last_used_time_(std::chrono::steady_clock::now()) {
}

MysqlConnection::~MysqlConnection() {
  if (mysql_) {
    mysql_close(mysql_);
    mysql_ = nullptr;
  }
}

bool MysqlConnection::IsValid() const {
  if (!mysql_) {
    return false;
  }
  
  // Use mysql_ping to check if connection is alive
  if (mysql_ping(mysql_) != 0) {
    TRPC_FMT_WARN("MySQL connection ping failed: {}", mysql_error(mysql_));
    return false;
  }
  
  return true;
}

void MysqlConnection::Reset() {
  if (mysql_) {
    // Reset connection state
    mysql_reset_connection(mysql_);
    UpdateLastUsedTime();
  }
}

uint64_t MysqlConnection::GetConnectionId() const {
  if (mysql_) {
    return mysql_thread_id(mysql_);
  }
  return 0;
}

}  // namespace trpc::mysql
