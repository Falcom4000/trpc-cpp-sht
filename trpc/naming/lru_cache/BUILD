licenses(["notice"])

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "lru_cache_selector",
    srcs = ["lru_cache_selector.cc"],
    hdrs = ["lru_cache_selector.h"],
    deps = [
        "//trpc/common/future",
        "//trpc/naming:selector",
        "//trpc/naming/common:common_defs",
        "//trpc/util/log:logging",
        "//trpc/util/lru_cache:lru_cache",
    ],
)

cc_library(
    name = "lru_polaris_selector",
    srcs = ["lru_polaris_selector.cc"],
    hdrs = ["lru_polaris_selector.h"],
    deps = [
        ":lru_cache_selector",
        "//trpc/naming:selector_factory",
        "//trpc/util/log:logging",
    ],
)

cc_test(
    name = "lru_cache_selector_test",
    srcs = ["lru_cache_selector_test.cc"],
    deps = [
        ":lru_cache_selector",
        "//trpc/client:client_context",
        "//trpc/codec/trpc:trpc_client_codec",
        "//trpc/naming/common:common_defs",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)
