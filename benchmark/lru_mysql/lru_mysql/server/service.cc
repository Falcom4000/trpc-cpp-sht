#include "server/service.h"

#include "trpc/client/make_client_context.h"
#include "trpc/client/mysql/mysql_service_proxy.h"
#include "trpc/client/trpc_client.h"
#include "trpc/log/trpc_log.h"

namespace trpc {
namespace benchmark {
namespace mysql {

::trpc::Status GreeterServiceImpl::GetID(::trpc::ServerContextPtr context, const ::trpc::benchmark::mysql::IDRequest* request, ::trpc::benchmark::mysql::IDReply* reply) {
  TRPC_FMT_INFO("GetID request received, msg: {}", request->msg());
  
  // Get MySQL service proxy
  // Note: The service name should match the configuration in trpc_cpp.yaml
  auto mysql_proxy = ::trpc::GetTrpcClient()->GetProxy<::trpc::mysql::MysqlServiceProxy>(
      "trpc.mysql.test.service");
  
  // Create client context
  auto ctx = ::trpc::MakeClientContext(mysql_proxy);
  ctx->SetTimeout(30000);  // 30 seconds timeout (enough for connection pool wait + SQL execution)
  
  // Execute SQL query to get ID based on msg
  // Example: Assuming there's a table with columns (id, msg)
  // You may need to adjust the SQL query and table name according to your actual database schema
  std::string sql = "SELECT id FROM test_table WHERE msg = ?";
  std::vector<std::string> params;
  params.push_back(request->msg());
  
  ::trpc::mysql::MysqlResultSet result;
  auto status = mysql_proxy->Execute(ctx, sql, params, &result);
  
  if (status.OK()) {
    if (!result.rows.empty() && !result.rows[0].empty()) {
      // Get the ID from the first row, first column
      std::string id_str = result.rows[0][0];
      reply->set_msg(id_str);
      TRPC_FMT_INFO("GetID succeeded, found ID: {}", id_str);
      return ::trpc::kSuccStatus;
    } else {
      // No matching record found
      reply->set_msg("NOT_FOUND");
      TRPC_FMT_WARN("GetID: No record found for msg: {}", request->msg());
      return ::trpc::kSuccStatus;
    }
  } else {
    // Query failed
    std::string error_msg = "Query failed: " + status.ErrorMessage();
    reply->set_msg(error_msg);
    TRPC_FMT_ERROR("GetID failed: {}", status.ErrorMessage());
    return status;
  }
}

}  // namespace mysql
}  // namespace benchmark
}  // namespace trpc
