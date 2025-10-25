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
#include <string>

#include "gflags/gflags.h"

#include "trpc/client/make_client_context.h"
#include "trpc/client/trpc_client.h"
#include "trpc/common/runtime_manager.h"
#include "trpc/log/trpc_log.h"

#include "examples/helloworld/helloworld.trpc.pb.h"

DEFINE_string(client_config, "trpc_cpp.yaml", "framework client_config file, --client_config=trpc_cpp.yaml");
DEFINE_string(service_name, "trpc.test.helloworld.Greeter", "callee service name");
DEFINE_bool(run_broadcast, false, "need to run Broadcast interface");

int DoRpcCall(const std::shared_ptr<::trpc::test::helloworld::GreeterServiceProxy>& proxy) {
  ::trpc::ClientContextPtr client_ctx = ::trpc::MakeClientContext(proxy);
  ::trpc::test::helloworld::HelloRequest req;
  req.set_msg("fiber");
  ::trpc::test::helloworld::HelloReply rsp;
  ::trpc::Status status = proxy->SayHello(client_ctx, req, &rsp);
  if (!status.OK()) {
    std::cerr << "get rpc error: " << status.ErrorMessage() << std::endl;
    return -1;
  }
  std::cout << "get rsp msg: " << rsp.msg() << std::endl;
  return 0;
}

void DoBroadcastCall(const std::shared_ptr<::trpc::test::helloworld::GreeterServiceProxy>& proxy) {
  ::trpc::test::helloworld::HelloRequest request;
  request.set_msg("fiber BroadcastSayHello");

  auto broadcast_context = ::trpc::MakeClientContext(proxy);
  broadcast_context->SetTimeout(1000);
  // broadcast_context 可以设置路由节点条件(北极星方式，比如设置env，namespace，set等等)

  // 同步调用时广播结果
  std::vector<std::tuple<::trpc::Status, ::trpc::test::helloworld::HelloReply>> response;
  ::trpc::Status broadcast_status = proxy->BroadcastSayHello(broadcast_context, request, &response);
  if (broadcast_status.OK()) {
    std::cout << "BroadcastSayHello success" << std::endl;
    // 如需要处理，可通过遍历方式获取每个节点RPC响应结果
    for (auto& itor_resp : response) {
      trpc::test::helloworld::HelloReply hello_reply = std::get<1>(itor_resp);
      std::cout << "reply_msg():" << hello_reply.msg() << std::endl;
    }
  } else {
    // 这里 status的信息应该包含失败原因，比如获取路由节点失败，或者哪些个节点RPC失败
    std::cout << "BroadcastSayHello failed, err_msg:" << broadcast_status.ToString() << std::endl;
    // 同样也可以这进行处理
    // 如果需要处理，通过遍历方式获取每个节点RPC响应结果
    for (auto& itor_resp : response) {
      ::trpc::Status rpc_status = std::get<0>(itor_resp);
      if (rpc_status.OK()) {
        // 只有tuple对应的trpc::Status 为OK时，返回的HelloReply才有意义
        trpc::test::helloworld::HelloReply hello_reply = std::get<1>(itor_resp);
        std::cout << "rpc peer target success, get reply_msg():" << hello_reply.msg() << std::endl;
      } else {
        // 如果tuple中的trpc::Status 不为OK时，包含对应节点RPC失败的信息
        std::cout << "rpc peer target failed, rpc_status:" << rpc_status.ToString() << std::endl;
      }
    }
  }
}

void DoBroadcastOnewayCall(const std::shared_ptr<::trpc::test::helloworld::GreeterServiceProxy>& proxy) {
  ::trpc::test::helloworld::HelloRequest request;
  request.set_msg("fiber BroadcastOnewaySayHello");

  auto broadcast_context = ::trpc::MakeClientContext(proxy);
  broadcast_context->SetTimeout(1000);
  // broadcast_context 可以设置路由节点条件(北极星方式，比如设置env，namespace，set等等)

  // 同步单向调用时广播结果
  ::trpc::Status broadcast_status = proxy->BroadcastSayHello(broadcast_context, request);
  if (broadcast_status.OK()) {
    std::cout << "BroadcastOnewaySayHello success" << std::endl;
  } else {
    // 这里 status的信息应该包含失败原因，比如获取路由节点失败，或者哪些个节点RPC失败
    std::cout << "BroadcastOnewaySayHello failed, err_msg:" << broadcast_status.ToString() << std::endl;
  }
}

void DoAsyncBroadcastCall(const std::shared_ptr<::trpc::test::helloworld::GreeterServiceProxy>& proxy) {
  ::trpc::test::helloworld::HelloRequest request;
  request.set_msg("fiber AsyncBroadcastSayHello");

  auto broadcast_context = ::trpc::MakeClientContext(proxy);
  broadcast_context->SetTimeout(1000);
  // broadcast_context 可以设置路由节点条件(北极星方式，比如设置env，namespace，set等等)

  // 异步Future广播调用结果
  auto fut =
      proxy->AsyncBroadcastSayHello(broadcast_context, request)
          .Then(
              [](::trpc::Future<::trpc::Status,
                                std::vector<std::tuple<::trpc::Status, ::trpc::test::helloworld::HelloReply>>>&& fut) {
                // 这里应该Future 都是Ready，错误信息判断是通过Future.GetValue0()返回的trpc::Status来判断
                if (fut.IsReady()) {
                  auto result = fut.GetValue();
                  trpc::Status broadcast_status = std::get<0>(result);
                  auto response = std::get<1>(result);

                  if (broadcast_status.OK()) {
                    std::cout << "AsyncBroadcastSayHello success" << std::endl;
                    // 如需要处理，可通过遍历方式获取每个节点RPC响应结果
                    for (auto& itor_resp : response) {
                      trpc::test::helloworld::HelloReply hello_reply = std::get<1>(itor_resp);
                      std::cout << "reply_msg():" << hello_reply.msg() << std::endl;
                    }
                  } else {
                    // 这里 status的信息应该包含失败原因，比如获取路由节点失败，或者哪些个节点RPC失败
                    std::cout << "AsyncBroadcastSayHello failed, err_msg:" << broadcast_status.ToString() << std::endl;
                    // 同样也可以这进行处理
                    // 如果需要处理，通过遍历方式获取每个节点RPC响应结果
                    for (auto& itor_resp : response) {
                      ::trpc::Status rpc_status = std::get<0>(itor_resp);
                      if (rpc_status.OK()) {
                        // 只有tuple对应的trpc::Status 为OK时，返回的HelloReply才有意义
                        trpc::test::helloworld::HelloReply hello_reply = std::get<1>(itor_resp);
                        std::cout << "rpc peer target success, get reply_msg():" << hello_reply.msg() << std::endl;
                      } else {
                        // 如果tuple中的trpc::Status 不为OK时，包含对应节点RPC失败的信息
                        std::cout << "rpc peer target failed, rpc_status:" << rpc_status.ToString() << std::endl;
                      }
                    }
                  }
                } else {
                  // 不应该走到这里，Future都是应该是Ready，这里仅做参考
                  auto exception = fut.GetException();
                  // 这里GetException()的信息应该包含失败原因，比如获取路由节点失败，或者哪些个节点RPC失败
                  TRPC_LOG_ERROR("AsyncBroadcastSayHello failed, exception:" << exception.what());
                }

                return ::trpc::MakeReadyFuture<>();
              });

  ::trpc::future::BlockingGet(std::move(fut));
}

int Run() {
  auto proxy = ::trpc::GetTrpcClient()->GetProxy<::trpc::test::helloworld::GreeterServiceProxy>(FLAGS_service_name);

  DoRpcCall(proxy);

  if (FLAGS_run_broadcast == true) {
    DoBroadcastCall(proxy);

    DoBroadcastOnewayCall(proxy);

    DoAsyncBroadcastCall(proxy);
  }

  return 0;
}

void ParseClientConfig(int argc, char* argv[]) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::CommandLineFlagInfo info;
  if (GetCommandLineFlagInfo("client_config", &info) && info.is_default) {
    std::cerr << "start client with client_config, for example: " << argv[0]
              << " --client_config=/client/client_config/filepath" << std::endl;
    exit(-1);
  }

  std::cout << "FLAGS_service_name:" << FLAGS_service_name << std::endl;
  std::cout << "FLAGS_client_config:" << FLAGS_client_config << std::endl;

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
  return ::trpc::RunInTrpcRuntime([]() { return Run(); });
}
