# MySQL client library for Bazel

licenses(["notice"])

package(default_visibility = ["//visibility:public"])

# System MySQL client library
cc_library(
    name = "mysql",
    srcs = [],
    hdrs = glob([]),
    copts = [
        "-I/usr/include/mysql",
    ],
    linkopts = [
        "-L/usr/lib/x86_64-linux-gnu",
        "-lmysqlclient",
        "-lzstd",
        "-lssl",
        "-lcrypto",
        "-lresolv",
        "-lm",
    ],
    visibility = ["//visibility:public"],
)
