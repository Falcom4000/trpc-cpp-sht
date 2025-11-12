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

#include "trpc/client/mysql/mysql_service_proxy.h"

#include "trpc/util/log/logging.h"

namespace trpc::mysql {

MysqlServiceProxy::~MysqlServiceProxy() {
  Destroy();
}

void MysqlServiceProxy::Stop() {
  if (executor_) {
    executor_->Stop();
  }
  
  if (conn_pool_) {
    conn_pool_->Stop();
  }
  
  ServiceProxy::Stop();
}

void MysqlServiceProxy::Destroy() {
  Stop();
  
  executor_.reset();
  conn_pool_.reset();
  
  ServiceProxy::Destroy();
}

void MysqlServiceProxy::InitTransport() {
  // Don't use standard transport, use connection pool instead
  auto config = ParseConfig();
  
  // Create connection pool
  conn_pool_ = std::make_unique<MysqlConnectionPool>(config);
  if (!conn_pool_->Start()) {
    TRPC_FMT_ERROR("Failed to start MySQL connection pool");
    throw std::runtime_error("Failed to start MySQL connection pool");
  }
  
  // Create executor
  executor_ = std::make_unique<MysqlExecutor>(config.executor_thread_num);
  if (!executor_->Start()) {
    TRPC_FMT_ERROR("Failed to start MySQL executor");
    throw std::runtime_error("Failed to start MySQL executor");
  }
  
  TRPC_FMT_INFO("MySQL service proxy initialized successfully");
}

Status MysqlServiceProxy::ExecuteInternal(const ClientContextPtr& context,
                                         const MysqlRequest& request,
                                         MysqlResultSet* result) {
  if (!conn_pool_ || !executor_) {
    return Status(-1, "Connection pool or executor not initialized");
  }
  
  // Set request data for filters
  context->SetRequestData(const_cast<MysqlRequest*>(&request));
  context->SetResponseData(result);
  
  // Fill client context
  FillClientContext(context);
  
  // Run pre-invoke filters
  int filter_ret = RunFilters(FilterPoint::CLIENT_PRE_RPC_INVOKE, context);
  if (filter_ret != 0) {
    return Status(-1, "Filter rejected request");
  }
  
  // Borrow connection from pool
  uint32_t conn_timeout = context->GetTimeout() > 0 ? context->GetTimeout() : 3000;
  auto handle = conn_pool_->Borrow(conn_timeout);
  if (!handle) {
    context->SetStatus(Status(-1, "Failed to get connection from pool"));
    RunFilters(FilterPoint::CLIENT_POST_RPC_INVOKE, context);
    return context->GetStatus();
  }
  
  auto conn = handle.Get();
  
  // Execute SQL
  uint32_t exec_timeout = context->GetTimeout() > 0 ? context->GetTimeout() : 30000;
  Status status = executor_->SubmitAndWait(conn, request, result, exec_timeout);
  
  
  // Set status and run post-invoke filters
  context->SetStatus(status);
  RunFilters(FilterPoint::CLIENT_POST_RPC_INVOKE, context);
  
  return status;
}

Future<MysqlResultSet> MysqlServiceProxy::AsyncExecuteInternal(
    const ClientContextPtr& context, const MysqlRequest& request) {
  if (!conn_pool_ || !executor_) {
    return MakeExceptionFuture<MysqlResultSet>(
        CommonException("Connection pool or executor not initialized"));
  }
  
  // Set request data for filters
  context->SetRequestData(const_cast<MysqlRequest*>(&request));
  
  // Fill client context
  FillClientContext(context);
  
  // Run pre-invoke filters
  int filter_ret = RunFilters(FilterPoint::CLIENT_PRE_RPC_INVOKE, context);
  if (filter_ret != 0) {
    return MakeExceptionFuture<MysqlResultSet>(
        CommonException("Filter rejected request"));
  }
  
  // Borrow connection from pool
  uint32_t conn_timeout = context->GetTimeout() > 0 ? context->GetTimeout() : 3000;
  auto handle = conn_pool_->Borrow(conn_timeout);
  if (!handle) {
    return MakeExceptionFuture<MysqlResultSet>(
        CommonException("Failed to get connection from pool"));
  }
  
  auto conn = handle.Get();
  
  // Submit async task
  // Note: We need to keep the handle alive until the future completes
  auto future = executor_->SubmitAsync(conn, request);
  
  // Return connection when done
  return future.Then([this, handle = std::move(handle), context](Future<MysqlResultSet>&& f) mutable {
    // Connection will be automatically returned when handle goes out of scope
    
    if (f.IsReady()) {
      context->SetStatus(Status());
    } else {
      context->SetStatus(Status(-1, "Async execution failed"));
    }
    
    // Run post-invoke filters
    RunFilters(FilterPoint::CLIENT_POST_RPC_INVOKE, context);
    
    return std::move(f);
  });
}

MysqlConnectionConfig MysqlServiceProxy::ParseConfig() {
  MysqlConnectionConfig config;
  
  auto option = GetServiceProxyOption();
  if (!option) {
    TRPC_FMT_ERROR("Service proxy option is null");
    return config;
  }
  
  // Parse host and port from target
  std::string target = option->target;
  size_t colon_pos = target.find(':');
  if (colon_pos != std::string::npos) {
    config.host = target.substr(0, colon_pos);
    config.port = std::stoi(target.substr(colon_pos + 1));
  } else {
    config.host = target;
  }
  
  // Get MySQL-specific configuration from option->user_extend_config
  // For now, use defaults and expect configuration through yaml
  // TODO: Parse from option->user_extend_config or yaml
  
  config.max_connections = 100;
  config.min_idle_connections = 10;
  config.executor_thread_num = 8;
  config.connect_timeout_ms = 3000;
  config.read_timeout_ms = 30000;
  config.write_timeout_ms = 30000;
  config.max_idle_time_ms = 60000;
  
  TRPC_FMT_INFO("MySQL config: host={}, port={}", config.host, config.port);
  
  return config;
}

// Type-safe API implementation

Status MysqlServiceProxy::Execute(const ClientContextPtr& context, const MysqlStatement& statement,
                                  MysqlResultSet* result) {
  MysqlRequest request;
  request.sql = statement.GetSql();
  request.is_prepared = true;
  request.params = statement.GetParamsAsStrings();
  
  return ExecuteInternal(context, request, result);
}

Future<MysqlResultSet> MysqlServiceProxy::AsyncExecute(const ClientContextPtr& context,
                                                       const MysqlStatement& statement) {
  MysqlRequest request;
  request.sql = statement.GetSql();
  request.is_prepared = true;
  request.params = statement.GetParamsAsStrings();
  
  return AsyncExecuteInternal(context, request);
}

}  // namespace trpc::mysql
