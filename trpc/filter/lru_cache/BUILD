licenses(["notice"])

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "lru_cache_client_filter",
    srcs = ["lru_cache_client_filter.cc"],
    hdrs = ["lru_cache_client_filter.h"],
    deps = [
        "//trpc/client:client_context",
        "//trpc/codec:codec_manager",
        "//trpc/common:status",
        "//trpc/filter:client_filter_base",
        "//trpc/util/buffer:noncontiguous_buffer",
        "//trpc/util/log:logging",
        "//trpc/util/lru_cache:lru_cache",
    ],
)

cc_test(
    name = "lru_cache_client_filter_test",
    srcs = ["lru_cache_client_filter_test.cc"],
    deps = [
        ":lru_cache_client_filter",
        "//trpc/client:client_context",
        "//trpc/codec/trpc:trpc_client_codec",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)
