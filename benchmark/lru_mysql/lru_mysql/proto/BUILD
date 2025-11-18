package(default_visibility = ["//visibility:public"])

load("@trpc_cpp//trpc:trpc.bzl", "trpc_proto_library")

trpc_proto_library(
    name = "lru_mysql_proto",
    srcs = ["lru_mysql.proto"],
    rootpath = "@trpc_cpp",
    use_trpc_plugin = True,
)
