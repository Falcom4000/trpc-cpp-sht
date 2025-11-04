# MySQL client plugin for tRPC-Cpp

licenses(["notice"])

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "mysql_common",
    hdrs = ["mysql_common.h"],
    deps = [],
)

cc_library(
    name = "mysql_connection",
    srcs = ["mysql_connection.cc"],
    hdrs = ["mysql_connection.h"],
    deps = [
        ":mysql_common",
        "//trpc/util/log:logging",
        "@com_github_mysql_mysql//:mysqlcppconn",
    ],
)

cc_library(
    name = "mysql_connection_pool",
    srcs = ["mysql_connection_pool.cc"],
    hdrs = ["mysql_connection_pool.h"],
    deps = [
        ":mysql_common",
        ":mysql_connection",
        "//trpc/util/log:logging",
        "@com_github_mysql_mysql//:mysqlcppconn",
    ],
)

cc_library(
    name = "mysql_executor",
    srcs = ["mysql_executor.cc"],
    hdrs = ["mysql_executor.h"],
    deps = [
        ":mysql_common",
        ":mysql_connection",
        "//trpc/common/future",
        "//trpc/common:status",
        "//trpc/coroutine:fiber",
        "//trpc/coroutine:fiber_basic",
        "//trpc/coroutine:future",
        "//trpc/future:future_utility",
        "//trpc/util/log:logging",
        "//trpc/util/thread:sq_thread_pool",
        "//trpc/util/thread:thread_pool",
        "//trpc/util/thread:thread_pool_option",
        "@com_github_mysql_mysql//:mysqlcppconn",
    ],
)

cc_library(
    name = "mysql_service_proxy",
    srcs = ["mysql_service_proxy.cc"],
    hdrs = ["mysql_service_proxy.h"],
    deps = [
        ":mysql_common",
        ":mysql_connection_pool",
        ":mysql_executor",
        "//trpc/client:service_proxy",
        "//trpc/common/future",
        "//trpc/common:status",
        "//trpc/util/log:logging",
    ],
)

cc_test(
    name = "mysql_connection_test",
    srcs = ["mysql_connection_test.cc"],
    deps = [
        ":mysql_connection",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_github_mysql_mysql//:mysqlcppconn",
    ],
)

cc_test(
    name = "mysql_connection_pool_test",
    srcs = ["mysql_connection_pool_test.cc"],
    deps = [
        ":mysql_connection_pool",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "mysql_executor_test",
    srcs = ["mysql_executor_test.cc"],
    deps = [
        ":mysql_executor",
        ":mysql_connection",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@com_github_mysql_mysql//:mysqlcppconn",
    ],
)

cc_test(
    name = "mysql_service_proxy_test",
    srcs = ["mysql_service_proxy_test.cc"],
    deps = [
        ":mysql_service_proxy",
        "//trpc/client:make_client_context",
        "//trpc/client:service_proxy_option",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)


