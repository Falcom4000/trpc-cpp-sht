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
#include <cppconn/driver.h>
#include <cppconn/exception.h>

namespace trpc::mysql::testing {

class MysqlConnectionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    driver_ = get_driver_instance();
  }
  
  void TearDown() override {
  }
  
  sql::Driver* driver_;
};

TEST_F(MysqlConnectionTest, Constructor) {
  // Create a connection (will fail if MySQL server is not running, but that's OK for unit test)
  sql::Connection* conn = nullptr;
  try {
    // Note: This test may fail if MySQL server is not available
    // In that case, we test with nullptr
    std::string url = "tcp://127.0.0.1:3306";
    std::string user = "test";
    std::string password = "test";
    conn = driver_->connect(url, user, password);
  } catch (const sql::SQLException& e) {
    // MySQL server not available, test with nullptr
    conn = nullptr;
  }
  
  if (conn) {
    MysqlConnection mysql_conn(conn);
    EXPECT_EQ(mysql_conn.GetRawConnection(), conn);
    // Connection ID should be > 0 when actually connected
    EXPECT_GT(mysql_conn.GetConnectionId(), 0u);
  } else {
    // Test with nullptr connection
    MysqlConnection mysql_conn(nullptr);
    EXPECT_EQ(mysql_conn.GetRawConnection(), nullptr);
    EXPECT_FALSE(mysql_conn.IsValid());
  }
}

TEST_F(MysqlConnectionTest, IsValidWithNull) {
  MysqlConnection conn(nullptr);
  EXPECT_FALSE(conn.IsValid());
}

TEST_F(MysqlConnectionTest, UpdateLastUsedTime) {
  sql::Connection* sql_conn = nullptr;
  try {
    std::string url = "tcp://127.0.0.1:3306";
    std::string user = "test";
    std::string password = "test";
    sql_conn = driver_->connect(url, user, password);
  } catch (const sql::SQLException& e) {
    // MySQL server not available, skip test
    GTEST_SKIP() << "MySQL server not available";
  }
  
  if (sql_conn) {
    MysqlConnection conn(sql_conn);
    auto time1 = conn.GetLastUsedTime();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    conn.UpdateLastUsedTime();
    
    auto time2 = conn.GetLastUsedTime();
    EXPECT_GT(time2, time1);
  }
}

TEST_F(MysqlConnectionTest, Reset) {
  sql::Connection* sql_conn = nullptr;
  try {
    std::string url = "tcp://127.0.0.1:3306";
    std::string user = "test";
    std::string password = "test";
    sql_conn = driver_->connect(url, user, password);
  } catch (const sql::SQLException& e) {
    // MySQL server not available, skip test
    GTEST_SKIP() << "MySQL server not available";
  }
  
  if (sql_conn) {
    MysqlConnection conn(sql_conn);
    auto time1 = conn.GetLastUsedTime();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    conn.Reset();
    
    auto time2 = conn.GetLastUsedTime();
    EXPECT_GT(time2, time1);
  }
}

}  // namespace trpc::mysql::testing
