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
#include <optional>

#include "trpc/util/lru_cache/detail/adaptive_mutex.h"
#include "trpc/util/lru_cache/lru_cache_statistics.h"

namespace trpc {

/// @brief Configuration for LRU cache
struct LruCacheConfig {
  size_t capacity = 1000;                                        // Cache capacity
  bool enable_ttl = false;                                       // Enable TTL feature
  std::chrono::milliseconds default_ttl{0};                      // Default TTL (0 means never expire)
  bool enable_statistics = true;                                 // Enable statistics collection
};

/// @brief LRU (Least Recently Used) Cache implementation with thread-safety
/// @tparam K Key type, must be hashable and comparable
/// @tparam V Value type, must be copyable or movable
/// @tparam MutexType Mutex type, defaults to AdaptiveMutex
template <typename K, typename V, typename MutexType = lru_cache::detail::AdaptiveMutex>
class LruCache {
 public:
  using KeyType = K;
  using ValueType = V;

  /// @brief Constructor with capacity
  /// @param capacity Maximum number of items in the cache
  explicit LruCache(size_t capacity);

  /// @brief Constructor with configuration
  /// @param config LRU cache configuration
  explicit LruCache(const LruCacheConfig& config);

  /// @brief Destructor
  ~LruCache();

  // Delete copy constructor and assignment
  LruCache(const LruCache&) = delete;
  LruCache& operator=(const LruCache&) = delete;

  /// @brief Put a key-value pair into the cache
  /// @param key The key
  /// @param value The value
  /// @param ttl Time-to-live (0 means use default TTL)
  /// @return true on success, false on failure
  bool Put(const K& key, const V& value,
           std::chrono::milliseconds ttl = std::chrono::milliseconds{0});

  /// @brief Put a key-value pair into the cache (move semantics)
  /// @param key The key
  /// @param value The value (will be moved)
  /// @param ttl Time-to-live (0 means use default TTL)
  /// @return true on success, false on failure
  bool Put(const K& key, V&& value,
           std::chrono::milliseconds ttl = std::chrono::milliseconds{0});

  /// @brief Get a value from the cache
  /// @param key The key
  /// @return Optional value (nullopt if not found or expired)
  std::optional<V> Get(const K& key);

  /// @brief Get a value from the cache (output parameter version)
  /// @param key The key
  /// @param value Output parameter for the value
  /// @return true if found and not expired, false otherwise
  bool Get(const K& key, V& value);

  /// @brief Remove a key from the cache
  /// @param key The key
  /// @return true if removed, false if not found
  bool Remove(const K& key);

  /// @brief Clear all items from the cache
  void Clear();

  /// @brief Get current cache size
  /// @return Number of items in the cache
  size_t Size() const;

  /// @brief Get cache capacity
  /// @return Maximum capacity of the cache
  size_t Capacity() const { return capacity_; }

  /// @brief Get statistics snapshot
  /// @return Snapshot of the statistics
  LruCacheStatisticsSnapshot GetStatistics() const;

  /// @brief Reset statistics
  void ResetStatistics();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
  size_t capacity_;
  LruCacheConfig config_;
};

}  // namespace trpc
