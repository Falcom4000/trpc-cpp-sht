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
#include <string>
#include <vector>

#include "trpc/client/mysql/mysql_common.h"
#include "trpc/client/mysql/mysql_connection_pool.h"
#include "trpc/client/mysql/mysql_executor.h"
#include "trpc/client/service_proxy.h"
#include "trpc/common/future/future.h"
#include "trpc/common/status.h"

namespace trpc::mysql {

/// @brief MySQL service proxy, inherits from ServiceProxy to reuse framework capabilities
class MysqlServiceProxy : public ServiceProxy {
 public:
  MysqlServiceProxy() = default;
  ~MysqlServiceProxy() override;
  
  /// @brief Stop the service proxy
  void Stop() override;
  
  /// @brief Destroy the service proxy
  void Destroy() override;
  
  // ========== Synchronous Interfaces ==========
  
  /// @brief Execute SQL statement with parameters
  /// @param context Client context
  /// @param sql SQL statement (can contain '?' placeholders for parameters)
  /// @param params Parameters for prepared statement (optional)
  /// @param result Execution result
  /// @return Execution status
  Status Execute(const ClientContextPtr& context, const std::string& sql,
                 const std::vector<std::string>& params, MysqlResultSet* result);
  
  /// @brief Execute SQL statement without parameters
  /// @param context Client context
  /// @param sql SQL statement
  /// @param result Execution result
  /// @return Execution status
  Status Execute(const ClientContextPtr& context, const std::string& sql,
                 MysqlResultSet* result);
  
  // ========== Asynchronous Interfaces ==========
  
  /// @brief Asynchronously execute SQL statement with parameters
  /// @param context Client context
  /// @param sql SQL statement (can contain '?' placeholders for parameters)
  /// @param params Parameters for prepared statement (optional)
  /// @return Future with result
  /// @note The returned Future must be handled (e.g., via Then() or BlockingGet()),
  ///       otherwise the connection may leak.
  [[nodiscard]] Future<MysqlResultSet> AsyncExecute(const ClientContextPtr& context,
                                                     const std::string& sql,
                                                     const std::vector<std::string>& params);
  
  /// @brief Asynchronously execute SQL statement without parameters
  /// @param context Client context
  /// @param sql SQL statement
  /// @return Future with result
  /// @note The returned Future must be handled (e.g., via Then() or BlockingGet()),
  ///       otherwise the connection may leak.
  [[nodiscard]] Future<MysqlResultSet> AsyncExecute(const ClientContextPtr& context,
                                                     const std::string& sql);
  
 protected:
  /// @brief Initialize transport (use custom connection pool instead of standard transport)
  void InitTransport() override;
  
  /// @brief Check if selector is needed (can support master-slave scenarios)
  bool NeedSelector() override { return false; }
  
 private:
  /// @brief Execute SQL internally (synchronous)
  Status ExecuteInternal(const ClientContextPtr& context, const MysqlRequest& request,
                        MysqlResultSet* result);
  
  /// @brief Execute SQL internally (asynchronous)
  /// @note The returned Future must be handled, otherwise the connection may leak.
  [[nodiscard]] Future<MysqlResultSet> AsyncExecuteInternal(const ClientContextPtr& context,
                                                             const MysqlRequest& request);
  
 private:
  MysqlConnectionPoolPtr conn_pool_;
  MysqlExecutorPtr executor_;
};

using MysqlServiceProxyPtr = std::shared_ptr<MysqlServiceProxy>;

}  // namespace trpc::mysql
