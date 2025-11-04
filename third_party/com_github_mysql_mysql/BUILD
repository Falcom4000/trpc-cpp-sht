# MySQL client library for Bazel

licenses(["notice"])

package(default_visibility = ["//visibility:public"])

# System MySQL client library (C API)
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

# MySQL Connector/C++ library
cc_library(
    name = "mysqlcppconn",
    srcs = [],
    hdrs = glob([]),
    copts = [
        "-I/usr/include/cppconn",
        "-I/usr/include/mysql-cppconn",
    ],
    linkopts = [
        "-L/usr/lib/x86_64-linux-gnu",
        "-lmysqlcppconn",
    ],
    visibility = ["//visibility:public"],
)
