cc_binary(
    name = "server_bin",
    deps = [
        ":server",
    ],
)

cc_library(
    name = "server",
    srcs = ["server.cc"],
    hdrs = ["server.h"],
    deps = [
        ":service",
        "@com_github_fmtlib_fmt//:fmtlib",
        "@trpc_cpp//trpc/client:make_client_context",
        "@trpc_cpp//trpc/client:trpc_client",
        "@trpc_cpp//trpc/client/mysql:mysql_service_proxy",
        "@trpc_cpp//trpc/common:trpc_app",
        "@trpc_cpp//trpc/log:trpc_log",
    ],
)

cc_library(
    name = "service",
    srcs = ["service.cc"],
    hdrs = ["service.h"],
    deps = [
        "@proto//:lru_mysql_proto",
        "@trpc_cpp//trpc/client:make_client_context",
        "@trpc_cpp//trpc/client:trpc_client",
        "@trpc_cpp//trpc/client/mysql:mysql_service_proxy",
        "@trpc_cpp//trpc/log:trpc_log",
    ],
)
