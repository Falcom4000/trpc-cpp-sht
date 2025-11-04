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

#include "trpc/client/mysql/mysql_connection.h"

#include <chrono>
#include <thread>

#include "gtest/gtest.h"
#include "mysql/mysql.h"

namespace trpc::mysql::testing {

class MysqlConnectionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Initialize MySQL library
    mysql_library_init(0, nullptr, nullptr);
  }
  
  void TearDown() override {
    mysql_library_end();
  }
};

TEST_F(MysqlConnectionTest, Constructor) {
  MYSQL* mysql = mysql_init(nullptr);
  ASSERT_NE(mysql, nullptr);
  
  MysqlConnection conn(mysql);
  EXPECT_EQ(conn.GetRawConnection(), mysql);
  // Connection ID is 0 when not actually connected to MySQL server
  EXPECT_GE(conn.GetConnectionId(), 0u);
}

TEST_F(MysqlConnectionTest, IsValidWithNull) {
  MysqlConnection conn(nullptr);
  EXPECT_FALSE(conn.IsValid());
}

TEST_F(MysqlConnectionTest, UpdateLastUsedTime) {
  MYSQL* mysql = mysql_init(nullptr);
  ASSERT_NE(mysql, nullptr);
  
  MysqlConnection conn(mysql);
  auto time1 = conn.GetLastUsedTime();
  
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  conn.UpdateLastUsedTime();
  
  auto time2 = conn.GetLastUsedTime();
  EXPECT_GT(time2, time1);
}

TEST_F(MysqlConnectionTest, Reset) {
  MYSQL* mysql = mysql_init(nullptr);
  ASSERT_NE(mysql, nullptr);
  
  MysqlConnection conn(mysql);
  auto time1 = conn.GetLastUsedTime();
  
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  conn.Reset();
  
  auto time2 = conn.GetLastUsedTime();
  EXPECT_GT(time2, time1);
}

}  // namespace trpc::mysql::testing
