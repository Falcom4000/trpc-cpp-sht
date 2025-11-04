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

#include "yaml-cpp/yaml.h"

#include "trpc/client/mysql/mysql_common.h"

namespace YAML {

template <>
struct convert<trpc::mysql::MysqlConnectionConfig> {
  static YAML::Node encode(const trpc::mysql::MysqlConnectionConfig& mysql_conf) {
    YAML::Node node;
    node["user"] = mysql_conf.user;
    node["password"] = mysql_conf.password;
    node["database"] = mysql_conf.database;
    node["charset"] = mysql_conf.charset;
    node["max_connections"] = mysql_conf.max_connections;
    node["min_idle_connections"] = mysql_conf.min_idle_connections;
    node["max_idle_time_ms"] = mysql_conf.max_idle_time_ms;
    node["connect_timeout_ms"] = mysql_conf.connect_timeout_ms;
    node["read_timeout_ms"] = mysql_conf.read_timeout_ms;
    node["write_timeout_ms"] = mysql_conf.write_timeout_ms;
    node["executor_thread_num"] = mysql_conf.executor_thread_num;
    return node;
  }

  static bool decode(const YAML::Node& node, trpc::mysql::MysqlConnectionConfig& mysql_conf) {  // NOLINT
    if (node["user"]) {
      mysql_conf.user = node["user"].as<std::string>();
    }
    if (node["password"]) {
      mysql_conf.password = node["password"].as<std::string>();
    }
    if (node["database"]) {
      mysql_conf.database = node["database"].as<std::string>();
    }
    if (node["charset"]) {
      mysql_conf.charset = node["charset"].as<std::string>();
    }
    if (node["max_connections"]) {
      mysql_conf.max_connections = node["max_connections"].as<uint32_t>();
    }
    if (node["min_idle_connections"]) {
      mysql_conf.min_idle_connections = node["min_idle_connections"].as<uint32_t>();
    }
    if (node["max_idle_time_ms"]) {
      mysql_conf.max_idle_time_ms = node["max_idle_time_ms"].as<uint32_t>();
    }
    if (node["connect_timeout_ms"]) {
      mysql_conf.connect_timeout_ms = node["connect_timeout_ms"].as<uint32_t>();
    }
    if (node["read_timeout_ms"]) {
      mysql_conf.read_timeout_ms = node["read_timeout_ms"].as<uint32_t>();
    }
    if (node["write_timeout_ms"]) {
      mysql_conf.write_timeout_ms = node["write_timeout_ms"].as<uint32_t>();
    }
    if (node["executor_thread_num"]) {
      mysql_conf.executor_thread_num = node["executor_thread_num"].as<uint32_t>();
    }
    return true;
  }
};

}  // namespace YAML

