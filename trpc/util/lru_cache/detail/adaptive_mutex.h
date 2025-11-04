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

#include <mutex>

#include "trpc/coroutine/fiber_mutex.h"
#include "trpc/runtime/threadmodel/fiber/detail/fiber_entity.h"

namespace trpc::lru_cache::detail {

/// @brief Adaptive mutex that automatically selects between FiberMutex and std::mutex
///        based on the runtime environment (fiber or thread context)
class AdaptiveMutex {
 public:
  AdaptiveMutex() = default;
  ~AdaptiveMutex() = default;

  /// @brief Lock the mutex
  void lock() {
    if (IsInFiberContext()) {
      fiber_mutex_.lock();
    } else {
      thread_mutex_.lock();
    }
  }

  /// @brief Unlock the mutex
  void unlock() {
    if (IsInFiberContext()) {
      fiber_mutex_.unlock();
    } else {
      thread_mutex_.unlock();
    }
  }

  /// @brief Try to lock the mutex
  /// @return true if lock acquired, false otherwise
  bool try_lock() {
    if (IsInFiberContext()) {
      return fiber_mutex_.try_lock();
    }
    return thread_mutex_.try_lock();
  }

 private:
  /// @brief Check if we're in a fiber context
  static bool IsInFiberContext() {
    return trpc::fiber::detail::IsFiberContextPresent();
  }

  FiberMutex fiber_mutex_;
  std::mutex thread_mutex_;
};

}  // namespace trpc::lru_cache::detail
