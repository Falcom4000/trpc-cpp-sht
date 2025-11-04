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

#include <atomic>
#include <cstdint>

namespace trpc {

/// @brief Statistics for LRU cache operations
/// @note This struct contains atomic members and cannot be copied.
///       Use GetSnapshot() to get a copyable snapshot of current values.
struct LruCacheStatistics {
  std::atomic<uint64_t> total_gets{0};         // Total number of Get operations
  std::atomic<uint64_t> total_hits{0};         // Number of cache hits
  std::atomic<uint64_t> total_misses{0};       // Number of cache misses
  std::atomic<uint64_t> total_puts{0};         // Total number of Put operations
  std::atomic<uint64_t> total_evictions{0};    // Number of evictions due to capacity
  std::atomic<uint64_t> total_expirations{0};  // Number of evictions due to expiration

  /// @brief Calculate hit rate
  /// @return Hit rate as a percentage (0.0 to 1.0)
  double GetHitRate() const {
    uint64_t gets = total_gets.load(std::memory_order_relaxed);
    if (gets == 0) return 0.0;
    uint64_t hits = total_hits.load(std::memory_order_relaxed);
    return static_cast<double>(hits) / static_cast<double>(gets);
  }

  /// @brief Reset all statistics
  void Reset() {
    total_gets.store(0, std::memory_order_relaxed);
    total_hits.store(0, std::memory_order_relaxed);
    total_misses.store(0, std::memory_order_relaxed);
    total_puts.store(0, std::memory_order_relaxed);
    total_evictions.store(0, std::memory_order_relaxed);
    total_expirations.store(0, std::memory_order_relaxed);
  }
};

/// @brief Snapshot of LRU cache statistics (copyable)
struct LruCacheStatisticsSnapshot {
  uint64_t total_gets{0};
  uint64_t total_hits{0};
  uint64_t total_misses{0};
  uint64_t total_puts{0};
  uint64_t total_evictions{0};
  uint64_t total_expirations{0};

  /// @brief Calculate hit rate
  /// @return Hit rate as a percentage (0.0 to 1.0)
  double GetHitRate() const {
    if (total_gets == 0) return 0.0;
    return static_cast<double>(total_hits) / static_cast<double>(total_gets);
  }
};

}  // namespace trpc
