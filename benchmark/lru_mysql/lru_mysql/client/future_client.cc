#include <iostream>
#include <string>

#include "gflags/gflags.h"

#include "trpc/client/make_client_context.h"
#include "trpc/client/trpc_client.h"
#include "trpc/common/runtime_manager.h"
#include "trpc/log/trpc_log.h"
#include "trpc/util/thread/latch.h"

#include "lru_mysql.trpc.pb.h"

DEFINE_string(client_config, "trpc_cpp.yaml", "framework client config file, --client_config=trpc_cpp.yaml");
DEFINE_string(service_name, "trpc.benchmark.mysql.Greeter", "callee service name");


int GreeterAsyncGetID(const std::shared_ptr<::trpc::benchmark::mysql::GreeterServiceProxy>& proxy) {
  ::trpc::ClientContextPtr client_ctx = ::trpc::MakeClientContext(proxy);
  ::trpc::benchmark::mysql::IDRequest req;
  // fill some filed of req
  bool succ = true;
  ::trpc::Latch latch(1);
  proxy->AsyncGetID(client_ctx, req).
      Then([&latch, &succ](::trpc::Future<::trpc::benchmark::mysql::IDReply>&& fut) {
        if (fut.IsReady()) {
          auto rsp = fut.GetValue0();
          // print some filed of rsp
          std::cout << "get rsp success" << std::endl;
        } else {
          auto exception = fut.GetException();
          succ = false;
          std::cerr << "get rpc error: " << exception.what() << std::endl;
        }
        latch.count_down();
        return ::trpc::MakeReadyFuture<>();
      });
  latch.wait();
  return succ ? 0 : -1;
}


int Run() {
  auto proxy = ::trpc::GetTrpcClient()->GetProxy<::trpc::benchmark::mysql::GreeterServiceProxy>(FLAGS_service_name);
  return GreeterAsyncGetID(proxy);
}

void ParseClientConfig(int argc, char* argv[]) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::CommandLineFlagInfo info;
  if (GetCommandLineFlagInfo("client_config", &info) && info.is_default) {
    TRPC_FMT_ERROR("start client with config, for example:{} --client_config=/client/config/filepath", argv[0]);
    exit(-1);
  }
  
  std::cout << "FLAGS_service_name: " << FLAGS_service_name << std::endl;
  std::cout << "FLAGS_client_config: " << FLAGS_client_config << std::endl;

  int ret = ::trpc::TrpcConfig::GetInstance()->Init(FLAGS_client_config);
  if (ret != 0) {
    std::cerr << "load client_config failed." << std::endl;
    exit(-1);
  }
}

int main(int argc, char* argv[]) {
  ParseClientConfig(argc, argv);
  // If the business code is running in trpc pure client mode,
  // the business code needs to be running in the `RunInTrpcRuntime` function
  // This function can be seen as a program entry point and should be called only once.
  return ::trpc::RunInTrpcRuntime([]() {
    return Run();
  });
}
