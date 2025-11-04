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

#include "trpc/util/lru_cache/lru_cache.h"

#include <unordered_map>
#include <utility>

#include "trpc/naming/common/common_defs.h"
#include "trpc/util/buffer/noncontiguous_buffer.h"
#include "trpc/util/lru_cache/detail/lru_node.h"

namespace trpc {

template <typename K, typename V, typename MutexType>
class LruCache<K, V, MutexType>::Impl {
 public:
  using NodeType = lru_cache::detail::LruNode<K, V>;
  using MapType = std::unordered_map<K, NodeType*>;

  explicit Impl(size_t capacity, const LruCacheConfig& config)
      : capacity_(capacity), config_(config) {
    // Initialize sentinel nodes using new
    head_ = new NodeType();
    tail_ = new NodeType();
    head_->next = tail_;
    tail_->prev = head_;
  }

  ~Impl() {
    Clear();
    delete head_;
    delete tail_;
  }

  // Use template to handle both const V& and V&& with perfect forwarding
  template <typename ValueType>
  bool Put(const K& key, ValueType&& value, std::chrono::milliseconds ttl) {
    std::lock_guard<MutexType> lock(mutex_);

    // Check if key already exists
    auto it = map_.find(key);
    if (it != map_.end()) {
      // Update existing node
      NodeType* node = it->second;
      node->value = std::forward<ValueType>(value);
      node->expire_time = CalculateExpireTime(ttl);
      MoveToHead(node);

      if (config_.enable_statistics) {
        stats_.total_puts.fetch_add(1, std::memory_order_relaxed);
      }
      return true;
    }

    // If cache is full, evict LRU
    if (map_.size() >= capacity_) {
      EvictLru();
    }

    // Create new node and add to head
    NodeType* node = new NodeType();
    node->key = key;
    node->value = std::forward<ValueType>(value);
    node->expire_time = CalculateExpireTime(ttl);

    AddToHead(node);
    map_[key] = node;

    if (config_.enable_statistics) {
      stats_.total_puts.fetch_add(1, std::memory_order_relaxed);
    }

    return true;
  }

  std::optional<V> Get(const K& key) {
    std::lock_guard<MutexType> lock(mutex_);

    if (config_.enable_statistics) {
      stats_.total_gets.fetch_add(1, std::memory_order_relaxed);
    }

    auto it = map_.find(key);
    if (it == map_.end()) {
      if (config_.enable_statistics) {
        stats_.total_misses.fetch_add(1, std::memory_order_relaxed);
      }
      return std::nullopt;
    }

    NodeType* node = it->second;

    // Check if expired
    if (config_.enable_ttl && node->IsExpired()) {
      RemoveNode(node);
      map_.erase(it);
      delete node;

      if (config_.enable_statistics) {
        stats_.total_misses.fetch_add(1, std::memory_order_relaxed);
        stats_.total_expirations.fetch_add(1, std::memory_order_relaxed);
      }
      return std::nullopt;
    }

    // Move to head (most recently used)
    MoveToHead(node);

    if (config_.enable_statistics) {
      stats_.total_hits.fetch_add(1, std::memory_order_relaxed);
    }

    return node->value;
  }

  bool Get(const K& key, V& value) {
    std::lock_guard<MutexType> lock(mutex_);

    if (config_.enable_statistics) {
      stats_.total_gets.fetch_add(1, std::memory_order_relaxed);
    }

    auto it = map_.find(key);
    if (it == map_.end()) {
      if (config_.enable_statistics) {
        stats_.total_misses.fetch_add(1, std::memory_order_relaxed);
      }
      return false;
    }

    NodeType* node = it->second;

    // Check if expired
    if (config_.enable_ttl && node->IsExpired()) {
      RemoveNode(node);
      map_.erase(it);
      delete node;

      if (config_.enable_statistics) {
        stats_.total_misses.fetch_add(1, std::memory_order_relaxed);
        stats_.total_expirations.fetch_add(1, std::memory_order_relaxed);
      }
      return false;
    }

    // Move to head (most recently used)
    MoveToHead(node);
    value = node->value;

    if (config_.enable_statistics) {
      stats_.total_hits.fetch_add(1, std::memory_order_relaxed);
    }

    return true;
  }

  bool Remove(const K& key) {
    std::lock_guard<MutexType> lock(mutex_);

    auto it = map_.find(key);
    if (it == map_.end()) {
      return false;
    }

    NodeType* node = it->second;
    RemoveNode(node);
    map_.erase(it);
    delete node;

    return true;
  }

  void Clear() {
    std::lock_guard<MutexType> lock(mutex_);

    NodeType* cur = head_->next;
    while (cur != tail_) {
      NodeType* next = cur->next;
      delete cur;
      cur = next;
    }

    head_->next = tail_;
    tail_->prev = head_;
    map_.clear();
  }

  size_t Size() const {
    std::lock_guard<MutexType> lock(mutex_);
    return map_.size();
  }

  LruCacheStatisticsSnapshot GetStatistics() const {
    LruCacheStatisticsSnapshot snapshot;
    snapshot.total_gets = stats_.total_gets.load(std::memory_order_relaxed);
    snapshot.total_hits = stats_.total_hits.load(std::memory_order_relaxed);
    snapshot.total_misses = stats_.total_misses.load(std::memory_order_relaxed);
    snapshot.total_puts = stats_.total_puts.load(std::memory_order_relaxed);
    snapshot.total_evictions = stats_.total_evictions.load(std::memory_order_relaxed);
    snapshot.total_expirations = stats_.total_expirations.load(std::memory_order_relaxed);
    return snapshot;
  }

  void ResetStatistics() { stats_.Reset(); }

 private:
  void AddToHead(NodeType* node) {
    node->next = head_->next;
    node->prev = head_;
    head_->next->prev = node;
    head_->next = node;
  }

  void RemoveNode(NodeType* node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
  }

  void MoveToHead(NodeType* node) {
    RemoveNode(node);
    AddToHead(node);
  }

  void EvictLru() {
    NodeType* victim = tail_->prev;
    if (victim == head_) {
      return;  // Empty list
    }

    RemoveNode(victim);
    map_.erase(victim->key);
    delete victim;

    if (config_.enable_statistics) {
      stats_.total_evictions.fetch_add(1, std::memory_order_relaxed);
    }
  }

  std::chrono::steady_clock::time_point CalculateExpireTime(std::chrono::milliseconds ttl) {
    if (!config_.enable_ttl) {
      return std::chrono::steady_clock::time_point::max();
    }

    if (ttl.count() == 0) {
      ttl = config_.default_ttl;
    }

    if (ttl.count() == 0) {
      return std::chrono::steady_clock::time_point::max();
    }

    return std::chrono::steady_clock::now() + ttl;
  }

 private:
  size_t capacity_;
  LruCacheConfig config_;
  MapType map_;
  NodeType* head_;
  NodeType* tail_;
  mutable MutexType mutex_;
  LruCacheStatistics stats_;
};

// Constructor implementations
template <typename K, typename V, typename MutexType>
LruCache<K, V, MutexType>::LruCache(size_t capacity)
    : impl_(new Impl(capacity, LruCacheConfig{})), capacity_(capacity) {
  config_.capacity = capacity;
}

template <typename K, typename V, typename MutexType>
LruCache<K, V, MutexType>::LruCache(const LruCacheConfig& config)
    : impl_(new Impl(config.capacity, config)), capacity_(config.capacity), config_(config) {}

template <typename K, typename V, typename MutexType>
LruCache<K, V, MutexType>::~LruCache() = default;

// Method implementations
template <typename K, typename V, typename MutexType>
bool LruCache<K, V, MutexType>::Put(const K& key, const V& value, std::chrono::milliseconds ttl) {
  return impl_->Put(key, value, ttl);
}

template <typename K, typename V, typename MutexType>
bool LruCache<K, V, MutexType>::Put(const K& key, V&& value, std::chrono::milliseconds ttl) {
  return impl_->Put(key, std::move(value), ttl);
}

template <typename K, typename V, typename MutexType>
std::optional<V> LruCache<K, V, MutexType>::Get(const K& key) {
  return impl_->Get(key);
}

template <typename K, typename V, typename MutexType>
bool LruCache<K, V, MutexType>::Get(const K& key, V& value) {
  return impl_->Get(key, value);
}

template <typename K, typename V, typename MutexType>
bool LruCache<K, V, MutexType>::Remove(const K& key) {
  return impl_->Remove(key);
}

template <typename K, typename V, typename MutexType>
void LruCache<K, V, MutexType>::Clear() {
  impl_->Clear();
}

template <typename K, typename V, typename MutexType>
size_t LruCache<K, V, MutexType>::Size() const {
  return impl_->Size();
}

template <typename K, typename V, typename MutexType>
LruCacheStatisticsSnapshot LruCache<K, V, MutexType>::GetStatistics() const {
  return impl_->GetStatistics();
}

template <typename K, typename V, typename MutexType>
void LruCache<K, V, MutexType>::ResetStatistics() {
  impl_->ResetStatistics();
}

// Explicit instantiations for common types
template class LruCache<std::string, std::string>;
template class LruCache<int, std::string>;
template class LruCache<int, int>;
template class LruCache<uint64_t, std::string>;
template class LruCache<std::string, int>;
template class LruCache<std::string, NoncontiguousBuffer>;
template class LruCache<std::string, TrpcEndpointInfo>;
template class LruCache<std::string, std::vector<TrpcEndpointInfo>>;

}  // namespace trpc
