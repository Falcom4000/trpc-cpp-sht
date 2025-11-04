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

#include "trpc/common/future/future.h"
#include "trpc/naming/selector.h"
#include "trpc/util/lru_cache/lru_cache.h"

namespace trpc {

/// @brief Configuration for LRU cache selector
struct LruCacheSelectorConfig {
  /// Maximum number of cached service routing results
  size_t max_capacity = 1000;
  
  /// Default TTL for cached entries in milliseconds (default: 60 seconds)
  uint64_t default_ttl_ms = 60000;
  
  /// Enable cache statistics collection
  bool enable_statistics = true;
};

/// @brief Selector decorator that caches service discovery results using LRU cache
/// @note This is a transparent decorator that wraps any existing Selector implementation
class LruCacheSelector : public Selector {
 public:
  /// @brief Constructor with underlying selector and configuration
  /// @param underlying_selector The actual selector to wrap
  /// @param config Configuration for the cache
  explicit LruCacheSelector(SelectorPtr underlying_selector, 
                           const LruCacheSelectorConfig& config = LruCacheSelectorConfig{});

  /// @brief Constructor with underlying selector (uses default config)
  explicit LruCacheSelector(SelectorPtr underlying_selector);

  ~LruCacheSelector() override = default;

  // Selector interface implementation
  std::string Name() const override;
  std::string Version() const override;
  
  int Select(const SelectorInfo* info, TrpcEndpointInfo* endpoint) override;
  Future<TrpcEndpointInfo> AsyncSelect(const SelectorInfo* info) override;
  
  int SelectBatch(const SelectorInfo* info, std::vector<TrpcEndpointInfo>* endpoints) override;
  Future<std::vector<TrpcEndpointInfo>> AsyncSelectBatch(const SelectorInfo* info) override;
  
  int ReportInvokeResult(const InvokeResult* result) override;
  int SetEndpoints(const RouterInfo* info) override;
  bool SetCircuitBreakWhiteList(const std::vector<int>& framework_retcodes) override;

  /// @brief Get cache statistics
  LruCacheStatisticsSnapshot GetStatistics() const;

  /// @brief Clear all cached entries
  void ClearCache();

 private:
  /// @brief Generate cache key from SelectorInfo
  std::string GenerateCacheKey(const SelectorInfo* info) const;

  /// @brief Generate cache key for batch selection
  std::string GenerateBatchCacheKey(const SelectorInfo* info) const;

 private:
  /// The underlying selector to delegate to
  SelectorPtr underlying_selector_;
  
  /// Configuration
  LruCacheSelectorConfig config_;
  
  /// LRU cache for single endpoint selection (service_name:policy -> endpoint)
  std::unique_ptr<LruCache<std::string, TrpcEndpointInfo>> single_cache_;
  
  /// LRU cache for batch endpoint selection (service_name:policy -> vector<endpoint>)
  std::unique_ptr<LruCache<std::string, std::vector<TrpcEndpointInfo>>> batch_cache_;
};

using LruCacheSelectorPtr = RefPtr<LruCacheSelector>;

}  // namespace trpc
