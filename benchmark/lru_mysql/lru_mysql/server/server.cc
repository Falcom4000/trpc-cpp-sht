#include "server/server.h"

#include <memory>
#include <string>
#include <thread>

#include "fmt/format.h"

#include "trpc/client/make_client_context.h"
#include "trpc/client/mysql/mysql_service_proxy.h"
#include "trpc/client/trpc_client.h"
#include "trpc/coroutine/fiber.h"
#include "trpc/log/trpc_log.h"

#include "server/service.h"

namespace trpc {
namespace benchmark {
namespace mysql {

// Initialize MySQL database and create tables
// This function should be called after server starts, when tRPC client is fully initialized
void InitializeMySQLDatabase() {
  TRPC_FMT_INFO("Initializing MySQL database...");
  
  // Wait a bit to ensure client is fully initialized
  ::trpc::FiberSleepFor(std::chrono::milliseconds(500));
  
  auto mysql_proxy = ::trpc::GetTrpcClient()->GetProxy<::trpc::mysql::MysqlServiceProxy>(
      "trpc.mysql.test.service");
  
  if (!mysql_proxy) {
    TRPC_FMT_ERROR("Failed to get MySQL service proxy");
    return;
  }
  
  auto ctx = ::trpc::MakeClientContext(mysql_proxy);
  ctx->SetTimeout(10000);  // 10 seconds timeout for initialization
  
  ::trpc::mysql::MysqlResultSet result;
  
  // Try to create database if not exists (may fail if user doesn't have permission, which is OK)
  // The database is specified in MySQL client configuration, so connection will use it automatically
  std::string create_db_sql = "CREATE DATABASE IF NOT EXISTS test_db";
  auto status = mysql_proxy->Execute(ctx, create_db_sql, &result);
  if (status.OK()) {
    TRPC_FMT_INFO("Database test_db created or already exists");
  } else {
    TRPC_FMT_WARN("Note: Could not create database (may already exist or insufficient permissions): {}", 
                  status.ErrorMessage());
    // Continue anyway, as database might already exist
  }
  
  // Create table if not exists
  // Table structure: id (auto increment primary key), msg (varchar)
  std::string create_table_sql = R"(
    CREATE TABLE IF NOT EXISTS test_table (
      id INT AUTO_INCREMENT PRIMARY KEY,
      msg VARCHAR(255) NOT NULL,
      created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
      INDEX idx_msg (msg)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
  )";
  
  status = mysql_proxy->Execute(ctx, create_table_sql, &result);
  if (!status.OK()) {
    TRPC_FMT_ERROR("Failed to create table: {}", status.ErrorMessage());
    return;
  }
  
  TRPC_FMT_INFO("MySQL database and table initialized successfully");
}

// The initialization logic depending on framework runtime(threadmodel,transport,etc) or plugins(config,metrics,etc) should be placed here.
// Others can simply place at main function(before Main() function invoked)
int LruMysqlServer::Initialize() {
  const auto& config = ::trpc::TrpcConfig::GetInstance()->GetServerConfig();

  // Set the service name, which must be the same as the value of the `/server/service/name` configuration item in the yaml file,
  // otherwise the framework cannot receive requests normally.
  std::string service_name = fmt::format("{}.{}.{}.{}", "trpc", config.app, config.server, "Greeter");
  ::trpc::ServicePtr my_service(std::make_shared<GreeterServiceImpl>());
  InitializeMySQLDatabase();
  RegisterService(service_name, my_service);
  TRPC_FMT_INFO("Register service: {}", service_name);
  return 0;
}

// If the resources initialized in the initialize function need to be destructed when the program exits, it is recommended to place them here.
void LruMysqlServer::Destroy() {}

}  // namespace mysql
}  // namespace benchmark
}  // namespace trpc

int main(int argc, char** argv) {
  ::trpc::benchmark::mysql::LruMysqlServer lrumysql_server;
  lrumysql_server.Main(argc, argv);
  lrumysql_server.Wait();

  return 0;
}
