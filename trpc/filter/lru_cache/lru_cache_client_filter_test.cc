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

#include "trpc/filter/lru_cache/lru_cache_client_filter.h"

#include <memory>
#include <string>
#include <thread>

#include "gtest/gtest.h"

#include "trpc/client/client_context.h"
#include "trpc/codec/trpc/trpc_client_codec.h"

namespace trpc::testing {

class LruCacheClientFilterTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create codec
    codec_ = std::make_shared<TrpcClientCodec>();

    // Create default filter
    LruCacheClientFilterConfig config;
    config.max_capacity = 100;
    config.default_ttl_ms = 1000;  // 1 second for testing
    filter_ = std::make_shared<LruCacheClientFilter>(config);
  }

  void TearDown() override {
    filter_->ClearCache();
  }

  ClientContextPtr CreateTestContext(const std::string& func_name) {
    auto context = MakeRefCounted<ClientContext>(codec_);
    context->SetCalleeName("trpc.test.helloworld.Greeter");
    context->SetFuncName(func_name);

    // Set request body with some test data
    ProtocolPtr req_msg = context->GetRequest();
    NoncontiguousBufferBuilder builder;
    builder.Append("test_request_data_" + func_name);
    req_msg->SetNonContiguousProtocolBody(builder.DestructiveGet());

    // Create response message
    context->SetResponse(codec_->CreateResponsePtr());

    return context;
  }

  void SetResponseData(const ClientContextPtr& context, const std::string& data) {
    ProtocolPtr rsp_msg = context->GetResponse();
    NoncontiguousBufferBuilder builder;
    builder.Append(data);
    rsp_msg->SetNonContiguousProtocolBody(builder.DestructiveGet());
    context->SetStatus(Status(0, "OK"));
  }

 protected:
  ClientCodecPtr codec_;
  std::shared_ptr<LruCacheClientFilter> filter_;
};

// Test basic cache hit/miss workflow
TEST_F(LruCacheClientFilterTest, BasicCacheHitMiss) {
  auto context = CreateTestContext("/SayHello");

  // First call - cache miss
  FilterStatus status = FilterStatus::CONTINUE;
  (*filter_)(status, FilterPoint::CLIENT_PRE_RPC_INVOKE, context);
  EXPECT_EQ(status, FilterStatus::CONTINUE);  // No cache, continue to backend

  // Simulate successful RPC response
  SetResponseData(context, "Hello, World!");

  // Store response in cache
  (*filter_)(status, FilterPoint::CLIENT_POST_RPC_INVOKE, context);

  // Second call with same request - cache hit
  auto context2 = CreateTestContext("/SayHello");
  status = FilterStatus::CONTINUE;
  (*filter_)(status, FilterPoint::CLIENT_PRE_RPC_INVOKE, context2);

  // Should get cached response
  EXPECT_EQ(status, FilterStatus::REJECT);  // Cache hit, no need to call backend
  EXPECT_TRUE(context2->GetStatus().OK());
  EXPECT_EQ(context2->GetStatus().ErrorMessage(), "cache_hit");

  // Verify response data
  const ProtocolPtr& rsp = context2->GetResponse();
  EXPECT_GT(rsp->GetNonContiguousProtocolBody().ByteSize(), 0);
}

// Test cache with different methods
TEST_F(LruCacheClientFilterTest, DifferentMethods) {
  // First method
  auto context1 = CreateTestContext("/SayHello");
  FilterStatus status = FilterStatus::CONTINUE;
  (*filter_)(status, FilterPoint::CLIENT_PRE_RPC_INVOKE, context1);
  SetResponseData(context1, "Hello");
  (*filter_)(status, FilterPoint::CLIENT_POST_RPC_INVOKE, context1);

  // Second method
  auto context2 = CreateTestContext("/SayGoodbye");
  status = FilterStatus::CONTINUE;
  (*filter_)(status, FilterPoint::CLIENT_PRE_RPC_INVOKE, context2);
  SetResponseData(context2, "Goodbye");
  (*filter_)(status, FilterPoint::CLIENT_POST_RPC_INVOKE, context2);

  // Verify both are cached separately
  auto context3 = CreateTestContext("/SayHello");
  status = FilterStatus::CONTINUE;
  (*filter_)(status, FilterPoint::CLIENT_PRE_RPC_INVOKE, context3);
  EXPECT_EQ(status, FilterStatus::REJECT);  // Cache hit

  auto context4 = CreateTestContext("/SayGoodbye");
  status = FilterStatus::CONTINUE;
  (*filter_)(status, FilterPoint::CLIENT_PRE_RPC_INVOKE, context4);
  EXPECT_EQ(status, FilterStatus::REJECT);  // Cache hit
}

// Test cache with different request data
TEST_F(LruCacheClientFilterTest, DifferentRequestData) {
  // Same method but different request data
  auto context1 = CreateTestContext("/SayHello");
  FilterStatus status = FilterStatus::CONTINUE;
  (*filter_)(status, FilterPoint::CLIENT_PRE_RPC_INVOKE, context1);
  SetResponseData(context1, "Response1");
  (*filter_)(status, FilterPoint::CLIENT_POST_RPC_INVOKE, context1);

  // Create context with different request data
  auto context2 = MakeRefCounted<ClientContext>(codec_);
  context2->SetCalleeName("trpc.test.helloworld.Greeter");
  context2->SetFuncName("/SayHello");
  ProtocolPtr req_msg = context2->GetRequest();
  NoncontiguousBufferBuilder builder;
  builder.Append("different_request_data");
  req_msg->SetNonContiguousProtocolBody(builder.DestructiveGet());

  // Should be cache miss (different request data = different cache key)
  status = FilterStatus::CONTINUE;
  (*filter_)(status, FilterPoint::CLIENT_PRE_RPC_INVOKE, context2);
  EXPECT_EQ(status, FilterStatus::CONTINUE);  // Cache miss
}

// Test TTL expiration
TEST_F(LruCacheClientFilterTest, TTLExpiration) {
  // Create filter with very short TTL
  LruCacheClientFilterConfig config;
  config.default_ttl_ms = 100;  // 100ms
  auto short_ttl_filter = std::make_shared<LruCacheClientFilter>(config);

  auto context = CreateTestContext("/SayHello");
  FilterStatus status = FilterStatus::CONTINUE;
  (*short_ttl_filter)(status, FilterPoint::CLIENT_PRE_RPC_INVOKE, context);
  SetResponseData(context, "Hello");
  (*short_ttl_filter)(status, FilterPoint::CLIENT_POST_RPC_INVOKE, context);

  // Immediate second call - should hit cache
  auto context2 = CreateTestContext("/SayHello");
  status = FilterStatus::CONTINUE;
  (*short_ttl_filter)(status, FilterPoint::CLIENT_PRE_RPC_INVOKE, context2);
  EXPECT_EQ(status, FilterStatus::REJECT);

  // Wait for TTL to expire (wait longer to ensure expiration)
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Third call - should miss cache (expired)
  auto context3 = CreateTestContext("/SayHello");
  status = FilterStatus::CONTINUE;
  (*short_ttl_filter)(status, FilterPoint::CLIENT_PRE_RPC_INVOKE, context3);
  EXPECT_EQ(status, FilterStatus::CONTINUE);  // Cache miss due to expiration
}

// Test whitelist functionality
TEST_F(LruCacheClientFilterTest, MethodWhitelist) {
  // Create filter with whitelist
  LruCacheClientFilterConfig config;
  config.enable_whitelist = true;
  config.method_whitelist.insert("/SayHello");  // Only cache this method
  auto whitelist_filter = std::make_shared<LruCacheClientFilter>(config);

  // Method in whitelist
  auto context1 = CreateTestContext("/SayHello");
  FilterStatus status = FilterStatus::CONTINUE;
  (*whitelist_filter)(status, FilterPoint::CLIENT_PRE_RPC_INVOKE, context1);
  SetResponseData(context1, "Hello");
  (*whitelist_filter)(status, FilterPoint::CLIENT_POST_RPC_INVOKE, context1);

  // Verify cached
  auto context1_check = CreateTestContext("/SayHello");
  status = FilterStatus::CONTINUE;
  (*whitelist_filter)(status, FilterPoint::CLIENT_PRE_RPC_INVOKE, context1_check);
  EXPECT_EQ(status, FilterStatus::REJECT);  // Cache hit

  // Method NOT in whitelist
  auto context2 = CreateTestContext("/SayGoodbye");
  status = FilterStatus::CONTINUE;
  (*whitelist_filter)(status, FilterPoint::CLIENT_PRE_RPC_INVOKE, context2);
  SetResponseData(context2, "Goodbye");
  (*whitelist_filter)(status, FilterPoint::CLIENT_POST_RPC_INVOKE, context2);

  // Should not be cached
  auto context2_check = CreateTestContext("/SayGoodbye");
  status = FilterStatus::CONTINUE;
  (*whitelist_filter)(status, FilterPoint::CLIENT_PRE_RPC_INVOKE, context2_check);
  EXPECT_EQ(status, FilterStatus::CONTINUE);  // Cache miss (not in whitelist)
}

// Test failed RPC responses are not cached
TEST_F(LruCacheClientFilterTest, FailedRpcNotCached) {
  auto context = CreateTestContext("/SayHello");
  FilterStatus status = FilterStatus::CONTINUE;
  (*filter_)(status, FilterPoint::CLIENT_PRE_RPC_INVOKE, context);

  // Simulate failed RPC
  SetResponseData(context, "Error");
  context->SetStatus(Status(TrpcRetCode::TRPC_INVOKE_UNKNOWN_ERR, "RPC failed"));

  // Try to cache failed response
  (*filter_)(status, FilterPoint::CLIENT_POST_RPC_INVOKE, context);

  // Verify not cached
  auto context2 = CreateTestContext("/SayHello");
  status = FilterStatus::CONTINUE;
  (*filter_)(status, FilterPoint::CLIENT_PRE_RPC_INVOKE, context2);
  EXPECT_EQ(status, FilterStatus::CONTINUE);  // Cache miss
}

// Test statistics collection
TEST_F(LruCacheClientFilterTest, Statistics) {
  auto stats = filter_->GetStatistics();
  EXPECT_EQ(stats.total_hits, 0);
  EXPECT_EQ(stats.total_misses, 0);

  // First call - miss
  auto context1 = CreateTestContext("/SayHello");
  FilterStatus status = FilterStatus::CONTINUE;
  (*filter_)(status, FilterPoint::CLIENT_PRE_RPC_INVOKE, context1);
  SetResponseData(context1, "Hello");
  (*filter_)(status, FilterPoint::CLIENT_POST_RPC_INVOKE, context1);

  stats = filter_->GetStatistics();
  EXPECT_EQ(stats.total_misses, 1);

  // Second call - hit
  auto context2 = CreateTestContext("/SayHello");
  status = FilterStatus::CONTINUE;
  (*filter_)(status, FilterPoint::CLIENT_PRE_RPC_INVOKE, context2);

  stats = filter_->GetStatistics();
  EXPECT_EQ(stats.total_hits, 1);
  EXPECT_EQ(stats.total_misses, 1);
}

// Test cache clear
TEST_F(LruCacheClientFilterTest, CacheClear) {
  auto context = CreateTestContext("/SayHello");
  FilterStatus status = FilterStatus::CONTINUE;
  (*filter_)(status, FilterPoint::CLIENT_PRE_RPC_INVOKE, context);
  SetResponseData(context, "Hello");
  (*filter_)(status, FilterPoint::CLIENT_POST_RPC_INVOKE, context);

  // Clear cache
  filter_->ClearCache();

  // Should miss after clear
  auto context2 = CreateTestContext("/SayHello");
  status = FilterStatus::CONTINUE;
  (*filter_)(status, FilterPoint::CLIENT_PRE_RPC_INVOKE, context2);
  EXPECT_EQ(status, FilterStatus::CONTINUE);  // Cache miss
}

// Test LRU eviction behavior
TEST_F(LruCacheClientFilterTest, LruEviction) {
  // Create filter with small capacity
  LruCacheClientFilterConfig config;
  config.max_capacity = 2;
  auto small_filter = std::make_shared<LruCacheClientFilter>(config);

  // Add 3 items to trigger eviction
  for (int i = 1; i <= 3; ++i) {
    auto context = MakeRefCounted<ClientContext>(codec_);
    context->SetCalleeName("trpc.test.helloworld.Greeter");
    context->SetFuncName("/Method" + std::to_string(i));
    ProtocolPtr req_msg = context->GetRequest();
    NoncontiguousBufferBuilder builder;
    builder.Append("request_" + std::to_string(i));
    req_msg->SetNonContiguousProtocolBody(builder.DestructiveGet());

    // Create response message
    context->SetResponse(codec_->CreateResponsePtr());

    FilterStatus status = FilterStatus::CONTINUE;
    (*small_filter)(status, FilterPoint::CLIENT_PRE_RPC_INVOKE, context);
    SetResponseData(context, "response_" + std::to_string(i));
    (*small_filter)(status, FilterPoint::CLIENT_POST_RPC_INVOKE, context);
  }

  // First entry should be evicted
  auto context_check = MakeRefCounted<ClientContext>(codec_);
  context_check->SetCalleeName("trpc.test.helloworld.Greeter");
  context_check->SetFuncName("/Method1");
  ProtocolPtr req_msg = context_check->GetRequest();
  NoncontiguousBufferBuilder builder;
  builder.Append("request_1");
  req_msg->SetNonContiguousProtocolBody(builder.DestructiveGet());

  // Create response message
  context_check->SetResponse(codec_->CreateResponsePtr());

  FilterStatus status = FilterStatus::CONTINUE;
  (*small_filter)(status, FilterPoint::CLIENT_PRE_RPC_INVOKE, context_check);
  EXPECT_EQ(status, FilterStatus::CONTINUE);  // Cache miss (evicted)

  auto stats = small_filter->GetStatistics();
  EXPECT_GE(stats.total_evictions, 1);
}

}  // namespace trpc::testing
