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

#include "trpc/client/mysql/mysql_executor.h"

#include <cppconn/connection.h>
#include <cppconn/exception.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <cppconn/statement.h>

#include "trpc/coroutine/fiber.h"
#include "trpc/coroutine/fiber_latch.h"
#include "trpc/coroutine/future.h"
#include "trpc/future/future_utility.h"
#include "trpc/util/log/logging.h"

namespace trpc::mysql {

MysqlExecutor::MysqlExecutor(uint32_t thread_num) : thread_num_(thread_num) {
}

MysqlExecutor::~MysqlExecutor() {
  Stop();
}

bool MysqlExecutor::Start() {
  ThreadPoolOption option;
  option.thread_num = thread_num_;
  option.task_queue_size = 10000;
  
  thread_pool_ = std::make_unique<ThreadPoolImpl<SQThreadPool>>(std::move(option));
  
  if (!thread_pool_->Start()) {
    TRPC_FMT_ERROR("Failed to start MySQL executor thread pool");
    return false;
  }
  
  TRPC_FMT_INFO("MySQL executor started with {} threads", thread_num_);
  return true;
}

void MysqlExecutor::Stop() {
  if (thread_pool_) {
    thread_pool_->Stop();
    thread_pool_->Join();
    thread_pool_.reset();
  }
}

Status MysqlExecutor::SubmitAndWait(MysqlConnectionPtr conn, const MysqlRequest& request,
                                    MysqlResultSet* result, uint32_t timeout_ms) {
  if (!conn || !result) {
    return Status(-1, "Invalid connection or result pointer");
  }
  
  Promise<Status> promise;
  auto future = promise.GetFuture();
  
  auto task = [this, conn, request, result, promise = std::move(promise)]() mutable {
    try {
      ExecutePreparedSql(conn->GetRawConnection(), request.sql, request.params, result);
      promise.SetValue(Status());
    } catch (const sql::SQLException& e) {
      promise.SetValue(Status(-1, std::string("SQL error: ") + e.what()));
    } catch (const std::exception& e) {
      promise.SetValue(Status(-1, e.what()));
    }
  };
  
  if (!thread_pool_->AddTask(std::move(task))) {
    return Status(-1, "Failed to submit task to thread pool");
  }
  
  // Wait for result with timeout
  // Use fiber::BlockingTryGet in fiber environment to avoid blocking pthread
  std::optional<Future<Status>> status;
  if (trpc::IsRunningInFiberWorker()) {
    status = trpc::fiber::BlockingTryGet(std::move(future), timeout_ms);
  } else {
    status = trpc::future::BlockingTryGet(std::move(future), timeout_ms);
  }
  
  if (!status) {
    return Status(-1, "Execute timeout");
  }
  
  return status->GetValue0();
}

Future<MysqlResultSet> MysqlExecutor::SubmitAsync(MysqlConnectionPtr conn,
                                                   const MysqlRequest& request) {
  if (!conn) {
    return MakeExceptionFuture<MysqlResultSet>(
        CommonException("Invalid connection pointer"));
  }
  
  Promise<MysqlResultSet> promise;
  auto future = promise.GetFuture();
  
  auto task = [this, conn, request, promise = std::move(promise)]() mutable {
    try {
      MysqlResultSet result;
      ExecutePreparedSql(conn->GetRawConnection(), request.sql, request.params, &result);
      promise.SetValue(std::move(result));
    } catch (const sql::SQLException& e) {
      std::string error_msg = std::string("SQL error: ") + e.what();
      promise.SetException(CommonException(error_msg.c_str()));
    } catch (const std::exception& e) {
      promise.SetException(CommonException(e.what()));
    }
  };
  
  if (!thread_pool_->AddTask(std::move(task))) {
    return MakeExceptionFuture<MysqlResultSet>(
        CommonException("Failed to submit task to thread pool"));
  }
  
  return future;
}

void MysqlExecutor::ExecutePreparedSql(sql::Connection* conn, const std::string& sql,
                                       const std::vector<std::string>& params,
                                       MysqlResultSet* result) {
  result->Clear();
  
  if (!conn) {
    throw std::runtime_error("Connection is null");
  }
  
  // Check if SQL has parameters (contains '?')
  bool has_params = std::find(sql.begin(), sql.end(), '?') != sql.end();
  
  if (has_params && !params.empty()) {
    // Use prepared statement
    std::unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
    
    // Bind parameters (1-indexed)
    for (size_t i = 0; i < params.size(); ++i) {
      pstmt->setString(static_cast<int>(i + 1), params[i]);
    }
    
    // Execute query
    bool has_result = pstmt->execute();
    
    if (has_result) {
      // SELECT query - get result set
      std::unique_ptr<sql::ResultSet> res(pstmt->getResultSet());
      ParseResultSet(res.get(), result);
    } else {
      // Non-SELECT query - get update count
      result->affected_rows = pstmt->getUpdateCount();
      // Get last insert ID using a query
      std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery("SELECT LAST_INSERT_ID()"));
      if (res->next()) {
        result->insert_id = res->getUInt64(1);
      }
    }
  } else {
    // No parameters - use regular statement
    std::unique_ptr<sql::Statement> stmt(conn->createStatement());
    
    bool has_result = stmt->execute(sql);
    
    if (has_result) {
      // SELECT query - get result set
      std::unique_ptr<sql::ResultSet> res(stmt->getResultSet());
      ParseResultSet(res.get(), result);
    } else {
      // Non-SELECT query - get update count
      result->affected_rows = stmt->getUpdateCount();
      // For regular statements, we need to get last insert id separately
      std::unique_ptr<sql::ResultSet> res(stmt->executeQuery("SELECT LAST_INSERT_ID()"));
      if (res->next()) {
        result->insert_id = res->getUInt64(1);
      }
    }
  }
}

void MysqlExecutor::ParseResultSet(sql::ResultSet* res, MysqlResultSet* result) {
  if (!res) {
    return;
  }
  
  // Get metadata
  sql::ResultSetMetaData* meta = res->getMetaData();
  unsigned int field_count = meta->getColumnCount();
  result->field_count = field_count;
  
  // Get field names
  for (unsigned int i = 1; i <= field_count; ++i) {
    result->field_names.push_back(meta->getColumnName(i));
  }
  
  // Fetch all rows
  while (res->next()) {
    std::vector<std::string> row_data;
    for (unsigned int i = 1; i <= field_count; ++i) {
      if (res->isNull(i)) {
        row_data.push_back("");
      } else {
        // Get value as string
        row_data.push_back(res->getString(i));
      }
    }
    result->rows.push_back(std::move(row_data));
  }
  
  TRPC_FMT_DEBUG("Parsed {} rows with {} fields", result->rows.size(), field_count);
}

}  // namespace trpc::mysql
