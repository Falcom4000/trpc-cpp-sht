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

#include <thread>
#include <vector>

#include "gtest/gtest.h"

#include "trpc/coroutine/fiber.h"
#include "trpc/coroutine/fiber_latch.h"
#include "trpc/coroutine/testing/fiber_runtime.h"

namespace trpc::testing {

// Test basic Put and Get operations
TEST(LruCacheTest, BasicPutAndGet) {
  LruCache<std::string, std::string> cache(10);

  // Put some items
  EXPECT_TRUE(cache.Put("key1", "value1"));
  EXPECT_TRUE(cache.Put("key2", "value2"));
  EXPECT_TRUE(cache.Put("key3", "value3"));

  // Get items
  auto val1 = cache.Get("key1");
  EXPECT_TRUE(val1.has_value());
  EXPECT_EQ(*val1, "value1");

  auto val2 = cache.Get("key2");
  EXPECT_TRUE(val2.has_value());
  EXPECT_EQ(*val2, "value2");

  // Get non-existent item
  auto val_none = cache.Get("nonexistent");
  EXPECT_FALSE(val_none.has_value());

  EXPECT_EQ(cache.Size(), 3);
}

// Test output parameter version of Get
TEST(LruCacheTest, GetWithOutputParameter) {
  LruCache<int, std::string> cache(10);

  cache.Put(1, "one");
  cache.Put(2, "two");

  std::string value;
  EXPECT_TRUE(cache.Get(1, value));
  EXPECT_EQ(value, "one");

  EXPECT_TRUE(cache.Get(2, value));
  EXPECT_EQ(value, "two");

  EXPECT_FALSE(cache.Get(999, value));
}

// Test move semantics
TEST(LruCacheTest, MoveSemantic) {
  LruCache<int, std::string> cache(10);

  std::string val = "movable_value";
  EXPECT_TRUE(cache.Put(1, std::move(val)));

  auto result = cache.Get(1);
  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(*result, "movable_value");
}

// Test update existing key
TEST(LruCacheTest, UpdateExistingKey) {
  LruCache<std::string, int> cache(10);

  cache.Put("key", 100);
  auto val = cache.Get("key");
  EXPECT_TRUE(val.has_value());
  EXPECT_EQ(*val, 100);

  // Update with new value
  cache.Put("key", 200);
  val = cache.Get("key");
  EXPECT_TRUE(val.has_value());
  EXPECT_EQ(*val, 200);

  EXPECT_EQ(cache.Size(), 1);
}

// Test LRU eviction
TEST(LruCacheTest, LruEviction) {
  LruCache<int, std::string> cache(3);  // Capacity of 3

  cache.Put(1, "one");
  cache.Put(2, "two");
  cache.Put(3, "three");
  EXPECT_EQ(cache.Size(), 3);

  // Access key 1 to make it recently used
  auto val = cache.Get(1);
  EXPECT_TRUE(val.has_value());

  // Add a new item, should evict key 2 (least recently used)
  cache.Put(4, "four");
  EXPECT_EQ(cache.Size(), 3);

  // Key 2 should be evicted
  EXPECT_FALSE(cache.Get(2).has_value());

  // Keys 1, 3, 4 should still exist
  EXPECT_TRUE(cache.Get(1).has_value());
  EXPECT_TRUE(cache.Get(3).has_value());
  EXPECT_TRUE(cache.Get(4).has_value());
}

// Test Remove operation
TEST(LruCacheTest, Remove) {
  LruCache<std::string, std::string> cache(10);

  cache.Put("key1", "value1");
  cache.Put("key2", "value2");
  EXPECT_EQ(cache.Size(), 2);

  EXPECT_TRUE(cache.Remove("key1"));
  EXPECT_EQ(cache.Size(), 1);
  EXPECT_FALSE(cache.Get("key1").has_value());

  // Remove non-existent key
  EXPECT_FALSE(cache.Remove("nonexistent"));
  EXPECT_EQ(cache.Size(), 1);
}

// Test Clear operation
TEST(LruCacheTest, Clear) {
  LruCache<int, std::string> cache(10);

  cache.Put(1, "one");
  cache.Put(2, "two");
  cache.Put(3, "three");
  EXPECT_EQ(cache.Size(), 3);

  cache.Clear();
  EXPECT_EQ(cache.Size(), 0);
  EXPECT_FALSE(cache.Get(1).has_value());
  EXPECT_FALSE(cache.Get(2).has_value());
  EXPECT_FALSE(cache.Get(3).has_value());
}

// Test TTL expiration
TEST(LruCacheTest, TtlExpiration) {
  LruCacheConfig config;
  config.capacity = 10;
  config.enable_ttl = true;
  config.default_ttl = std::chrono::milliseconds(100);

  LruCache<std::string, std::string> cache(config);

  // Put item with 100ms TTL
  cache.Put("key1", "value1", std::chrono::milliseconds(100));

  // Should be available immediately
  auto val = cache.Get("key1");
  EXPECT_TRUE(val.has_value());
  EXPECT_EQ(*val, "value1");

  // Wait for expiration
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  // Should be expired now
  val = cache.Get("key1");
  EXPECT_FALSE(val.has_value());
}

// Test statistics
TEST(LruCacheTest, Statistics) {
  LruCacheConfig config;
  config.capacity = 3;
  config.enable_statistics = true;

  LruCache<int, std::string> cache(config);

  cache.Put(1, "one");
  cache.Put(2, "two");
  cache.Put(3, "three");

  // Hit
  cache.Get(1);
  cache.Get(2);

  // Miss
  cache.Get(999);

  // Eviction
  cache.Put(4, "four");

  auto stats = cache.GetStatistics();
  EXPECT_EQ(stats.total_puts, 4);
  EXPECT_EQ(stats.total_gets, 3);
  EXPECT_EQ(stats.total_hits, 2);
  EXPECT_EQ(stats.total_misses, 1);
  EXPECT_EQ(stats.total_evictions, 1);
  EXPECT_GT(stats.GetHitRate(), 0.6);
  EXPECT_LT(stats.GetHitRate(), 0.7);

  // Reset statistics
  cache.ResetStatistics();
  stats = cache.GetStatistics();
  EXPECT_EQ(stats.total_puts, 0);
  EXPECT_EQ(stats.total_gets, 0);
}

// Test thread safety
TEST(LruCacheTest, ThreadSafety) {
  LruCache<int, int> cache(1000);

  const int num_threads = 10;
  const int operations_per_thread = 1000;

  std::vector<std::thread> threads;
  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([&cache, t, operations_per_thread]() {
      for (int i = 0; i < operations_per_thread; ++i) {
        int key = t * operations_per_thread + i;
        cache.Put(key, key * 10);
        auto val = cache.Get(key);
        if (val.has_value()) {
          EXPECT_EQ(*val, key * 10);
        }
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  auto stats = cache.GetStatistics();
  EXPECT_GT(stats.total_puts, 0);
  EXPECT_GT(stats.total_gets, 0);
}

// Test with fiber runtime
TEST(LruCacheTest, FiberSafety) {
  RunAsFiber([]() {
    LruCache<std::string, std::string> cache(100);

    const int num_fibers = 10;
    const int operations_per_fiber = 100;

    FiberLatch latch(num_fibers);

    for (int f = 0; f < num_fibers; ++f) {
      StartFiberDetached([&cache, &latch, f, operations_per_fiber]() {
        for (int i = 0; i < operations_per_fiber; ++i) {
          std::string key = "fiber_" + std::to_string(f) + "_key_" + std::to_string(i);
          std::string value = "value_" + std::to_string(i);
          cache.Put(key, value);

          auto val = cache.Get(key);
          if (val.has_value()) {
            EXPECT_EQ(*val, value);
          }
        }
        latch.CountDown();
      });
    }

    latch.Wait();

    auto stats = cache.GetStatistics();
    EXPECT_GT(stats.total_puts, 0);
    EXPECT_GT(stats.total_gets, 0);
  });
}

// Test capacity
TEST(LruCacheTest, Capacity) {
  LruCache<int, int> cache(100);
  EXPECT_EQ(cache.Capacity(), 100);

  LruCacheConfig config;
  config.capacity = 200;
  LruCache<int, int> cache2(config);
  EXPECT_EQ(cache2.Capacity(), 200);
}

}  // namespace trpc::testing
