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

#include "trpc/naming/lru_cache/lru_cache_selector.h"

#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "trpc/client/client_context.h"
#include "trpc/codec/trpc/trpc_client_codec.h"
#include "trpc/naming/common/common_defs.h"

namespace trpc::testing {

// Mock Selector for testing
class MockSelector : public Selector {
 public:
  MOCK_CONST_METHOD0(Name, std::string());
  MOCK_CONST_METHOD0(Version, std::string());
  MOCK_METHOD2(Select, int(const SelectorInfo*, TrpcEndpointInfo*));
  MOCK_METHOD1(AsyncSelect, Future<TrpcEndpointInfo>(const SelectorInfo*));
  MOCK_METHOD2(SelectBatch, int(const SelectorInfo*, std::vector<TrpcEndpointInfo>*));
  MOCK_METHOD1(AsyncSelectBatch, Future<std::vector<TrpcEndpointInfo>>(const SelectorInfo*));
  MOCK_METHOD1(ReportInvokeResult, int(const InvokeResult*));
  MOCK_METHOD1(SetEndpoints, int(const RouterInfo*));
  MOCK_METHOD1(SetCircuitBreakWhiteList, bool(const std::vector<int>&));
};

class LruCacheSelectorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create mock selector
    mock_selector_ = MakeRefCounted<MockSelector>();

    // Setup default mock behavior
    ON_CALL(*mock_selector_, Name()).WillByDefault(::testing::Return("mock_selector"));
    ON_CALL(*mock_selector_, Version()).WillByDefault(::testing::Return("1.0.0"));

    // Create LRU cache selector with short TTL for testing
    LruCacheSelectorConfig config;
    config.max_capacity = 10;
    config.default_ttl_ms = 1000;  // 1 second
    cache_selector_ = MakeRefCounted<LruCacheSelector>(mock_selector_, config);
  }

  void TearDown() override {
    cache_selector_->ClearCache();
  }

  TrpcEndpointInfo CreateEndpoint(const std::string& host, int port) {
    TrpcEndpointInfo endpoint;
    endpoint.host = host;
    endpoint.port = port;
    endpoint.is_ipv6 = false;
    endpoint.weight = 100;
    return endpoint;
  }

  SelectorInfo CreateSelectorInfo(const std::string& service_name) {
    SelectorInfo info;
    info.name = service_name;
    info.policy = SelectorPolicy::ONE;
    info.load_balance_name = "polling";
    return info;
  }

 protected:
  RefPtr<MockSelector> mock_selector_;
  RefPtr<LruCacheSelector> cache_selector_;
};

// Test basic Select with cache hit/miss
TEST_F(LruCacheSelectorTest, SelectCacheHitMiss) {
  SelectorInfo info = CreateSelectorInfo("test.service");
  TrpcEndpointInfo expected_endpoint = CreateEndpoint("127.0.0.1", 8080);

  // First call - cache miss, should call underlying selector
  EXPECT_CALL(*mock_selector_, Select(::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(::testing::SetArgPointee<1>(expected_endpoint), ::testing::Return(0)));

  TrpcEndpointInfo result1;
  int ret = cache_selector_->Select(&info, &result1);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(result1.host, "127.0.0.1");
  EXPECT_EQ(result1.port, 8080);

  // Second call - cache hit, should NOT call underlying selector
  EXPECT_CALL(*mock_selector_, Select(::testing::_, ::testing::_)).Times(0);

  TrpcEndpointInfo result2;
  ret = cache_selector_->Select(&info, &result2);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(result2.host, "127.0.0.1");
  EXPECT_EQ(result2.port, 8080);

  // Verify statistics
  auto stats = cache_selector_->GetStatistics();
  EXPECT_EQ(stats.total_hits, 1);
  EXPECT_EQ(stats.total_misses, 1);
}

// Test Select with different services
TEST_F(LruCacheSelectorTest, SelectDifferentServices) {
  SelectorInfo info1 = CreateSelectorInfo("service1");
  SelectorInfo info2 = CreateSelectorInfo("service2");

  TrpcEndpointInfo endpoint1 = CreateEndpoint("127.0.0.1", 8081);
  TrpcEndpointInfo endpoint2 = CreateEndpoint("127.0.0.1", 8082);

  // Both should miss cache initially
  EXPECT_CALL(*mock_selector_, Select(::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(::testing::SetArgPointee<1>(endpoint1), ::testing::Return(0)))
      .WillOnce(::testing::DoAll(::testing::SetArgPointee<1>(endpoint2), ::testing::Return(0)));

  TrpcEndpointInfo result1, result2;
  cache_selector_->Select(&info1, &result1);
  cache_selector_->Select(&info2, &result2);

  EXPECT_EQ(result1.port, 8081);
  EXPECT_EQ(result2.port, 8082);

  // Both should hit cache on second call
  EXPECT_CALL(*mock_selector_, Select(::testing::_, ::testing::_)).Times(0);

  TrpcEndpointInfo result3, result4;
  cache_selector_->Select(&info1, &result3);
  cache_selector_->Select(&info2, &result4);

  EXPECT_EQ(result3.port, 8081);
  EXPECT_EQ(result4.port, 8082);
}

// Test SelectBatch with cache
TEST_F(LruCacheSelectorTest, SelectBatchCacheHitMiss) {
  SelectorInfo info = CreateSelectorInfo("test.service");
  info.policy = SelectorPolicy::ALL;

  std::vector<TrpcEndpointInfo> expected_endpoints;
  expected_endpoints.push_back(CreateEndpoint("127.0.0.1", 8080));
  expected_endpoints.push_back(CreateEndpoint("127.0.0.1", 8081));

  // First call - cache miss
  EXPECT_CALL(*mock_selector_, SelectBatch(::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(::testing::SetArgPointee<1>(expected_endpoints), ::testing::Return(0)));

  std::vector<TrpcEndpointInfo> result1;
  int ret = cache_selector_->SelectBatch(&info, &result1);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(result1.size(), 2);

  // Second call - cache hit
  EXPECT_CALL(*mock_selector_, SelectBatch(::testing::_, ::testing::_)).Times(0);

  std::vector<TrpcEndpointInfo> result2;
  ret = cache_selector_->SelectBatch(&info, &result2);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(result2.size(), 2);
  EXPECT_EQ(result2[0].port, 8080);
  EXPECT_EQ(result2[1].port, 8081);
}

// Test AsyncSelect with cache
TEST_F(LruCacheSelectorTest, AsyncSelectCacheHitMiss) {
  SelectorInfo info = CreateSelectorInfo("test.service");
  TrpcEndpointInfo expected_endpoint = CreateEndpoint("127.0.0.1", 8080);

  // First call - cache miss
  EXPECT_CALL(*mock_selector_, AsyncSelect(::testing::_))
      .WillOnce(::testing::Invoke([expected_endpoint](const SelectorInfo*) {
        return MakeReadyFuture<TrpcEndpointInfo>(TrpcEndpointInfo(expected_endpoint));
      }));

  auto fut1 = cache_selector_->AsyncSelect(&info);
  ASSERT_TRUE(fut1.IsReady());
  auto result1 = fut1.GetValue0();
  EXPECT_EQ(result1.host, "127.0.0.1");
  EXPECT_EQ(result1.port, 8080);

  // Second call - cache hit
  EXPECT_CALL(*mock_selector_, AsyncSelect(::testing::_)).Times(0);

  auto fut2 = cache_selector_->AsyncSelect(&info);
  ASSERT_TRUE(fut2.IsReady());
  auto result2 = fut2.GetValue0();
  EXPECT_EQ(result2.host, "127.0.0.1");
  EXPECT_EQ(result2.port, 8080);
}

// Test AsyncSelectBatch with cache
TEST_F(LruCacheSelectorTest, AsyncSelectBatchCacheHitMiss) {
  SelectorInfo info = CreateSelectorInfo("test.service");
  info.policy = SelectorPolicy::ALL;

  std::vector<TrpcEndpointInfo> expected_endpoints;
  expected_endpoints.push_back(CreateEndpoint("127.0.0.1", 8080));
  expected_endpoints.push_back(CreateEndpoint("127.0.0.1", 8081));

  // First call - cache miss
  EXPECT_CALL(*mock_selector_, AsyncSelectBatch(::testing::_))
      .WillOnce(::testing::Invoke([expected_endpoints](const SelectorInfo*) {
        return MakeReadyFuture<std::vector<TrpcEndpointInfo>>(
            std::vector<TrpcEndpointInfo>(expected_endpoints));
      }));

  auto fut1 = cache_selector_->AsyncSelectBatch(&info);
  ASSERT_TRUE(fut1.IsReady());
  auto result1 = fut1.GetValue0();
  EXPECT_EQ(result1.size(), 2);

  // Second call - cache hit
  EXPECT_CALL(*mock_selector_, AsyncSelectBatch(::testing::_)).Times(0);

  auto fut2 = cache_selector_->AsyncSelectBatch(&info);
  ASSERT_TRUE(fut2.IsReady());
  auto result2 = fut2.GetValue0();
  EXPECT_EQ(result2.size(), 2);
}

// Test TTL expiration
TEST_F(LruCacheSelectorTest, TTLExpiration) {
  // Create selector with very short TTL
  LruCacheSelectorConfig config;
  config.default_ttl_ms = 100;  // 100ms
  auto short_ttl_selector = MakeRefCounted<LruCacheSelector>(mock_selector_, config);

  SelectorInfo info = CreateSelectorInfo("test.service");
  TrpcEndpointInfo endpoint = CreateEndpoint("127.0.0.1", 8080);

  // First call - cache miss
  EXPECT_CALL(*mock_selector_, Select(::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(::testing::SetArgPointee<1>(endpoint), ::testing::Return(0)));

  TrpcEndpointInfo result1;
  short_ttl_selector->Select(&info, &result1);

  // Immediate second call - cache hit
  EXPECT_CALL(*mock_selector_, Select(::testing::_, ::testing::_)).Times(0);
  TrpcEndpointInfo result2;
  short_ttl_selector->Select(&info, &result2);

  // Wait for expiration
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Third call - cache miss (expired)
  EXPECT_CALL(*mock_selector_, Select(::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(::testing::SetArgPointee<1>(endpoint), ::testing::Return(0)));

  TrpcEndpointInfo result3;
  short_ttl_selector->Select(&info, &result3);
}

// Test SetEndpoints clears cache
TEST_F(LruCacheSelectorTest, SetEndpointsClearsCache) {
  SelectorInfo info = CreateSelectorInfo("test.service");
  TrpcEndpointInfo endpoint = CreateEndpoint("127.0.0.1", 8080);

  // Cache an entry
  EXPECT_CALL(*mock_selector_, Select(::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(::testing::SetArgPointee<1>(endpoint), ::testing::Return(0)));

  TrpcEndpointInfo result1;
  cache_selector_->Select(&info, &result1);

  // Call SetEndpoints - should clear cache
  RouterInfo router_info;
  router_info.name = "test.service";
  EXPECT_CALL(*mock_selector_, SetEndpoints(::testing::_)).WillOnce(::testing::Return(0));
  cache_selector_->SetEndpoints(&router_info);

  // Next Select should be cache miss
  EXPECT_CALL(*mock_selector_, Select(::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(::testing::SetArgPointee<1>(endpoint), ::testing::Return(0)));

  TrpcEndpointInfo result2;
  cache_selector_->Select(&info, &result2);
}

// Test ReportInvokeResult delegates to underlying
TEST_F(LruCacheSelectorTest, ReportInvokeResultDelegates) {
  InvokeResult result;
  result.name = "test.service";
  result.framework_result = 0;

  EXPECT_CALL(*mock_selector_, ReportInvokeResult(::testing::_)).WillOnce(::testing::Return(0));

  int ret = cache_selector_->ReportInvokeResult(&result);
  EXPECT_EQ(ret, 0);
}

// Test Name() returns decorated name
TEST_F(LruCacheSelectorTest, NameReturnsDecoratedName) {
  std::string name = cache_selector_->Name();
  EXPECT_EQ(name, "lru_cache_mock_selector");
}

// Test ClearCache
TEST_F(LruCacheSelectorTest, ClearCache) {
  SelectorInfo info = CreateSelectorInfo("test.service");
  TrpcEndpointInfo endpoint = CreateEndpoint("127.0.0.1", 8080);

  // Cache an entry
  EXPECT_CALL(*mock_selector_, Select(::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(::testing::SetArgPointee<1>(endpoint), ::testing::Return(0)));

  TrpcEndpointInfo result1;
  cache_selector_->Select(&info, &result1);

  // Clear cache
  cache_selector_->ClearCache();

  // Next call should be cache miss
  EXPECT_CALL(*mock_selector_, Select(::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(::testing::SetArgPointee<1>(endpoint), ::testing::Return(0)));

  TrpcEndpointInfo result2;
  cache_selector_->Select(&info, &result2);
}

// Test statistics collection
TEST_F(LruCacheSelectorTest, Statistics) {
  SelectorInfo info = CreateSelectorInfo("test.service");
  TrpcEndpointInfo endpoint = CreateEndpoint("127.0.0.1", 8080);

  auto stats = cache_selector_->GetStatistics();
  EXPECT_EQ(stats.total_hits, 0);
  EXPECT_EQ(stats.total_misses, 0);

  // First call - miss
  EXPECT_CALL(*mock_selector_, Select(::testing::_, ::testing::_))
      .WillOnce(::testing::DoAll(::testing::SetArgPointee<1>(endpoint), ::testing::Return(0)));

  TrpcEndpointInfo result1;
  cache_selector_->Select(&info, &result1);

  stats = cache_selector_->GetStatistics();
  EXPECT_EQ(stats.total_misses, 1);

  // Second call - hit
  TrpcEndpointInfo result2;
  cache_selector_->Select(&info, &result2);

  stats = cache_selector_->GetStatistics();
  EXPECT_EQ(stats.total_hits, 1);
  EXPECT_EQ(stats.total_misses, 1);
}

}  // namespace trpc::testing
