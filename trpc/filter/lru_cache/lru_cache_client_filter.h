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

#include <chrono>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "trpc/client/client_context.h"
#include "trpc/filter/client_filter_base.h"
#include "trpc/util/lru_cache/lru_cache.h"

namespace trpc {

/// @brief Configuration for LRU cache client filter.
struct LruCacheClientFilterConfig {
  /// @brief Default TTL for cached responses (milliseconds).
  uint32_t default_ttl_ms = 60000;  // 60 seconds

  /// @brief Maximum capacity of the cache.
  size_t max_capacity = 10000;

  /// @brief Whitelist of methods that should be cached.
  /// If empty, all methods are cached.
  std::unordered_set<std::string> method_whitelist;

  /// @brief Whether to use method whitelist. If false, all methods are cached.
  bool enable_whitelist = false;
};

/// @brief Client filter for caching RPC responses using LRU cache.
/// 
/// This filter caches successful RPC responses to reduce redundant backend calls.
/// It operates at two filter points:
/// - CLIENT_PRE_RPC_INVOKE: Check if cached response exists, return it if found
/// - CLIENT_POST_RPC_INVOKE: Store successful response in cache
///
/// Cache key format: "callee_name:func_name:request_hash"
/// Cache value: Serialized response bytes (NoncontiguousBuffer)
///
/// Usage example:
/// @code
///   LruCacheClientFilterConfig config;
///   config.default_ttl_ms = 30000;  // 30 seconds
///   config.max_capacity = 5000;
///   config.enable_whitelist = true;
///   config.method_whitelist.insert("/trpc.test.helloworld.Greeter/SayHello");
///
///   auto filter = std::make_shared<LruCacheClientFilter>(config);
///   FilterManager::GetInstance()->AddMessageClientFilter(filter);
/// @endcode
class LruCacheClientFilter : public MessageClientFilter {
 public:
  /// @brief Construct with configuration.
  explicit LruCacheClientFilter(const LruCacheClientFilterConfig& config);

  /// @brief Default constructor with default configuration.
  LruCacheClientFilter();

  /// @brief Destructor.
  ~LruCacheClientFilter() override = default;

  /// @brief Get filter name.
  std::string Name() override { return "lru_cache_client"; }

  /// @brief Get filter points where this filter operates.
  std::vector<FilterPoint> GetFilterPoint() override {
    return {FilterPoint::CLIENT_PRE_RPC_INVOKE, FilterPoint::CLIENT_POST_RPC_INVOKE};
  }

  /// @brief Filter entry point invoked by the framework.
  /// @param status Filter execution status (CONTINUE or REJECT)
  /// @param point Current filter point
  /// @param context Client context containing request/response information
  void operator()(FilterStatus& status, FilterPoint point, const ClientContextPtr& context) override;

  /// @brief Get current cache statistics.
  LruCacheStatisticsSnapshot GetStatistics() const;

  /// @brief Clear all cached entries.
  void ClearCache();

 private:
  /// @brief Check if the method should be cached according to whitelist.
  bool ShouldCache(const std::string& func_name) const;

  /// @brief Generate cache key from context.
  /// Format: "callee:func:hash(request_bytes)"
  std::string GenerateCacheKey(const ClientContextPtr& context);

  /// @brief Hash function for request data.
  uint64_t HashRequestData(const NoncontiguousBuffer& data);

  /// @brief Handle CLIENT_PRE_RPC_INVOKE: Check cache and return if hit.
  void HandlePreInvoke(FilterStatus& status, const ClientContextPtr& context);

  /// @brief Handle CLIENT_POST_RPC_INVOKE: Store response in cache.
  void HandlePostInvoke(const ClientContextPtr& context);

  /// @brief Serialize response to bytes for caching.
  /// @return true if serialization succeeded, false otherwise
  bool SerializeResponse(const ClientContextPtr& context, NoncontiguousBuffer& buffer);

  /// @brief Deserialize cached bytes back to response.
  /// @return true if deserialization succeeded, false otherwise
  bool DeserializeResponse(const NoncontiguousBuffer& buffer, const ClientContextPtr& context);

 private:
  /// Configuration for the filter.
  LruCacheClientFilterConfig config_;

  /// LRU cache: key is cache key string, value is serialized response bytes.
  std::unique_ptr<LruCache<std::string, NoncontiguousBuffer>> cache_;
};

using LruCacheClientFilterPtr = std::shared_ptr<LruCacheClientFilter>;

}  // namespace trpc
