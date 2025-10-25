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

#include "trpc/client/rpc_service_proxy.h"

#include <memory>
#include <string>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "trpc/client/make_client_context.h"
#include "trpc/client/service_proxy_option_setter.h"
#include "trpc/client/testing/service_proxy_testing.h"
#include "trpc/codec/trpc/testing/trpc_protocol_testing.h"
#include "trpc/codec/trpc/trpc_client_codec.h"
#include "trpc/common/runtime_manager.h"
#include "trpc/filter/filter_manager.h"
#include "trpc/future/future_utility.h"
#include "trpc/naming/common/util/loadbalance/polling/polling_load_balance.h"
#include "trpc/naming/direct/direct_selector_filter.h"
#include "trpc/naming/direct/selector_direct.h"
#include "trpc/naming/selector_factory.h"
#include "trpc/proto/testing/helloworld.pb.h"
#include "trpc/proto/testing/helloworld_generated.h"
#include "trpc/serialization/serialization_type.h"
#include "trpc/stream/testing/mock_stream_provider.h"
#include "trpc/util/buffer/noncontiguous_buffer.h"
#include "trpc/util/buffer/zero_copy_stream.h"
#include "trpc/util/flatbuffers/trpc_fbs.h"

namespace trpc::testing {

class MockTrpcClientCodec : public TrpcClientCodec {
 public:
  std::string Name() const { return "mock_codec"; }

  MOCK_METHOD(bool, FillRequest, (const ClientContextPtr& context, const ProtocolPtr& in, void* out), (override));
  MOCK_METHOD(bool, FillResponse, (const ClientContextPtr& context, const ProtocolPtr& in, void* out), (override));
};

class MockRpcServiceProxy : public RpcServiceProxy {
 public:
  stream::StreamReaderWriterProviderPtr SelectStreamProvider(const ClientContextPtr& context,
                                                             void* rpc_reply_msg = nullptr) override {
    if (!stream_) stream_ = testing::CreateMockStreamReaderWriterProvider();
    return stream_;
  }

  void SetMockServiceProxyOption(const std::shared_ptr<ServiceProxyOption>& option) {
    SetServiceProxyOptionInner(option);
  }

  void SetMockEndpointInfo(const std::string& endpoint_info) { SetEndpointInfo(endpoint_info); }

  MOCK_METHOD(void, UnaryTransportInvoke, (const ClientContextPtr& context, const ProtocolPtr& req, ProtocolPtr& rsp),
              (override));

  MOCK_METHOD(Future<ProtocolPtr>, AsyncUnaryTransportInvoke,
              (const ClientContextPtr& context, const ProtocolPtr& req_protocol), (override));

  MOCK_METHOD(void, OnewayTransportInvoke, (const ClientContextPtr& context, const ProtocolPtr& req), (override));

 protected:
  stream::StreamReaderWriterProviderPtr stream_;
};

using MockRpcServiceProxyPtr = std::shared_ptr<MockRpcServiceProxy>;

class RpcServiceProxyTestFixture : public ::testing::Test {
 public:
  static void SetUpTestCase() {
    codec_ = std::make_shared<MockTrpcClientCodec>();
    ClientCodecFactory::GetInstance()->Register(codec_);
    RegisterPlugins();

    detail::SetDefaultOption(option_);
    option_->name = "trpc.test.helloworld.Greeter";
    option_->caller_name = "Test.HelloWorldClient";
    option_->codec_name = "mock_codec";
    option_->conn_type = "long";
    option_->network = "tcp";
    option_->timeout = 1000;
    option_->target = "127.0.0.1:10001";
    option_->selector_name = "direct";

    detail::SetDefaultOption(broadcast_option_);
    broadcast_option_->name = "trpc.test.helloworld.BroadcastGreeter";
    broadcast_option_->caller_name = "Test.BroadcastHelloWorldClient";
    broadcast_option_->codec_name = "mock_codec";
    broadcast_option_->conn_type = "long";
    broadcast_option_->network = "tcp";
    broadcast_option_->timeout = 1000;
    broadcast_option_->target = "127.0.0.1:10001,127.0.0.1:10002,127.0.0.1:10003,127.0.0.1:10004,127.0.0.1:10005";
    broadcast_option_->selector_name = "direct";
  }

  static void TearDownTestCase() { UnregisterPlugins(); }

 protected:
  void SetUp() override {
    mock_rpc_service_proxy_ = std::make_shared<MockRpcServiceProxy>();
    mock_rpc_service_proxy_->SetMockServiceProxyOption(option_);

    mock_broadcast_rpc_service_proxy_ = std::make_shared<MockRpcServiceProxy>();
    mock_broadcast_rpc_service_proxy_->SetMockServiceProxyOption(broadcast_option_);
    mock_broadcast_rpc_service_proxy_->SetMockEndpointInfo(broadcast_option_->target);
  }

  void TearDown() override {
    mock_rpc_service_proxy_->Stop();
    mock_rpc_service_proxy_->Destroy();

    mock_broadcast_rpc_service_proxy_->Stop();
    mock_broadcast_rpc_service_proxy_->Destroy();
  }

  ClientContextPtr GetClientContext(ServiceProxyPtr proxy = nullptr) {
    if (proxy == nullptr) {
      proxy = mock_rpc_service_proxy_;
    }
    auto ctx = MakeClientContext(proxy);
    ctx->SetAddr("127.0.0.1", 10001);
    return ctx;
  }

 protected:
  static std::shared_ptr<ServiceProxyOption> option_;
  static std::shared_ptr<MockTrpcClientCodec> codec_;
  MockRpcServiceProxyPtr mock_rpc_service_proxy_{nullptr};

  static std::shared_ptr<ServiceProxyOption> broadcast_option_;
  MockRpcServiceProxyPtr mock_broadcast_rpc_service_proxy_{nullptr};
};

std::shared_ptr<ServiceProxyOption> RpcServiceProxyTestFixture::option_ = std::make_shared<ServiceProxyOption>();
std::shared_ptr<MockTrpcClientCodec> RpcServiceProxyTestFixture::codec_ = nullptr;

std::shared_ptr<ServiceProxyOption> RpcServiceProxyTestFixture::broadcast_option_ =
    std::make_shared<ServiceProxyOption>();

// unary invoke ok
TEST_F(RpcServiceProxyTestFixture, UnaryInvokeOk) {
  EXPECT_CALL(*codec_, FillRequest(::testing::_, ::testing::_, ::testing::_))
      .Times(1)
      .WillOnce(::testing::Return(true));
  EXPECT_CALL(*codec_, FillResponse(::testing::_, ::testing::_, ::testing::_))
      .Times(1)
      .WillOnce(::testing::Return(true));

  ProtocolPtr rsp_data = codec_->CreateResponsePtr();
  EXPECT_CALL(*mock_rpc_service_proxy_, UnaryTransportInvoke(::testing::_, ::testing::_, ::testing::_))
      .Times(1)
      .WillOnce(::testing::SetArgReferee<2>(rsp_data));

  auto client_context = GetClientContext();
  test::helloworld::HelloRequest hello_req;
  test::helloworld::HelloReply hello_rsp;
  Status status = mock_rpc_service_proxy_->UnaryInvoke<test::helloworld::HelloRequest, test::helloworld::HelloReply>(
      client_context, hello_req, &hello_rsp);

  EXPECT_TRUE(status.OK());
}

// fillrequest failed
TEST_F(RpcServiceProxyTestFixture, UnaryInvokeFillRequestError) {
  EXPECT_CALL(*codec_, FillRequest(::testing::_, ::testing::_, ::testing::_))
      .Times(1)
      .WillOnce(::testing::Return(false));

  auto client_context = GetClientContext();
  test::helloworld::HelloRequest hello_req;
  test::helloworld::HelloReply hello_rsp;
  Status status = mock_rpc_service_proxy_->UnaryInvoke<test::helloworld::HelloRequest, test::helloworld::HelloReply>(
      client_context, hello_req, &hello_rsp);

  EXPECT_FALSE(status.OK());
  EXPECT_EQ(status.GetFrameworkRetCode(), TrpcRetCode::TRPC_CLIENT_ENCODE_ERR);
  EXPECT_EQ(status.GetFuncRetCode(), 0);
}

// encode fail
TEST_F(RpcServiceProxyTestFixture, UnaryInvokeEncodeError) {
  EXPECT_CALL(*codec_, FillRequest(::testing::_, ::testing::_, ::testing::_))
      .Times(1)
      .WillOnce(::testing::Return(true));

  auto client_context = GetClientContext();
  EXPECT_CALL(*mock_rpc_service_proxy_, UnaryTransportInvoke(::testing::_, ::testing::_, ::testing::_))
      .WillOnce(::testing::Invoke([client_context](const ClientContextPtr&, const ProtocolPtr&, ProtocolPtr&) {
        client_context->SetStatus(Status(TrpcRetCode::TRPC_CLIENT_ENCODE_ERR, 0, "encode failed"));
      }));

  test::helloworld::HelloRequest hello_req;
  test::helloworld::HelloReply hello_rsp;
  Status ret_status =
      mock_rpc_service_proxy_->UnaryInvoke<test::helloworld::HelloRequest, test::helloworld::HelloReply>(
          client_context, hello_req, &hello_rsp);

  EXPECT_FALSE(ret_status.OK());
  EXPECT_EQ(ret_status.GetFrameworkRetCode(), TrpcRetCode::TRPC_CLIENT_ENCODE_ERR);
}

// invoke error
TEST_F(RpcServiceProxyTestFixture, UnaryInvokeInvokeError) {
  EXPECT_CALL(*codec_, FillRequest(::testing::_, ::testing::_, ::testing::_))
      .Times(1)
      .WillOnce(::testing::Return(true));

  int func_ret = -3;
  auto client_context = GetClientContext();
  ProtocolPtr rsp_data = codec_->CreateResponsePtr();
  EXPECT_CALL(*mock_rpc_service_proxy_, UnaryTransportInvoke(::testing::_, ::testing::_, ::testing::_))
      .Times(1)
      .WillOnce(::testing::DoAll(
          ::testing::SetArgReferee<2>(rsp_data),
          ::testing::Invoke([client_context, func_ret](const ClientContextPtr&, const ProtocolPtr&, ProtocolPtr&) {
            client_context->SetStatus(Status(TrpcRetCode::TRPC_INVOKE_UNKNOWN_ERR, func_ret, "invoke failed"));
          })));

  test::helloworld::HelloRequest hello_req;
  test::helloworld::HelloReply hello_rsp;
  auto ret_status = mock_rpc_service_proxy_->UnaryInvoke<test::helloworld::HelloRequest, test::helloworld::HelloReply>(
      client_context, hello_req, &hello_rsp);

  EXPECT_FALSE(ret_status.OK());
  EXPECT_EQ(ret_status.GetFuncRetCode(), func_ret);
}

// decode fail
TEST_F(RpcServiceProxyTestFixture, UnaryInvokeDecodeError) {
  EXPECT_CALL(*codec_, FillRequest(::testing::_, ::testing::_, ::testing::_))
      .Times(1)
      .WillOnce(::testing::Return(true));

  auto client_context = GetClientContext();
  ProtocolPtr rsp_data = codec_->CreateResponsePtr();
  EXPECT_CALL(*mock_rpc_service_proxy_, UnaryTransportInvoke(::testing::_, ::testing::_, ::testing::_))
      .Times(1)
      .WillOnce(::testing::DoAll(
          ::testing::SetArgReferee<2>(rsp_data),
          ::testing::Invoke([client_context](const ClientContextPtr&, const ProtocolPtr&, ProtocolPtr&) {
            client_context->SetStatus(Status(TrpcRetCode::TRPC_CLIENT_DECODE_ERR, 0, "decode failed"));
          })));

  test::helloworld::HelloRequest hello_req;
  test::helloworld::HelloReply hello_rsp;
  Status ret_status =
      mock_rpc_service_proxy_->UnaryInvoke<test::helloworld::HelloRequest, test::helloworld::HelloReply>(
          client_context, hello_req, &hello_rsp);

  EXPECT_FALSE(ret_status.OK());
  EXPECT_EQ(ret_status.GetFrameworkRetCode(), TrpcRetCode::TRPC_CLIENT_DECODE_ERR);
}

// fill response faild
TEST_F(RpcServiceProxyTestFixture, UnaryInvokeFillResponseError) {
  EXPECT_CALL(*codec_, FillRequest(::testing::_, ::testing::_, ::testing::_))
      .Times(1)
      .WillOnce(::testing::Return(true));
  EXPECT_CALL(*codec_, FillResponse(::testing::_, ::testing::_, ::testing::_))
      .Times(1)
      .WillOnce(::testing::Return(false));

  ProtocolPtr rsp_data = codec_->CreateResponsePtr();
  EXPECT_CALL(*mock_rpc_service_proxy_, UnaryTransportInvoke(::testing::_, ::testing::_, ::testing::_))
      .Times(1)
      .WillOnce(::testing::SetArgReferee<2>(rsp_data));

  auto client_context = GetClientContext();
  test::helloworld::HelloRequest hello_req;
  test::helloworld::HelloReply hello_rsp;
  Status status = mock_rpc_service_proxy_->UnaryInvoke<test::helloworld::HelloRequest, test::helloworld::HelloReply>(
      client_context, hello_req, &hello_rsp);

  EXPECT_FALSE(status.OK());
  EXPECT_EQ(status.GetFrameworkRetCode(), TrpcRetCode::TRPC_CLIENT_DECODE_ERR);
}

// PbSerializedReqUnaryInvoke
TEST_F(RpcServiceProxyTestFixture, PbSerializedReqUnaryInvokeNormal) {
  EXPECT_CALL(*codec_, FillRequest(::testing::_, ::testing::_, ::testing::_))
      .Times(::testing::AtLeast(1))
      .WillOnce(::testing::Return(true));

  EXPECT_CALL(*codec_, FillResponse(::testing::_, ::testing::_, ::testing::_))
      .Times(::testing::AtLeast(1))
      .WillOnce(::testing::Return(true));

  ProtocolPtr rsp_data = codec_->CreateResponsePtr();
  EXPECT_CALL(*mock_rpc_service_proxy_, UnaryTransportInvoke(::testing::_, ::testing::_, ::testing::_))
      .Times(::testing::AtLeast(1))
      .WillOnce(::testing::SetArgReferee<2>(rsp_data));

  // Serialize the PB request into a non-contiguous buffer.
  test::helloworld::HelloRequest hello_req;
  NoncontiguousBuffer pb_serialized_buffer;
  NoncontiguousBufferBuilder builder;
  NoncontiguousBufferOutputStream nbos(&builder);
  hello_req.SerializePartialToZeroCopyStream(&nbos);
  nbos.Flush();
  pb_serialized_buffer = builder.DestructiveGet();

  auto client_context = GetClientContext();
  test::helloworld::HelloReply hello_rsp;
  Status status =
      mock_rpc_service_proxy_->PbSerializedReqUnaryInvoke(client_context, std::move(pb_serialized_buffer), &hello_rsp);
  EXPECT_EQ(status.GetFrameworkRetCode(), 0);
}

// async unary invoke ok
TEST_F(RpcServiceProxyTestFixture, AsyncUnaryInvokeOk) {
  EXPECT_CALL(*codec_, FillRequest(::testing::_, ::testing::_, ::testing::_))
      .Times(1)
      .WillOnce(::testing::Return(true));
  EXPECT_CALL(*codec_, FillResponse(::testing::_, ::testing::_, ::testing::_))
      .Times(1)
      .WillOnce(::testing::Return(true));
  EXPECT_CALL(*mock_rpc_service_proxy_, AsyncUnaryTransportInvoke(::testing::_, ::testing::_))
      .Times(1)
      .WillOnce(::testing::Return(::testing::ByMove(MakeReadyFuture<ProtocolPtr>(codec_->CreateResponsePtr()))));

  auto client_context = GetClientContext();
  test::helloworld::HelloRequest hello_req;
  auto fut =
      mock_rpc_service_proxy_
          ->AsyncUnaryInvoke<test::helloworld::HelloRequest, test::helloworld::HelloReply>(client_context, hello_req)
          .Then([client_context](Future<test::helloworld::HelloReply>&& fut) {
            EXPECT_TRUE(fut.IsReady());
            Status status = client_context->GetStatus();
            EXPECT_TRUE(status.OK());
            return MakeReadyFuture<>();
          });
  future::BlockingGet(std::move(fut));
}

TEST_F(RpcServiceProxyTestFixture, AsyncUnaryInvokeError) {
  EXPECT_CALL(*codec_, FillRequest(::testing::_, ::testing::_, ::testing::_))
      .Times(1)
      .WillOnce(::testing::Return(true));

  // response error
  EXPECT_CALL(*mock_rpc_service_proxy_, AsyncUnaryTransportInvoke(::testing::_, ::testing::_))
      .Times(1)
      .WillOnce(::testing::Return(::testing::ByMove(MakeExceptionFuture<ProtocolPtr>(
          CommonException("invoke error", TrpcRetCode::TRPC_CLIENT_INVOKE_TIMEOUT_ERR)))));

  auto client_context = GetClientContext();
  test::helloworld::HelloRequest hello_req;
  hello_req.set_msg("test");

  auto fut =
      mock_rpc_service_proxy_
          ->AsyncUnaryInvoke<test::helloworld::HelloRequest, test::helloworld::HelloReply>(client_context, hello_req)
          .Then([](Future<test::helloworld::HelloReply>&& fut) {
            EXPECT_TRUE(fut.IsFailed());
            return MakeReadyFuture<>();
          });
  future::BlockingGet(std::move(fut));
}

// async unary invoke when fill request error
TEST_F(RpcServiceProxyTestFixture, AsyncUnaryInvokeFillRequestError) {
  EXPECT_CALL(*codec_, FillRequest(::testing::_, ::testing::_, ::testing::_))
      .Times(1)
      .WillOnce(::testing::Return(false));

  auto client_context = GetClientContext();
  test::helloworld::HelloRequest hello_req;
  auto fut =
      mock_rpc_service_proxy_
          ->AsyncUnaryInvoke<test::helloworld::HelloRequest, test::helloworld::HelloReply>(client_context, hello_req)
          .Then([client_context](Future<test::helloworld::HelloReply>&& fut) {
            EXPECT_TRUE(fut.IsFailed());
            Status status = client_context->GetStatus();
            EXPECT_FALSE(status.OK());
            EXPECT_EQ(status.GetFrameworkRetCode(), TrpcRetCode::TRPC_CLIENT_ENCODE_ERR);
            return MakeReadyFuture<>();
          });
  future::BlockingGet(std::move(fut));
}

// async unary invoke when fill response error
TEST_F(RpcServiceProxyTestFixture, AsyncUnaryInvokeFillResponseError) {
  EXPECT_CALL(*codec_, FillRequest(::testing::_, ::testing::_, ::testing::_))
      .Times(1)
      .WillOnce(::testing::Return(true));
  EXPECT_CALL(*codec_, FillResponse(::testing::_, ::testing::_, ::testing::_))
      .Times(1)
      .WillOnce(::testing::Return(false));
  EXPECT_CALL(*mock_rpc_service_proxy_, AsyncUnaryTransportInvoke(::testing::_, ::testing::_))
      .Times(1)
      .WillOnce(::testing::Return(::testing::ByMove(MakeReadyFuture<ProtocolPtr>(codec_->CreateResponsePtr()))));

  auto client_context = GetClientContext();
  test::helloworld::HelloRequest hello_req;
  auto fut =
      mock_rpc_service_proxy_
          ->AsyncUnaryInvoke<test::helloworld::HelloRequest, test::helloworld::HelloReply>(client_context, hello_req)
          .Then([client_context](Future<test::helloworld::HelloReply>&& fut) {
            EXPECT_TRUE(fut.IsFailed());
            Status status = client_context->GetStatus();
            EXPECT_FALSE(status.OK());
            EXPECT_EQ(status.GetFrameworkRetCode(), TrpcRetCode::TRPC_CLIENT_DECODE_ERR);
            return MakeReadyFuture<>();
          });
  future::BlockingGet(std::move(fut));
}

TEST_F(RpcServiceProxyTestFixture, AsyncStreamInvoke1) {
  auto client_context = GetClientContext();
  auto stream = dynamic_pointer_cast<testing::MockStreamReaderWriterProvider>(
      mock_rpc_service_proxy_->SelectStreamProvider(client_context));
  EXPECT_CALL(*stream, AsyncStart()).WillOnce(::testing::Return(::testing::ByMove(MakeReadyFuture<>())));
  EXPECT_CALL(*stream, AsyncRead(1000))
      .WillOnce(::testing::Return(::testing::ByMove(
          MakeReadyFuture<std::optional<NoncontiguousBuffer>>(std::make_optional<NoncontiguousBuffer>()))));

  client_context->SetTimeout(1000);
  auto fut = mock_rpc_service_proxy_
                 ->AsyncStreamInvoke<test::helloworld::HelloRequest, test::helloworld::HelloReply>(client_context)
                 .Then([](std::pair<stream::AsyncWriterPtr<test::helloworld::HelloRequest>,
                                    Future<test::helloworld::HelloReply>>&& p) {
                   EXPECT_TRUE(p.second.IsReady());
                   return MakeReadyFuture<>();
                 });
  fut = future::BlockingGet(std::move(fut));
  EXPECT_TRUE(fut.IsReady());
}

TEST_F(RpcServiceProxyTestFixture, AsyncStreamInvoke2) {
  auto client_context = GetClientContext();
  auto stream = dynamic_pointer_cast<testing::MockStreamReaderWriterProvider>(
      mock_rpc_service_proxy_->SelectStreamProvider(client_context));
  EXPECT_CALL(*stream, AsyncStart()).WillOnce(::testing::Return(::testing::ByMove(MakeReadyFuture<>())));
  EXPECT_CALL(*stream, AsyncWrite(::testing::_)).WillOnce(::testing::Return(::testing::ByMove(MakeReadyFuture<>())));

  auto fut = mock_rpc_service_proxy_->AsyncStreamInvoke<test::helloworld::HelloRequest, test::helloworld::HelloReply>(
      client_context, test::helloworld::HelloRequest{});
  fut = future::BlockingGet(std::move(fut));
  EXPECT_TRUE(fut.IsReady());
}

TEST_F(RpcServiceProxyTestFixture, SetMessageEncodeType) {
  EXPECT_CALL(*codec_, FillRequest(::testing::_, ::testing::_, ::testing::_)).WillRepeatedly(::testing::Return(false));

  // Transparent
  {
    auto context = MakeClientContext(mock_rpc_service_proxy_);
    context->SetTransparent(true);
    NoncontiguousBuffer req, rsp;
    mock_rpc_service_proxy_->UnaryInvoke(context, req, &rsp);
    EXPECT_EQ(context->GetReqEncodeType(), 0);
    EXPECT_EQ(context->GetReqEncodeDataType(), 0);
  }

  // pb
  {
    auto context = MakeClientContext(mock_rpc_service_proxy_);
    test::helloworld::HelloRequest req;
    test::helloworld::HelloReply rsp;
    mock_rpc_service_proxy_->UnaryInvoke(context, req, &rsp);
    EXPECT_EQ(context->GetReqEncodeType(), serialization::kPbType);
    EXPECT_EQ(context->GetReqEncodeDataType(), serialization::kPbMessage);
  }

  // flat buffer
  {
    auto context = MakeClientContext(mock_rpc_service_proxy_);
    flatbuffers::trpc::Message<trpc::test::helloworld::FbRequest> req;
    flatbuffers::trpc::Message<trpc::test::helloworld::FbReply> rsp;
    mock_rpc_service_proxy_->UnaryInvoke(context, req, &rsp);
    EXPECT_EQ(context->GetReqEncodeType(), serialization::kFlatBuffersType);
    EXPECT_EQ(context->GetReqEncodeDataType(), serialization::kFlatBuffers);
  }

  // json
  {
    auto context = MakeClientContext(mock_rpc_service_proxy_);
    rapidjson::Document req, rsp;
    mock_rpc_service_proxy_->UnaryInvoke(context, req, &rsp);
    EXPECT_EQ(context->GetReqEncodeType(), serialization::kJsonType);
    EXPECT_EQ(context->GetReqEncodeDataType(), serialization::kRapidJson);
  }

  // string
  {
    auto context = MakeClientContext(mock_rpc_service_proxy_);
    std::string req, rsp;
    mock_rpc_service_proxy_->UnaryInvoke(context, req, &rsp);
    EXPECT_EQ(context->GetReqEncodeType(), serialization::kNoopType);
    EXPECT_EQ(context->GetReqEncodeDataType(), serialization::kStringNoop);
  }

  // NonContiguousBuffer
  {
    auto context = MakeClientContext(mock_rpc_service_proxy_);
    NoncontiguousBuffer req;
    NoncontiguousBuffer rsp;
    mock_rpc_service_proxy_->UnaryInvoke(context, req, &rsp);
    EXPECT_EQ(context->GetReqEncodeType(), serialization::kNoopType);
    EXPECT_EQ(context->GetReqEncodeDataType(), serialization::kNonContiguousBufferNoop);
  }

  // no supported type
  {
    auto context = MakeClientContext(mock_rpc_service_proxy_);
    int req, rsp;
    mock_rpc_service_proxy_->UnaryInvoke(context, req, &rsp);
    EXPECT_FALSE(context->GetStatus().OK());
    EXPECT_EQ(context->GetStatus().GetFrameworkRetCode(), TrpcRetCode::TRPC_CLIENT_ENCODE_ERR);
  }
}

// BroadcastUnaryInvoke ok
TEST_F(RpcServiceProxyTestFixture, BroadcastUnaryInvokeOk) {
  // 下游广播节点有5个，Mock 5次RPC
  EXPECT_CALL(*codec_, FillRequest(::testing::_, ::testing::_, ::testing::_))
      .Times(5)
      .WillRepeatedly(::testing::Return(true));
  EXPECT_CALL(*codec_, FillResponse(::testing::_, ::testing::_, ::testing::_))
      .Times(5)
      .WillRepeatedly(::testing::Return(true));

  // 这里是普通线程模型，所以广播同步会使用异步Future访问，所以mock AsyncUnaryTransportInvoke
  EXPECT_CALL(*mock_broadcast_rpc_service_proxy_, AsyncUnaryTransportInvoke(::testing::_, ::testing::_))
      .Times(5)
      .WillOnce(::testing::Return(::testing::ByMove(MakeReadyFuture<ProtocolPtr>(codec_->CreateResponsePtr()))))
      .WillOnce(::testing::Return(::testing::ByMove(MakeReadyFuture<ProtocolPtr>(codec_->CreateResponsePtr()))))
      .WillOnce(::testing::Return(::testing::ByMove(MakeReadyFuture<ProtocolPtr>(codec_->CreateResponsePtr()))))
      .WillOnce(::testing::Return(::testing::ByMove(MakeReadyFuture<ProtocolPtr>(codec_->CreateResponsePtr()))))
      .WillOnce(::testing::Return(::testing::ByMove(MakeReadyFuture<ProtocolPtr>(codec_->CreateResponsePtr()))));

  auto client_context = GetClientContext(mock_broadcast_rpc_service_proxy_);
  test::helloworld::HelloRequest hello_req;
  std::vector<std::tuple<Status, test::helloworld::HelloReply>> rsp;
  Status broadcast_status = mock_broadcast_rpc_service_proxy_
                                ->BroadcastUnaryInvoke<test::helloworld::HelloRequest, test::helloworld::HelloReply>(
                                    client_context, hello_req, &rsp);

  std::cout << "BroadcastUnaryInvokeOk status:" << broadcast_status.ToString() << std::endl;
  EXPECT_TRUE(broadcast_status.OK());
  EXPECT_TRUE(rsp.size() == 5);
  for (auto& itor_resp : rsp) {
    Status rpc_status = std::get<0>(itor_resp);
    EXPECT_TRUE(rpc_status.OK());
  }
}

// BroadcastUnaryInvoke Failed
TEST_F(RpcServiceProxyTestFixture, BroadcastUnaryInvokeFailed) {
  // 下游广播节点有5个，Mock 5次RPC
  EXPECT_CALL(*codec_, FillRequest(::testing::_, ::testing::_, ::testing::_))
      .Times(5)
      .WillRepeatedly(::testing::Return(true));
  EXPECT_CALL(*codec_, FillResponse(::testing::_, ::testing::_, ::testing::_))
      .Times(3)
      .WillRepeatedly(::testing::Return(true));

  // 这里是普通线程模型，所以广播同步会使用异步Future访问，所以mock AsyncUnaryTransportInvoke
  // 模拟有成功，有失败的
  EXPECT_CALL(*mock_broadcast_rpc_service_proxy_, AsyncUnaryTransportInvoke(::testing::_, ::testing::_))
      .Times(5)
      .WillOnce(::testing::Return(::testing::ByMove(MakeReadyFuture<ProtocolPtr>(codec_->CreateResponsePtr()))))
      .WillOnce(::testing::Return(::testing::ByMove(MakeReadyFuture<ProtocolPtr>(codec_->CreateResponsePtr()))))
      .WillOnce(::testing::Return(::testing::ByMove(MakeExceptionFuture<ProtocolPtr>(
          CommonException("client timeout", TrpcRetCode::TRPC_CLIENT_INVOKE_TIMEOUT_ERR)))))
      .WillOnce(::testing::Return(::testing::ByMove(MakeReadyFuture<ProtocolPtr>(codec_->CreateResponsePtr()))))
      .WillOnce(::testing::Return(::testing::ByMove(MakeExceptionFuture<ProtocolPtr>(
          CommonException("client timeout", TrpcRetCode::TRPC_CLIENT_INVOKE_TIMEOUT_ERR)))));

  auto client_context = GetClientContext(mock_broadcast_rpc_service_proxy_);
  test::helloworld::HelloRequest hello_req;
  std::vector<std::tuple<Status, test::helloworld::HelloReply>> rsp;
  Status broadcast_status = mock_broadcast_rpc_service_proxy_
                                ->BroadcastUnaryInvoke<test::helloworld::HelloRequest, test::helloworld::HelloReply>(
                                    client_context, hello_req, &rsp);

  std::cout << "BroadcastUnaryInvokeFailed status:" << broadcast_status.ToString() << std::endl;
  EXPECT_TRUE(!broadcast_status.OK());
  EXPECT_TRUE(rsp.size() == 5);
  int rpc_success_count = 0;
  int rpc_failed_count = 0;
  for (auto& itor_resp : rsp) {
    Status rpc_status = std::get<0>(itor_resp);
    if (rpc_status.OK()) {
      rpc_success_count++;
    } else {
      rpc_failed_count++;
    }
  }

  EXPECT_TRUE(rpc_success_count == 3);
  EXPECT_TRUE(rpc_failed_count == 2);
}

// BroadcastOnewayInvoke ok
TEST_F(RpcServiceProxyTestFixture, BroadcastOnewayInvokeOk) {
  // 下游广播节点有5个，Mock 5次RPC
  EXPECT_CALL(*codec_, FillRequest(::testing::_, ::testing::_, ::testing::_))
      .Times(5)
      .WillRepeatedly(::testing::Return(true));

  // 这里是普通线程模型，所以广播同步会使用OnewayInvoke访问，所以mock OnewayTransportInvoke
  EXPECT_CALL(*mock_broadcast_rpc_service_proxy_, OnewayTransportInvoke(::testing::_, ::testing::_))
      .Times(5)
      .WillRepeatedly(::testing::Return());

  auto client_context = GetClientContext(mock_broadcast_rpc_service_proxy_);
  test::helloworld::HelloRequest hello_req;
  Status broadcast_status = mock_broadcast_rpc_service_proxy_->BroadcastOnewayInvoke<test::helloworld::HelloRequest>(
      client_context, hello_req);

  std::cout << "BroadcastOnewayInvokeOk status:" << broadcast_status.ToString() << std::endl;
  EXPECT_TRUE(broadcast_status.OK());
}

// AsyncBroadcastUnaryInvoke ok
TEST_F(RpcServiceProxyTestFixture, AsyncBroadcastUnaryInvokeOk) {
  // 下游广播节点有5个，Mock 5次RPC
  EXPECT_CALL(*codec_, FillRequest(::testing::_, ::testing::_, ::testing::_))
      .Times(5)
      .WillRepeatedly(::testing::Return(true));
  EXPECT_CALL(*codec_, FillResponse(::testing::_, ::testing::_, ::testing::_))
      .Times(5)
      .WillRepeatedly(::testing::Return(true));

  // 这里是普通线程模型，所以广播同步会使用异步访问，所以mock AsyncUnaryTransportInvoke
  EXPECT_CALL(*mock_broadcast_rpc_service_proxy_, AsyncUnaryTransportInvoke(::testing::_, ::testing::_))
      .Times(5)
      .WillOnce(::testing::Return(::testing::ByMove(MakeReadyFuture<ProtocolPtr>(codec_->CreateResponsePtr()))))
      .WillOnce(::testing::Return(::testing::ByMove(MakeReadyFuture<ProtocolPtr>(codec_->CreateResponsePtr()))))
      .WillOnce(::testing::Return(::testing::ByMove(MakeReadyFuture<ProtocolPtr>(codec_->CreateResponsePtr()))))
      .WillOnce(::testing::Return(::testing::ByMove(MakeReadyFuture<ProtocolPtr>(codec_->CreateResponsePtr()))))
      .WillOnce(::testing::Return(::testing::ByMove(MakeReadyFuture<ProtocolPtr>(codec_->CreateResponsePtr()))));

  auto broadcast_context = GetClientContext(mock_broadcast_rpc_service_proxy_);
  test::helloworld::HelloRequest hello_req;
  auto fut =
      mock_broadcast_rpc_service_proxy_
          ->AsyncBroadcastUnaryInvoke<test::helloworld::HelloRequest, test::helloworld::HelloReply>(broadcast_context,
                                                                                                    hello_req)
          .Then(
              [](::trpc::Future<::trpc::Status,
                                std::vector<std::tuple<::trpc::Status, ::trpc::test::helloworld::HelloReply>>>&& fut) {
                // 这里应该Future 都是Ready，错误信息判断是通过Future.GetValue0()返回的trpc::Status来判断
                EXPECT_TRUE(fut.IsReady());

                auto result = fut.GetValue();
                trpc::Status broadcast_status = std::get<0>(result);
                auto response = std::get<1>(result);
                std::cout << "AsyncBroadcastUnaryInvokeOk status:" << broadcast_status.ToString() << std::endl;
                EXPECT_TRUE(broadcast_status.OK());
                EXPECT_TRUE(response.size() == 5);
                for (auto& itor_resp : response) {
                  Status rpc_status = std::get<0>(itor_resp);
                  EXPECT_TRUE(rpc_status.OK());
                }

                return ::trpc::MakeReadyFuture<>();
              });

  future::BlockingGet(std::move(fut));
}

// AsyncBroadcastUnaryInvoke Failed
TEST_F(RpcServiceProxyTestFixture, AsyncBroadcastUnaryInvokeFailed) {
  // 下游广播节点有5个，Mock 5次RPC
  EXPECT_CALL(*codec_, FillRequest(::testing::_, ::testing::_, ::testing::_))
      .Times(5)
      .WillRepeatedly(::testing::Return(true));
  EXPECT_CALL(*codec_, FillResponse(::testing::_, ::testing::_, ::testing::_))
      .Times(3)
      .WillRepeatedly(::testing::Return(true));

  // 这里是普通线程模型，所以广播同步会使用异步Future访问，所以mock AsyncUnaryTransportInvoke
  // 模拟有成功，有失败的
  EXPECT_CALL(*mock_broadcast_rpc_service_proxy_, AsyncUnaryTransportInvoke(::testing::_, ::testing::_))
      .Times(5)
      .WillOnce(::testing::Return(::testing::ByMove(MakeReadyFuture<ProtocolPtr>(codec_->CreateResponsePtr()))))
      .WillOnce(::testing::Return(::testing::ByMove(MakeReadyFuture<ProtocolPtr>(codec_->CreateResponsePtr()))))
      .WillOnce(::testing::Return(::testing::ByMove(MakeExceptionFuture<ProtocolPtr>(
          CommonException("client timeout", TrpcRetCode::TRPC_CLIENT_INVOKE_TIMEOUT_ERR)))))
      .WillOnce(::testing::Return(::testing::ByMove(MakeReadyFuture<ProtocolPtr>(codec_->CreateResponsePtr()))))
      .WillOnce(::testing::Return(::testing::ByMove(MakeExceptionFuture<ProtocolPtr>(
          CommonException("client timeout", TrpcRetCode::TRPC_CLIENT_INVOKE_TIMEOUT_ERR)))));

  auto broadcast_context = GetClientContext(mock_broadcast_rpc_service_proxy_);
  test::helloworld::HelloRequest hello_req;
  auto fut =
      mock_broadcast_rpc_service_proxy_
          ->AsyncBroadcastUnaryInvoke<test::helloworld::HelloRequest, test::helloworld::HelloReply>(broadcast_context,
                                                                                                    hello_req)
          .Then(
              [](::trpc::Future<::trpc::Status,
                                std::vector<std::tuple<::trpc::Status, ::trpc::test::helloworld::HelloReply>>>&& fut) {
                // 这里应该Future 都是Ready，错误信息判断是通过Future.GetValue0()返回的trpc::Status来判断
                EXPECT_TRUE(fut.IsReady());

                auto result = fut.GetValue();
                trpc::Status broadcast_status = std::get<0>(result);
                auto response = std::get<1>(result);
                std::cout << "AsyncBroadcastUnaryInvokeFailed status:" << broadcast_status.ToString() << std::endl;
                EXPECT_TRUE(!broadcast_status.OK());
                EXPECT_TRUE(response.size() == 5);
                int rpc_success_count = 0;
                int rpc_failed_count = 0;
                for (auto& itor_resp : response) {
                  Status rpc_status = std::get<0>(itor_resp);
                  if (rpc_status.OK()) {
                    rpc_success_count++;
                  } else {
                    rpc_failed_count++;
                  }
                }

                EXPECT_TRUE(rpc_success_count == 3);
                EXPECT_TRUE(rpc_failed_count == 2);

                return ::trpc::MakeReadyFuture<>();
              });

  future::BlockingGet(std::move(fut));
}

class RpcServiceProxyTestFiberFixture : public ::testing::Test {
 public:
  static void SetUpTestCase() {
    int ret = TrpcConfig::GetInstance()->Init("./trpc/client/testing/trpc_cpp_client_fiber.yaml");
    EXPECT_TRUE(ret == 0);

    codec_ = std::make_shared<MockTrpcClientCodec>();
    ClientCodecFactory::GetInstance()->Register(codec_);

    // Register direct selector and filter before InitFrameworkRuntime
    // to make SetEndpointInfo work properly
    auto direct_selector = MakeRefCounted<SelectorDirect>(MakeRefCounted<PollingLoadBalance>());
    direct_selector->Init();
    SelectorFactory::GetInstance()->Register(direct_selector);
    auto direct_selector_filter = std::make_shared<DirectSelectorFilter>();
    direct_selector_filter->Init();
    FilterManager::GetInstance()->AddMessageClientFilter(direct_selector_filter);

    trpc::InitFrameworkRuntime();

    detail::SetDefaultOption(broadcast_option_);
    broadcast_option_->name = "trpc.test.helloworld.FiberBroadcastGreeter";
    broadcast_option_->caller_name = "Test.BroadcastHelloWorldClient";
    broadcast_option_->codec_name = "mock_codec";
    broadcast_option_->conn_type = "long";
    broadcast_option_->network = "tcp";
    broadcast_option_->timeout = 1000;
    broadcast_option_->target = "127.0.0.1:10001,127.0.0.1:10002,127.0.0.1:10003,127.0.0.1:10004,127.0.0.1:10005";
    broadcast_option_->selector_name = "direct";
  }

  static void TearDownTestCase() { 
    trpc::DestroyFrameworkRuntime(); 
    FilterManager::GetInstance()->Clear();
    SelectorFactory::GetInstance()->Clear();
  }

 protected:
  void SetUp() override {
    mock_broadcast_rpc_service_proxy_ = std::make_shared<MockRpcServiceProxy>();
    mock_broadcast_rpc_service_proxy_->SetMockServiceProxyOption(broadcast_option_);
    mock_broadcast_rpc_service_proxy_->SetMockEndpointInfo(broadcast_option_->target);
  }

  void TearDown() override {
    mock_broadcast_rpc_service_proxy_->Stop();
    mock_broadcast_rpc_service_proxy_->Destroy();
  }

 protected:
  static std::shared_ptr<MockTrpcClientCodec> codec_;
  static std::shared_ptr<ServiceProxyOption> broadcast_option_;
  MockRpcServiceProxyPtr mock_broadcast_rpc_service_proxy_{nullptr};
};

std::shared_ptr<MockTrpcClientCodec> RpcServiceProxyTestFiberFixture::codec_ = nullptr;

std::shared_ptr<ServiceProxyOption> RpcServiceProxyTestFiberFixture::broadcast_option_ =
    std::make_shared<ServiceProxyOption>();

// BroadcastUnaryInvoke ok
TEST_F(RpcServiceProxyTestFiberFixture, BroadcastUnaryInvokeOk) {
  // 下游广播节点有5个，Mock 5次RPC
  EXPECT_CALL(*codec_, FillRequest(::testing::_, ::testing::_, ::testing::_))
      .Times(5)
      .WillRepeatedly(::testing::Return(true));
  EXPECT_CALL(*codec_, FillResponse(::testing::_, ::testing::_, ::testing::_))
      .Times(5)
      .WillRepeatedly(::testing::Return(true));

  // 这里是Fiber线程模型，所以广播同步会使用Fiber访问，所以mock UnaryTransportInvoke
  EXPECT_CALL(*mock_broadcast_rpc_service_proxy_, UnaryTransportInvoke(::testing::_, ::testing::_, ::testing::_))
      .Times(5)
      .WillOnce(::testing::SetArgReferee<2>(codec_->CreateResponsePtr()))
      .WillOnce(::testing::SetArgReferee<2>(codec_->CreateResponsePtr()))
      .WillOnce(::testing::SetArgReferee<2>(codec_->CreateResponsePtr()))
      .WillOnce(::testing::SetArgReferee<2>(codec_->CreateResponsePtr()))
      .WillOnce(::testing::SetArgReferee<2>(codec_->CreateResponsePtr()));

  auto broadcast_context = MakeClientContext(mock_broadcast_rpc_service_proxy_);
  test::helloworld::HelloRequest hello_req;
  std::vector<std::tuple<Status, test::helloworld::HelloReply>> rsp;
  Status broadcast_status = mock_broadcast_rpc_service_proxy_
                                ->BroadcastUnaryInvoke<test::helloworld::HelloRequest, test::helloworld::HelloReply>(
                                    broadcast_context, hello_req, &rsp);

  std::cout << "BroadcastUnaryInvokeOk status:" << broadcast_status.ToString() << std::endl;
  EXPECT_TRUE(broadcast_status.OK());
  EXPECT_TRUE(rsp.size() == 5);
  for (auto& itor_resp : rsp) {
    Status rpc_status = std::get<0>(itor_resp);
    EXPECT_TRUE(rpc_status.OK());
  }
}

// BroadcastUnaryInvoke Failed
TEST_F(RpcServiceProxyTestFiberFixture, BroadcastUnaryInvokeFailed) {
  // 下游广播节点有5个，Mock 5次RPC
  EXPECT_CALL(*codec_, FillRequest(::testing::_, ::testing::_, ::testing::_))
      .Times(5)
      .WillRepeatedly(::testing::Return(true));
  EXPECT_CALL(*codec_, FillResponse(::testing::_, ::testing::_, ::testing::_))
      .Times(5)
      .WillOnce(::testing::Return(true))
      .WillOnce(::testing::Return(false))
      .WillOnce(::testing::Return(true))
      .WillOnce(::testing::Return(true))
      .WillOnce(::testing::Return(false));

  // 这里是Fiber线程模型，所以广播同步会使用Fiber访问，所以mock UnaryTransportInvoke
  // 模拟有成功，有失败的
  EXPECT_CALL(*mock_broadcast_rpc_service_proxy_, UnaryTransportInvoke(::testing::_, ::testing::_, ::testing::_))
      .Times(5)
      .WillOnce(::testing::SetArgReferee<2>(codec_->CreateResponsePtr()))
      .WillOnce(::testing::SetArgReferee<2>(codec_->CreateResponsePtr()))
      .WillOnce(::testing::SetArgReferee<2>(codec_->CreateResponsePtr()))
      .WillOnce(::testing::SetArgReferee<2>(codec_->CreateResponsePtr()))
      .WillOnce(::testing::SetArgReferee<2>(codec_->CreateResponsePtr()));

  auto broadcast_context = MakeClientContext(mock_broadcast_rpc_service_proxy_);
  test::helloworld::HelloRequest hello_req;
  std::vector<std::tuple<Status, test::helloworld::HelloReply>> rsp;
  Status broadcast_status = mock_broadcast_rpc_service_proxy_
                                ->BroadcastUnaryInvoke<test::helloworld::HelloRequest, test::helloworld::HelloReply>(
                                    broadcast_context, hello_req, &rsp);

  std::cout << "BroadcastUnaryInvokeFailed status:" << broadcast_status.ToString() << std::endl;
  EXPECT_TRUE(!broadcast_status.OK());
  EXPECT_TRUE(rsp.size() == 5);
  int rpc_success_count = 0;
  int rpc_failed_count = 0;
  for (auto& itor_resp : rsp) {
    Status rpc_status = std::get<0>(itor_resp);
    if (rpc_status.OK()) {
      rpc_success_count++;
    } else {
      rpc_failed_count++;
    }
  }

  EXPECT_TRUE(rpc_success_count == 3);
  EXPECT_TRUE(rpc_failed_count == 2);
}

// BroadcastOnewayInvoke ok
TEST_F(RpcServiceProxyTestFiberFixture, BroadcastOnewayInvokeOk) {
  // 下游广播节点有5个，Mock 5次RPC
  EXPECT_CALL(*codec_, FillRequest(::testing::_, ::testing::_, ::testing::_))
      .Times(5)
      .WillRepeatedly(::testing::Return(true));

  // 这里是Fiber线程模型，所以广播同步会使用OnewayInvoke访问，所以mock OnewayTransportInvoke
  EXPECT_CALL(*mock_broadcast_rpc_service_proxy_, OnewayTransportInvoke(::testing::_, ::testing::_))
      .Times(5)
      .WillRepeatedly(::testing::Return());

  auto broadcast_context = MakeClientContext(mock_broadcast_rpc_service_proxy_);
  test::helloworld::HelloRequest hello_req;
  Status broadcast_status = mock_broadcast_rpc_service_proxy_->BroadcastOnewayInvoke<test::helloworld::HelloRequest>(
      broadcast_context, hello_req);

  std::cout << "BroadcastOnewayInvokeOk status:" << broadcast_status.ToString() << std::endl;
  EXPECT_TRUE(broadcast_status.OK());
}

// AsyncBroadcastUnaryInvoke ok
TEST_F(RpcServiceProxyTestFiberFixture, AsyncBroadcastUnaryInvokeOk) {
  // 下游广播节点有5个，Mock 5次RPC
  EXPECT_CALL(*codec_, FillRequest(::testing::_, ::testing::_, ::testing::_))
      .Times(5)
      .WillRepeatedly(::testing::Return(true));
  EXPECT_CALL(*codec_, FillResponse(::testing::_, ::testing::_, ::testing::_))
      .Times(5)
      .WillRepeatedly(::testing::Return(true));

  // 这里是Fiber线程模型，所以广播异步会使用异步访问，所以mock AsyncUnaryTransportInvoke
  EXPECT_CALL(*mock_broadcast_rpc_service_proxy_, AsyncUnaryTransportInvoke(::testing::_, ::testing::_))
      .Times(5)
      .WillOnce(::testing::Return(::testing::ByMove(MakeReadyFuture<ProtocolPtr>(codec_->CreateResponsePtr()))))
      .WillOnce(::testing::Return(::testing::ByMove(MakeReadyFuture<ProtocolPtr>(codec_->CreateResponsePtr()))))
      .WillOnce(::testing::Return(::testing::ByMove(MakeReadyFuture<ProtocolPtr>(codec_->CreateResponsePtr()))))
      .WillOnce(::testing::Return(::testing::ByMove(MakeReadyFuture<ProtocolPtr>(codec_->CreateResponsePtr()))))
      .WillOnce(::testing::Return(::testing::ByMove(MakeReadyFuture<ProtocolPtr>(codec_->CreateResponsePtr()))));

  auto broadcast_context = MakeClientContext(mock_broadcast_rpc_service_proxy_);
  test::helloworld::HelloRequest hello_req;
  auto fut =
      mock_broadcast_rpc_service_proxy_
          ->AsyncBroadcastUnaryInvoke<test::helloworld::HelloRequest, test::helloworld::HelloReply>(broadcast_context,
                                                                                                    hello_req)
          .Then(
              [](::trpc::Future<::trpc::Status,
                                std::vector<std::tuple<::trpc::Status, ::trpc::test::helloworld::HelloReply>>>&& fut) {
                // 这里应该Future 都是Ready，错误信息判断是通过Future.GetValue0()返回的trpc::Status来判断
                EXPECT_TRUE(fut.IsReady());

                auto result = fut.GetValue();
                trpc::Status broadcast_status = std::get<0>(result);
                auto response = std::get<1>(result);
                std::cout << "AsyncBroadcastUnaryInvokeOk status:" << broadcast_status.ToString() << std::endl;

                EXPECT_TRUE(broadcast_status.OK());
                EXPECT_TRUE(response.size() == 5);
                for (auto& itor_resp : response) {
                  Status rpc_status = std::get<0>(itor_resp);
                  EXPECT_TRUE(rpc_status.OK());
                }

                return ::trpc::MakeReadyFuture<>();
              });

  future::BlockingGet(std::move(fut));
}

// BroadcastUnaryInvoke Failed
TEST_F(RpcServiceProxyTestFiberFixture, AsyncBroadcastUnaryInvokeFailed) {
  // 下游广播节点有5个，Mock 5次RPC
  EXPECT_CALL(*codec_, FillRequest(::testing::_, ::testing::_, ::testing::_))
      .Times(5)
      .WillRepeatedly(::testing::Return(true));

  EXPECT_CALL(*codec_, FillResponse(::testing::_, ::testing::_, ::testing::_))
      .Times(3)
      .WillRepeatedly(::testing::Return(true));

  // 这里是Fiber线程模型，所以广播同步会使用异步Future访问，所以mock AsyncUnaryTransportInvoke
  // 模拟有成功，有失败的
  EXPECT_CALL(*mock_broadcast_rpc_service_proxy_, AsyncUnaryTransportInvoke(::testing::_, ::testing::_))
      .Times(5)
      .WillOnce(::testing::Return(::testing::ByMove(MakeReadyFuture<ProtocolPtr>(codec_->CreateResponsePtr()))))
      .WillOnce(::testing::Return(::testing::ByMove(MakeReadyFuture<ProtocolPtr>(codec_->CreateResponsePtr()))))
      .WillOnce(::testing::Return(::testing::ByMove(MakeExceptionFuture<ProtocolPtr>(
          CommonException("client timeout", TrpcRetCode::TRPC_CLIENT_INVOKE_TIMEOUT_ERR)))))
      .WillOnce(::testing::Return(::testing::ByMove(MakeReadyFuture<ProtocolPtr>(codec_->CreateResponsePtr()))))
      .WillOnce(::testing::Return(::testing::ByMove(MakeExceptionFuture<ProtocolPtr>(
          CommonException("client timeout", TrpcRetCode::TRPC_CLIENT_INVOKE_TIMEOUT_ERR)))));

  auto broadcast_context = MakeClientContext(mock_broadcast_rpc_service_proxy_);
  test::helloworld::HelloRequest hello_req;
  auto fut =
      mock_broadcast_rpc_service_proxy_
          ->AsyncBroadcastUnaryInvoke<test::helloworld::HelloRequest, test::helloworld::HelloReply>(broadcast_context,
                                                                                                    hello_req)
          .Then(
              [](::trpc::Future<::trpc::Status,
                                std::vector<std::tuple<::trpc::Status, ::trpc::test::helloworld::HelloReply>>>&& fut) {
                // 这里应该Future 都是Ready，错误信息判断是通过Future.GetValue0()返回的trpc::Status来判断
                EXPECT_TRUE(fut.IsReady());

                auto result = fut.GetValue();
                trpc::Status broadcast_status = std::get<0>(result);
                auto response = std::get<1>(result);
                std::cout << "AsyncBroadcastUnaryInvokeFailed status:" << broadcast_status.ToString() << std::endl;
                EXPECT_TRUE(!broadcast_status.OK());
                EXPECT_TRUE(response.size() == 5);
                int rpc_success_count = 0;
                int rpc_failed_count = 0;
                for (auto& itor_resp : response) {
                  Status rpc_status = std::get<0>(itor_resp);
                  if (rpc_status.OK()) {
                    rpc_success_count++;
                  } else {
                    rpc_failed_count++;
                  }
                }

                EXPECT_TRUE(rpc_success_count == 3);
                EXPECT_TRUE(rpc_failed_count == 2);

                return ::trpc::MakeReadyFuture<>();
              });

  future::BlockingGet(std::move(fut));
}

}  // namespace trpc::testing
