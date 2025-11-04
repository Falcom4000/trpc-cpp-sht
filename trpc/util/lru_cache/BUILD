licenses(["notice"])

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "lru_cache_statistics",
    hdrs = ["lru_cache_statistics.h"],
)

cc_library(
    name = "lru_node",
    hdrs = ["detail/lru_node.h"],
)

cc_library(
    name = "adaptive_mutex",
    hdrs = ["detail/adaptive_mutex.h"],
    deps = [
        "//trpc/coroutine:fiber",
    ],
)

cc_library(
    name = "lru_cache",
    srcs = ["lru_cache.cc"],
    hdrs = ["lru_cache.h"],
    deps = [
        ":adaptive_mutex",
        ":lru_cache_statistics",
        ":lru_node",
        "//trpc/naming/common:common_defs",
        "//trpc/util/buffer:noncontiguous_buffer",
    ],
)

cc_test(
    name = "lru_cache_test",
    srcs = ["lru_cache_test.cc"],
    deps = [
        ":lru_cache",
        "//trpc/coroutine:fiber",
        "//trpc/coroutine/testing:fiber_runtime_test",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)
