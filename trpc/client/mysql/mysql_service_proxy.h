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
#include "trpc/client/mysql/mysql_statement.h"
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
  
  /// @brief Execute SQL statement (INSERT/UPDATE/DELETE/DDL)
  /// @param context Client context
  /// @param sql SQL statement
  /// @param result Execution result (affected rows, etc.)
  /// @return Execution status
  Status Execute(const ClientContextPtr& context, const std::string& sql,
                 MysqlResultSet* result);
  
  /// @brief Execute query statement (SELECT)
  /// @param context Client context
  /// @param sql SQL query statement
  /// @param result Query result set
  /// @return Execution status
  Status Query(const ClientContextPtr& context, const std::string& sql,
               MysqlResultSet* result);
  
  /// @brief Execute prepared statement
  /// @param context Client context
  /// @param sql Prepared statement template (use ? as placeholder)
  /// @param params Parameter list
  /// @param result Execution result
  /// @return Execution status
  Status PreparedExecute(const ClientContextPtr& context, const std::string& sql,
                        const std::vector<std::string>& params,
                        MysqlResultSet* result);
  
  // ========== Type-Safe Statement Interfaces (Recommended) ==========
  
  /// @brief Execute statement with type-safe parameters
  /// @param context Client context
  /// @param statement Statement with bound parameters
  /// @param result Execution result
  /// @return Execution status
  Status Execute(const ClientContextPtr& context, const MysqlStatement& statement,
                 MysqlResultSet* result);
  
  /// @brief Execute query with type-safe parameters
  /// @param context Client context
  /// @param statement Statement with bound parameters
  /// @param result Query result set
  /// @return Execution status
  Status Query(const ClientContextPtr& context, const MysqlStatement& statement,
               MysqlResultSet* result);
  
  // ========== Asynchronous Interfaces ==========
  
  /// @brief Asynchronously execute SQL statement
  /// @param context Client context
  /// @param sql SQL statement
  /// @return Future with result
  Future<MysqlResultSet> AsyncExecute(const ClientContextPtr& context,
                                      const std::string& sql);
  
  /// @brief Asynchronously execute query
  /// @param context Client context
  /// @param sql SQL query statement
  /// @return Future with result
  Future<MysqlResultSet> AsyncQuery(const ClientContextPtr& context,
                                    const std::string& sql);
  
  /// @brief Asynchronously execute prepared statement
  /// @param context Client context
  /// @param sql Prepared statement template
  /// @param params Parameter list
  /// @return Future with result
  Future<MysqlResultSet> AsyncPreparedExecute(const ClientContextPtr& context,
                                              const std::string& sql,
                                              const std::vector<std::string>& params);
  
  /// @brief Asynchronously execute type-safe statement
  /// @param context Client context
  /// @param statement Statement with bound parameters
  /// @return Future with result
  Future<MysqlResultSet> AsyncExecute(const ClientContextPtr& context,
                                      const MysqlStatement& statement);
  
  /// @brief Asynchronously query with type-safe statement
  /// @param context Client context
  /// @param statement Statement with bound parameters
  /// @return Future with result
  Future<MysqlResultSet> AsyncQuery(const ClientContextPtr& context,
                                    const MysqlStatement& statement);
  
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
  Future<MysqlResultSet> AsyncExecuteInternal(const ClientContextPtr& context,
                                              const MysqlRequest& request);
  
  /// @brief Parse MySQL configuration from service proxy options
  MysqlConnectionConfig ParseConfig();
  
 private:
  MysqlConnectionPoolPtr conn_pool_;
  MysqlExecutorPtr executor_;
};

using MysqlServiceProxyPtr = std::shared_ptr<MysqlServiceProxy>;

}  // namespace trpc::mysql
