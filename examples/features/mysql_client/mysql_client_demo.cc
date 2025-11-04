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

#include <iostream>
#include <memory>
#include <string>

#include "trpc/client/make_client_context.h"
#include "trpc/client/mysql/mysql_service_proxy.h"
#include "trpc/client/trpc_client.h"
#include "trpc/common/runtime_manager.h"
#include "trpc/common/trpc_app.h"

namespace examples::mysql_client {

class MysqlClientApp : public ::trpc::TrpcApp {
 public:
  int Initialize() override {
    // Initialize tRPC runtime
    const auto& config = ::trpc::TrpcConfig::GetInstance()->GetServerConfig();
    ::trpc::TrpcPlugin::GetInstance()->RegisterPlugins();
    
    return 0;
  }
  
  void Destroy() override {}
  
  void Run() override {
    // Create MySQL service proxy
    auto mysql_proxy = ::trpc::GetTrpcClient()->GetProxy<::trpc::mysql::MysqlServiceProxy>(
        "trpc.mysql.test.service");
    
    // Example 1: Simple SELECT query (synchronous)
    std::cout << "=== Example 1: Synchronous SELECT query ===" << std::endl;
    {
      auto ctx = ::trpc::MakeClientContext(mysql_proxy);
      ctx->SetTimeout(5000);  // 5 seconds timeout
      
      ::trpc::mysql::MysqlResultSet result;
      auto status = mysql_proxy->Execute(ctx, "SELECT 1 AS num, 'Hello' AS msg", &result);
      
      if (status.OK()) {
        std::cout << "Query succeeded!" << std::endl;
        std::cout << "Field count: " << result.field_count << std::endl;
        
        // Print field names
        std::cout << "Fields: ";
        for (const auto& field_name : result.field_names) {
          std::cout << field_name << " ";
        }
        std::cout << std::endl;
        
        // Print rows
        for (const auto& row : result.rows) {
          for (const auto& field : row) {
            std::cout << field << " ";
          }
          std::cout << std::endl;
        }
      } else {
        std::cerr << "Query failed: " << status.ErrorMessage() << std::endl;
      }
    }
    
    // Example 2: INSERT statement (synchronous)
    std::cout << "\n=== Example 2: Synchronous INSERT ===" << std::endl;
    {
      auto ctx = ::trpc::MakeClientContext(mysql_proxy);
      ctx->SetTimeout(5000);
      
      ::trpc::mysql::MysqlResultSet result;
      
      // Create temporary table
      auto status = mysql_proxy->Execute(ctx,
          "CREATE TEMPORARY TABLE test_users (id INT AUTO_INCREMENT PRIMARY KEY, name VARCHAR(100))",
          &result);
      
      if (status.OK()) {
        std::cout << "Table created successfully" << std::endl;
        
        // Insert data
        status = mysql_proxy->Execute(ctx, "INSERT INTO test_users (name) VALUES ('Alice')", &result);
        if (status.OK()) {
          std::cout << "Insert succeeded! Affected rows: " << result.affected_rows
                    << ", Insert ID: " << result.insert_id << std::endl;
        }
      } else {
        std::cerr << "Failed to create table: " << status.ErrorMessage() << std::endl;
      }
    }
    
    // Example 3: Prepared statement (synchronous)
    std::cout << "\n=== Example 3: Prepared statement ===" << std::endl;
    {
      auto ctx = ::trpc::MakeClientContext(mysql_proxy);
      ctx->SetTimeout(5000);
      
      ::trpc::mysql::MysqlResultSet result;
      
      // Insert using prepared statement
      std::vector<std::string> params;
      params.push_back("Bob");
      auto status = mysql_proxy->Execute(ctx, "INSERT INTO test_users (name) VALUES (?)", params, &result);
      
      if (status.OK()) {
        std::cout << "Prepared insert succeeded! Insert ID: " << result.insert_id << std::endl;
        
        // Query using prepared statement
        params.clear();
        params.push_back("Bob");
        status = mysql_proxy->Execute(ctx, "SELECT * FROM test_users WHERE name = ?", params, &result);
        
        if (status.OK()) {
          std::cout << "Found " << result.rows.size() << " rows" << std::endl;
          for (const auto& row : result.rows) {
            std::cout << "ID: " << row[0] << ", Name: " << row[1] << std::endl;
          }
        }
      } else {
        std::cerr << "Prepared statement failed: " << status.ErrorMessage() << std::endl;
      }
    }
    
    // Example 4: Asynchronous query
    std::cout << "\n=== Example 4: Asynchronous query ===" << std::endl;
    {
      auto ctx = ::trpc::MakeClientContext(mysql_proxy);
      ctx->SetTimeout(5000);
      
      auto future = mysql_proxy->AsyncExecute(ctx, "SELECT * FROM test_users");
      
      // Wait for result
      future.Then([](::trpc::Future<::trpc::mysql::MysqlResultSet>&& f) {
        if (f.IsReady()) {
          auto result = f.GetValue0();
          std::cout << "Async query succeeded! Rows: " << result.rows.size() << std::endl;
          for (const auto& row : result.rows) {
            std::cout << "ID: " << row[0] << ", Name: " << row[1] << std::endl;
          }
        } else {
          std::cerr << "Async query failed!" << std::endl;
        }
        return ::trpc::MakeReadyFuture<>();
      }).Wait();
    }
    
    // Example 5: Multiple queries
    std::cout << "\n=== Example 5: Multiple queries ===" << std::endl;
    {
      auto ctx = ::trpc::MakeClientContext(mysql_proxy);
      ctx->SetTimeout(5000);
      
      ::trpc::mysql::MysqlResultSet result;
      
      // Insert more data
      for (int i = 0; i < 5; ++i) {
        std::string sql = "INSERT INTO test_users (name) VALUES ('User" + std::to_string(i) + "')";
        mysql_proxy->Execute(ctx, sql, &result);
      }
      
      // Query all users
      auto status = mysql_proxy->Execute(ctx, "SELECT COUNT(*) AS total FROM test_users", &result);
      if (status.OK() && !result.rows.empty()) {
        std::cout << "Total users in table: " << result.rows[0][0] << std::endl;
      }
    }
    
    std::cout << "\n=== All examples completed ===" << std::endl;
  }
};

}  // namespace examples::mysql_client

int main(int argc, char* argv[]) {
  examples::mysql_client::MysqlClientApp app;
  
  int ret = app.Main(argc, argv);
  if (ret != 0) {
    std::cerr << "Failed to start application" << std::endl;
    return ret;
  }
  
  app.Run();
  
  app.Wait();
  
  return 0;
}
