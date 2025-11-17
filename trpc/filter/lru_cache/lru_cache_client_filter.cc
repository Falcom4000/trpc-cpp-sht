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

#include <functional>
#include <sstream>

#include "trpc/codec/codec_manager.h"
#include "trpc/common/status.h"
#include "trpc/util/buffer/noncontiguous_buffer.h"
#include "trpc/util/log/logging.h"

namespace trpc {

namespace {

// Custom context index for storing cache key.
constexpr uint32_t kLruCacheKeyIndex = 10000;

}  // namespace

LruCacheClientFilter::LruCacheClientFilter(const LruCacheClientFilterConfig& config)
    : config_(config) {
  // Create LRU cache with TTL enabled
  LruCacheConfig cache_config;
  cache_config.capacity = config.max_capacity;
  cache_config.enable_ttl = true;
  cache_config.enable_statistics = true;
  
  cache_ = std::make_unique<LruCache<std::string, NoncontiguousBuffer>>(cache_config);
}

LruCacheClientFilter::LruCacheClientFilter()
    : LruCacheClientFilter(LruCacheClientFilterConfig{}) {}

void LruCacheClientFilter::operator()(FilterStatus& status, FilterPoint point,
                                      const ClientContextPtr& context) {
  switch (point) {
    case FilterPoint::CLIENT_PRE_RPC_INVOKE:
      HandlePreInvoke(status, context);
      break;

    case FilterPoint::CLIENT_POST_RPC_INVOKE:
      HandlePostInvoke(context);
      // Always continue for POST point
      status = FilterStatus::CONTINUE;
      break;

    default:
      status = FilterStatus::CONTINUE;
      break;
  }
}

bool LruCacheClientFilter::ShouldCache(const std::string& func_name) const {
  if (!config_.enable_whitelist) {
    return true;
  }

  return config_.method_whitelist.find(func_name) != config_.method_whitelist.end();
}

std::string LruCacheClientFilter::GenerateCacheKey(const ClientContextPtr& context) {
  std::string callee_name = context->GetCalleeName();
  std::string func_name = context->GetFuncName();

  // Get request protocol message
  const ProtocolPtr& req_msg = context->GetRequest();
  if (!req_msg) {
    return "";
  }

  // Get request body bytes for hashing
  NoncontiguousBuffer req_buffer = req_msg->GetNonContiguousProtocolBody();
  uint64_t req_hash = HashRequestData(req_buffer);

  // Format: "callee:func:hash"
  std::ostringstream oss;
  oss << callee_name << ":" << func_name << ":" << req_hash;
  return oss.str();
}

uint64_t LruCacheClientFilter::HashRequestData(const NoncontiguousBuffer& data) {
  // Simple hash implementation using FNV-1a algorithm
  uint64_t hash = 14695981039346656037ULL;  // FNV offset basis
  const uint64_t prime = 1099511628211ULL;  // FNV prime

  // Flatten buffer to contiguous bytes for hashing
  std::string flattened = FlattenSlow(data);
  for (unsigned char c : flattened) {
    hash ^= static_cast<uint64_t>(c);
    hash *= prime;
  }

  return hash;
}

void LruCacheClientFilter::HandlePreInvoke(FilterStatus& status, const ClientContextPtr& context) {
  status = FilterStatus::CONTINUE;

  std::string func_name = context->GetFuncName();

  // Check if this method should be cached
  if (!ShouldCache(func_name)) {
    TRPC_FMT_DEBUG("Method {} not in whitelist, skip cache", func_name);
    return;
  }

  // Generate cache key
  std::string cache_key = GenerateCacheKey(context);
  if (cache_key.empty()) {
    TRPC_FMT_WARN("Failed to generate cache key for method {}", func_name);
    return;
  }

  // Try to get from cache
  auto cached_value = cache_->Get(cache_key);
  if (!cached_value.has_value()) {
    TRPC_FMT_DEBUG("Cache miss for method {}", func_name);
    // Store cache key in context for POST point (to cache the response)
    context->SetFilterData<std::string>(kLruCacheKeyIndex, std::move(cache_key));
    return;
  }

  // Cache hit! Deserialize cached response
  if (!DeserializeResponse(cached_value.value(), context)) {
    TRPC_FMT_WARN("Failed to deserialize cached response for method {}", func_name);
    // Remove invalid cache entry
    cache_->Remove(cache_key);
    return;
  }

  TRPC_FMT_DEBUG("Cache hit for method {}, key: {}", func_name, cache_key);

  // Set success status to indicate cache hit
  context->SetStatus(Status(0, "cache_hit"));

  // Stop filter chain - we already have the response
  status = FilterStatus::REJECT;
}

void LruCacheClientFilter::HandlePostInvoke(const ClientContextPtr& context) {
  // Only cache successful responses
  const Status& status = context->GetStatus();
  if (!status.OK()) {
    TRPC_FMT_DEBUG("RPC failed with status {}, skip caching", status.ToString());
    return;
  }

  // Get cache key from PRE point
  const std::string* cache_key = context->GetFilterData<std::string>(kLruCacheKeyIndex);
  if (!cache_key) {
    return;
  }

  std::string func_name = context->GetFuncName();

  // Serialize response
  NoncontiguousBuffer response_buffer;
  if (!SerializeResponse(context, response_buffer)) {
    TRPC_FMT_WARN("Failed to serialize response for method {}", func_name);
    return;
  }

  // Store in cache with TTL
  auto ttl = std::chrono::milliseconds(config_.default_ttl_ms);
  cache_->Put(*cache_key, std::move(response_buffer), ttl);

  TRPC_FMT_DEBUG("Cached response for method {}, key: {}", func_name, *cache_key);
}

bool LruCacheClientFilter::SerializeResponse(const ClientContextPtr& context,
                                              NoncontiguousBuffer& buffer) {
  // Get response protocol message
  const ProtocolPtr& rsp_msg = context->GetResponse();
  if (!rsp_msg) {
    return false;
  }

  // Get response body bytes
  buffer = rsp_msg->GetNonContiguousProtocolBody();
  
  // Also need to copy response attachment if exists
  const NoncontiguousBuffer& attachment = context->GetResponseAttachment();
  if (attachment.ByteSize() > 0) {
    // Append a separator and attachment
    // In real production, should use a more robust serialization format
    buffer.Append(attachment);
  }

  return buffer.ByteSize() > 0;
}

bool LruCacheClientFilter::DeserializeResponse(const NoncontiguousBuffer& buffer,
                                                const ClientContextPtr& context) {
  if (buffer.ByteSize() == 0) {
    return false;
  }

  // Get response protocol message
  ProtocolPtr& rsp_msg = context->GetResponse();
  if (!rsp_msg) {
    return false;
  }

  // Set response body bytes
  // Note: In a real production system, we need to properly decode the protocol message
  // For now, we directly set the body
  NoncontiguousBuffer body_copy = buffer;
  rsp_msg->SetNonContiguousProtocolBody(std::move(body_copy));

  return true;
}

LruCacheStatisticsSnapshot LruCacheClientFilter::GetStatistics() const {
  return cache_->GetStatistics();
}

void LruCacheClientFilter::ClearCache() {
  cache_->Clear();
}

}  // namespace trpc
