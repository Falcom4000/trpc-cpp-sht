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

namespace trpc::lru_cache::detail {

/// @brief Node structure for LRU cache doubly-linked list
/// @tparam K Key type
/// @tparam V Value type
template <typename K, typename V>
struct LruNode {
  K key;
  V value;
  std::chrono::steady_clock::time_point expire_time;
  LruNode* prev{nullptr};
  LruNode* next{nullptr};

  /// @brief Check if the node has expired
  /// @return true if expired, false otherwise
  bool IsExpired() const {
    if (expire_time == std::chrono::steady_clock::time_point::max()) {
      return false;  // Never expires
    }
    return std::chrono::steady_clock::now() >= expire_time;
  }

  /// @brief Constructor
  LruNode() = default;
  
  /// @brief Constructor with key and value
  LruNode(const K& k, const V& v)
      : key(k), value(v), expire_time(std::chrono::steady_clock::time_point::max()) {}
};

}  // namespace trpc::lru_cache::detail
