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

#include "mysql/mysql.h"

#include "trpc/coroutine/fiber_latch.h"
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
      ExecuteTask(conn->GetRawConnection(), request, result);
      promise.SetValue(Status());
    } catch (const std::exception& e) {
      promise.SetValue(Status(-1, e.what()));
    }
  };
  
  if (!thread_pool_->AddTask(std::move(task))) {
    return Status(-1, "Failed to submit task to thread pool");
  }
  
  // Wait for result with timeout
  auto status = trpc::future::BlockingTryGet(std::move(future), timeout_ms);
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
      ExecuteTask(conn->GetRawConnection(), request, &result);
      promise.SetValue(std::move(result));
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

void MysqlExecutor::ExecuteTask(MYSQL* mysql, const MysqlRequest& request,
                                MysqlResultSet* result) {
  if (request.is_prepared) {
    ExecutePreparedSql(mysql, request.sql, request.params, result);
  } else {
    ExecuteSql(mysql, request.sql, result);
  }
}

void MysqlExecutor::ExecuteSql(MYSQL* mysql, const std::string& sql,
                               MysqlResultSet* result) {
  result->Clear();
  
  // Execute query
  if (mysql_real_query(mysql, sql.c_str(), sql.length()) != 0) {
    std::string error_msg = mysql_error(mysql);
    int error_code = mysql_errno(mysql);
    TRPC_FMT_ERROR("mysql_real_query failed: [{}] {}", error_code, error_msg);
    throw std::runtime_error("MySQL query error: " + error_msg);
  }
  
  // Get result
  MYSQL_RES* res = mysql_store_result(mysql);
  if (res) {
    // SELECT query with results
    ParseResultSet(res, result);
    mysql_free_result(res);
  } else {
    // Non-SELECT query or error
    if (mysql_field_count(mysql) == 0) {
      // Non-SELECT query (INSERT/UPDATE/DELETE)
      result->affected_rows = mysql_affected_rows(mysql);
      result->insert_id = mysql_insert_id(mysql);
      
      TRPC_FMT_DEBUG("Query affected {} rows, insert_id: {}",
                     result->affected_rows, result->insert_id);
    } else {
      // Error occurred
      std::string error_msg = mysql_error(mysql);
      int error_code = mysql_errno(mysql);
      TRPC_FMT_ERROR("mysql_store_result failed: [{}] {}", error_code, error_msg);
      throw std::runtime_error("MySQL result error: " + error_msg);
    }
  }
}

void MysqlExecutor::ExecutePreparedSql(MYSQL* mysql, const std::string& sql,
                                       const std::vector<std::string>& params,
                                       MysqlResultSet* result) {
  result->Clear();
  
  // Initialize statement
  MYSQL_STMT* stmt = mysql_stmt_init(mysql);
  if (!stmt) {
    throw std::runtime_error("mysql_stmt_init failed");
  }
  
  // Prepare statement
  if (mysql_stmt_prepare(stmt, sql.c_str(), sql.length()) != 0) {
    std::string error_msg = mysql_stmt_error(stmt);
    mysql_stmt_close(stmt);
    throw std::runtime_error("mysql_stmt_prepare failed: " + error_msg);
  }
  
  // Check parameter count
  unsigned long param_count = mysql_stmt_param_count(stmt);
  if (param_count != params.size()) {
    mysql_stmt_close(stmt);
    throw std::runtime_error("Parameter count mismatch");
  }
  
  // Bind parameters
  if (param_count > 0) {
    std::vector<MYSQL_BIND> bind_params(param_count);
    std::vector<unsigned long> lengths(param_count);
    
    for (size_t i = 0; i < param_count; ++i) {
      bind_params[i] = {};
      bind_params[i].buffer_type = MYSQL_TYPE_STRING;
      bind_params[i].buffer = const_cast<char*>(params[i].c_str());
      bind_params[i].buffer_length = params[i].length();
      bind_params[i].length = &lengths[i];
      lengths[i] = params[i].length();
    }
    
    if (mysql_stmt_bind_param(stmt, bind_params.data()) != 0) {
      std::string error_msg = mysql_stmt_error(stmt);
      mysql_stmt_close(stmt);
      throw std::runtime_error("mysql_stmt_bind_param failed: " + error_msg);
    }
  }
  
  // Execute statement
  if (mysql_stmt_execute(stmt) != 0) {
    std::string error_msg = mysql_stmt_error(stmt);
    mysql_stmt_close(stmt);
    throw std::runtime_error("mysql_stmt_execute failed: " + error_msg);
  }
  
  // Get metadata for SELECT queries
  MYSQL_RES* metadata = mysql_stmt_result_metadata(stmt);
  if (metadata) {
    // SELECT query - bind results
    unsigned int field_count = mysql_num_fields(metadata);
    result->field_count = field_count;
    
    // Get field names
    MYSQL_FIELD* fields = mysql_fetch_fields(metadata);
    for (unsigned int i = 0; i < field_count; ++i) {
      result->field_names.push_back(fields[i].name);
    }
    
    // Bind result buffers
    std::vector<MYSQL_BIND> bind_results(field_count);
    std::vector<std::string> row_data(field_count);
    std::vector<unsigned long> lengths(field_count);
    std::vector<bool> is_nulls_vec(field_count);
    std::vector<bool> errors_vec(field_count);
    // Convert vector<bool> to regular bool array for MYSQL_BIND
    std::unique_ptr<bool[]> is_nulls(new bool[field_count]);
    std::unique_ptr<bool[]> errors(new bool[field_count]);
    
    const size_t buffer_size = 1024;
    std::vector<std::vector<char>> buffers(field_count, std::vector<char>(buffer_size));
    
    for (unsigned int i = 0; i < field_count; ++i) {
      bind_results[i] = {};
      bind_results[i].buffer_type = MYSQL_TYPE_STRING;
      bind_results[i].buffer = buffers[i].data();
      bind_results[i].buffer_length = buffer_size;
      bind_results[i].length = &lengths[i];
      bind_results[i].is_null = &is_nulls[i];
      bind_results[i].error = &errors[i];
    }
    
    if (mysql_stmt_bind_result(stmt, bind_results.data()) != 0) {
      std::string error_msg = mysql_stmt_error(stmt);
      mysql_free_result(metadata);
      mysql_stmt_close(stmt);
      throw std::runtime_error("mysql_stmt_bind_result failed: " + error_msg);
    }
    
    // Fetch rows
    while (mysql_stmt_fetch(stmt) == 0) {
      std::vector<std::string> row;
      for (unsigned int i = 0; i < field_count; ++i) {
        if (is_nulls[i]) {
          row.push_back("");
        } else {
          row.push_back(std::string(buffers[i].data(), lengths[i]));
        }
      }
      result->rows.push_back(std::move(row));
    }
    
    mysql_free_result(metadata);
  } else {
    // Non-SELECT query
    result->affected_rows = mysql_stmt_affected_rows(stmt);
    result->insert_id = mysql_stmt_insert_id(stmt);
  }
  
  mysql_stmt_close(stmt);
}

void MysqlExecutor::ParseResultSet(MYSQL_RES* res, MysqlResultSet* result) {
  // Get field information
  unsigned int field_count = mysql_num_fields(res);
  result->field_count = field_count;
  
  MYSQL_FIELD* fields = mysql_fetch_fields(res);
  for (unsigned int i = 0; i < field_count; ++i) {
    result->field_names.push_back(fields[i].name);
  }
  
  // Fetch all rows
  MYSQL_ROW row;
  while ((row = mysql_fetch_row(res)) != nullptr) {
    unsigned long* lengths = mysql_fetch_lengths(res);
    
    std::vector<std::string> row_data;
    for (unsigned int i = 0; i < field_count; ++i) {
      if (row[i]) {
        row_data.push_back(std::string(row[i], lengths[i]));
      } else {
        row_data.push_back("");
      }
    }
    result->rows.push_back(std::move(row_data));
  }
  
  TRPC_FMT_DEBUG("Parsed {} rows with {} fields", result->rows.size(), field_count);
}

}  // namespace trpc::mysql
