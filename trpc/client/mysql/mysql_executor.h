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

#include <memory>

#include "trpc/client/mysql/mysql_common.h"
#include "trpc/client/mysql/mysql_connection.h"
#include "trpc/common/future/future.h"
#include "trpc/common/status.h"
#include "trpc/util/thread/sq_thread_pool.h"
#include "trpc/util/thread/thread_pool.h"
#include "trpc/util/thread/thread_pool_option.h"

// Forward declaration
namespace sql {
class ResultSet;
}

namespace trpc::mysql {

/// @brief MySQL executor that runs MySQL C-API in separate thread pool
class MysqlExecutor {
 public:
  /// @brief Constructor with thread count
  explicit MysqlExecutor(uint32_t thread_num = 8);
  
  /// @brief Destructor
  ~MysqlExecutor();
  
  // Non-copyable and non-movable
  MysqlExecutor(const MysqlExecutor&) = delete;
  MysqlExecutor& operator=(const MysqlExecutor&) = delete;
  MysqlExecutor(MysqlExecutor&&) = delete;
  MysqlExecutor& operator=(MysqlExecutor&&) = delete;
  
  /// @brief Start the executor
  bool Start();
  
  /// @brief Stop the executor
  void Stop();
  
  /// @brief Submit task and wait for result (synchronous mode)
  /// @param conn MySQL connection
  /// @param request Request to execute
  /// @param result Result to fill
  /// @param timeout_ms Timeout in milliseconds
  /// @return Status of execution
  Status SubmitAndWait(MysqlConnectionPtr conn, const MysqlRequest& request,
                       MysqlResultSet* result, uint32_t timeout_ms);
  
  /// @brief Submit task asynchronously
  /// @param conn MySQL connection
  /// @param request Request to execute
  /// @return Future with result
  Future<MysqlResultSet> SubmitAsync(MysqlConnectionPtr conn, const MysqlRequest& request);
  
 private:
  /// @brief Execute prepared statement
  void ExecutePreparedSql(sql::Connection* conn, const std::string& sql,
                         const std::vector<std::string>& params,
                         MysqlResultSet* result);
  
  /// @brief Parse result set from sql::ResultSet
  void ParseResultSet(sql::ResultSet* res, MysqlResultSet* result);
  
 private:
  std::unique_ptr<ThreadPoolImpl<SQThreadPool>> thread_pool_;
  uint32_t thread_num_;
};

using MysqlExecutorPtr = std::unique_ptr<MysqlExecutor>;

}  // namespace trpc::mysql
