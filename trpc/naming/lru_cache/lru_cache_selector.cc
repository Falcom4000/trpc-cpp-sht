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

#include <sstream>

#include "trpc/util/log/logging.h"

namespace trpc {

LruCacheSelector::LruCacheSelector(SelectorPtr underlying_selector,
                                   const LruCacheSelectorConfig& config)
    : underlying_selector_(std::move(underlying_selector)), config_(config) {
  // Create LRU caches with TTL enabled
  LruCacheConfig cache_config;
  cache_config.capacity = config_.max_capacity;
  cache_config.enable_ttl = true;
  cache_config.enable_statistics = config_.enable_statistics;

  single_cache_ = std::make_unique<LruCache<std::string, TrpcEndpointInfo>>(cache_config);
  batch_cache_ = std::make_unique<LruCache<std::string, std::vector<TrpcEndpointInfo>>>(cache_config);
}

LruCacheSelector::LruCacheSelector(SelectorPtr underlying_selector)
    : LruCacheSelector(std::move(underlying_selector), LruCacheSelectorConfig{}) {}

std::string LruCacheSelector::Name() const {
  return "lru_cache_" + underlying_selector_->Name();
}

std::string LruCacheSelector::Version() const {
  return underlying_selector_->Version();
}

std::string LruCacheSelector::GenerateCacheKey(const SelectorInfo* info) const {
  if (!info) {
    return "";
  }

  // Cache key format: "service_name:policy:load_balance"
  std::ostringstream oss;
  oss << info->name << ":" << static_cast<int>(info->policy);
  
  if (!info->load_balance_name.empty()) {
    oss << ":" << info->load_balance_name;
  }

  // Include namespace if available in context
  if (info->context) {
    const std::string& ns = info->context->GetNamespace();
    if (!ns.empty()) {
      oss << ":ns=" << ns;
    }
  }

  return oss.str();
}

std::string LruCacheSelector::GenerateBatchCacheKey(const SelectorInfo* info) const {
  if (!info) {
    return "";
  }

  // For batch selection, include select_num in the key
  std::ostringstream oss;
  oss << info->name << ":" << static_cast<int>(info->policy) << ":batch=" << info->select_num;
  
  if (!info->load_balance_name.empty()) {
    oss << ":" << info->load_balance_name;
  }

  // Include namespace if available in context
  if (info->context) {
    const std::string& ns = info->context->GetNamespace();
    if (!ns.empty()) {
      oss << ":ns=" << ns;
    }
  }

  return oss.str();
}

int LruCacheSelector::Select(const SelectorInfo* info, TrpcEndpointInfo* endpoint) {
  if (!info || !endpoint) {
    return -1;
  }

  // Generate cache key
  std::string cache_key = GenerateCacheKey(info);
  if (cache_key.empty()) {
    TRPC_FMT_WARN("Failed to generate cache key for service {}", info->name);
    return underlying_selector_->Select(info, endpoint);
  }

  // Try to get from cache
  auto cached_value = single_cache_->Get(cache_key);
  if (cached_value.has_value()) {
    *endpoint = cached_value.value();
    TRPC_FMT_DEBUG("Cache hit for service {}, key: {}", info->name, cache_key);
    return 0;
  }

  TRPC_FMT_DEBUG("Cache miss for service {}, key: {}", info->name, cache_key);

  // Cache miss - call underlying selector
  int ret = underlying_selector_->Select(info, endpoint);
  if (ret != 0) {
    return ret;
  }

  // Store in cache with TTL
  auto ttl = std::chrono::milliseconds(config_.default_ttl_ms);
  single_cache_->Put(cache_key, *endpoint, ttl);

  TRPC_FMT_DEBUG("Cached endpoint for service {}, key: {}", info->name, cache_key);

  return 0;
}

Future<TrpcEndpointInfo> LruCacheSelector::AsyncSelect(const SelectorInfo* info) {
  if (!info) {
    return MakeExceptionFuture<TrpcEndpointInfo>(CommonException("info is null"));
  }

  // Generate cache key
  std::string cache_key = GenerateCacheKey(info);
  if (cache_key.empty()) {
    TRPC_FMT_WARN("Failed to generate cache key for service {}", info->name);
    return underlying_selector_->AsyncSelect(info);
  }

  // Try to get from cache
  auto cached_value = single_cache_->Get(cache_key);
  if (cached_value.has_value()) {
    TRPC_FMT_DEBUG("Async cache hit for service {}, key: {}", info->name, cache_key);
    return MakeReadyFuture<TrpcEndpointInfo>(std::move(cached_value.value()));
  }

  TRPC_FMT_DEBUG("Async cache miss for service {}, key: {}", info->name, cache_key);

  // Cache miss - call underlying selector and cache the result
  return underlying_selector_->AsyncSelect(info).Then([this, cache_key, service_name = info->name](
                                                          Future<TrpcEndpointInfo>&& fut) {
    if (fut.IsReady()) {
      TrpcEndpointInfo endpoint = fut.GetValue0();
      
      // Store in cache with TTL
      auto ttl = std::chrono::milliseconds(config_.default_ttl_ms);
      single_cache_->Put(cache_key, endpoint, ttl);
      
      TRPC_FMT_DEBUG("Async cached endpoint for service {}, key: {}", service_name, cache_key);
      
      return MakeReadyFuture<TrpcEndpointInfo>(std::move(endpoint));
    }
    
    // Propagate exception
    return std::move(fut);
  });
}

int LruCacheSelector::SelectBatch(const SelectorInfo* info, std::vector<TrpcEndpointInfo>* endpoints) {
  if (!info || !endpoints) {
    return -1;
  }

  // Generate cache key
  std::string cache_key = GenerateBatchCacheKey(info);
  if (cache_key.empty()) {
    TRPC_FMT_WARN("Failed to generate batch cache key for service {}", info->name);
    return underlying_selector_->SelectBatch(info, endpoints);
  }

  // Try to get from cache
  auto cached_value = batch_cache_->Get(cache_key);
  if (cached_value.has_value()) {
    *endpoints = cached_value.value();
    TRPC_FMT_DEBUG("Batch cache hit for service {}, key: {}", info->name, cache_key);
    return 0;
  }

  TRPC_FMT_DEBUG("Batch cache miss for service {}, key: {}", info->name, cache_key);

  // Cache miss - call underlying selector
  int ret = underlying_selector_->SelectBatch(info, endpoints);
  if (ret != 0) {
    return ret;
  }

  // Store in cache with TTL
  auto ttl = std::chrono::milliseconds(config_.default_ttl_ms);
  batch_cache_->Put(cache_key, *endpoints, ttl);

  TRPC_FMT_DEBUG("Cached batch endpoints for service {}, key: {}, count: {}",
                 info->name, cache_key, endpoints->size());

  return 0;
}

Future<std::vector<TrpcEndpointInfo>> LruCacheSelector::AsyncSelectBatch(const SelectorInfo* info) {
  if (!info) {
    return MakeExceptionFuture<std::vector<TrpcEndpointInfo>>(CommonException("info is null"));
  }

  // Generate cache key
  std::string cache_key = GenerateBatchCacheKey(info);
  if (cache_key.empty()) {
    TRPC_FMT_WARN("Failed to generate batch cache key for service {}", info->name);
    return underlying_selector_->AsyncSelectBatch(info);
  }

  // Try to get from cache
  auto cached_value = batch_cache_->Get(cache_key);
  if (cached_value.has_value()) {
    TRPC_FMT_DEBUG("Async batch cache hit for service {}, key: {}", info->name, cache_key);
    return MakeReadyFuture<std::vector<TrpcEndpointInfo>>(std::move(cached_value.value()));
  }

  TRPC_FMT_DEBUG("Async batch cache miss for service {}, key: {}", info->name, cache_key);

  // Cache miss - call underlying selector and cache the result
  return underlying_selector_->AsyncSelectBatch(info).Then(
      [this, cache_key, service_name = info->name](Future<std::vector<TrpcEndpointInfo>>&& fut) {
        if (fut.IsReady()) {
          std::vector<TrpcEndpointInfo> endpoints = fut.GetValue0();
          
          // Store in cache with TTL
          auto ttl = std::chrono::milliseconds(config_.default_ttl_ms);
          batch_cache_->Put(cache_key, endpoints, ttl);
          
          TRPC_FMT_DEBUG("Async cached batch endpoints for service {}, key: {}, count: {}",
                         service_name, cache_key, endpoints.size());
          
          return MakeReadyFuture<std::vector<TrpcEndpointInfo>>(std::move(endpoints));
        }
        
        // Propagate exception
        return std::move(fut);
      });
}

int LruCacheSelector::ReportInvokeResult(const InvokeResult* result) {
  // Always delegate to underlying selector for reporting
  return underlying_selector_->ReportInvokeResult(result);
}

int LruCacheSelector::SetEndpoints(const RouterInfo* info) {
  // Clear cache when endpoints are manually set
  if (info) {
    TRPC_FMT_INFO("Clearing cache due to SetEndpoints for service {}", info->name);
    ClearCache();
  }
  
  // Delegate to underlying selector
  return underlying_selector_->SetEndpoints(info);
}

bool LruCacheSelector::SetCircuitBreakWhiteList(const std::vector<int>& framework_retcodes) {
  // Delegate to underlying selector
  return underlying_selector_->SetCircuitBreakWhiteList(framework_retcodes);
}

LruCacheStatisticsSnapshot LruCacheSelector::GetStatistics() const {
  // Get combined statistics from both caches
  auto single_stats = single_cache_->GetStatistics();
  auto batch_stats = batch_cache_->GetStatistics();

  // Combine statistics
  LruCacheStatisticsSnapshot combined;
  combined.total_gets = single_stats.total_gets + batch_stats.total_gets;
  combined.total_hits = single_stats.total_hits + batch_stats.total_hits;
  combined.total_misses = single_stats.total_misses + batch_stats.total_misses;
  combined.total_puts = single_stats.total_puts + batch_stats.total_puts;
  combined.total_evictions = single_stats.total_evictions + batch_stats.total_evictions;
  combined.total_expirations = single_stats.total_expirations + batch_stats.total_expirations;

  return combined;
}

void LruCacheSelector::ClearCache() {
  single_cache_->Clear();
  batch_cache_->Clear();
  TRPC_FMT_INFO("Cleared all selector caches");
}

}  // namespace trpc
