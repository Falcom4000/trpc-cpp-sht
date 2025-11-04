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

#include "trpc/client/mysql/mysql_executor.h"

#include "gtest/gtest.h"
#include "mysql/mysql.h"

#include "trpc/client/mysql/mysql_connection.h"

namespace trpc::mysql::testing {

class MysqlExecutorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    mysql_library_init(0, nullptr, nullptr);
    executor_ = std::make_unique<MysqlExecutor>(2);
  }
  
  void TearDown() override {
    executor_.reset();
    mysql_library_end();
  }
  
  std::unique_ptr<MysqlExecutor> executor_;
};

TEST_F(MysqlExecutorTest, Constructor) {
  EXPECT_NE(executor_, nullptr);
}

TEST_F(MysqlExecutorTest, StartAndStop) {
  EXPECT_TRUE(executor_->Start());
  executor_->Stop();
}

TEST_F(MysqlExecutorTest, StopWithoutStart) {
  executor_->Stop();  // Should not crash
}

TEST_F(MysqlExecutorTest, SubmitAndWaitWithNullConnection) {
  ASSERT_TRUE(executor_->Start());
  
  MysqlRequest request;
  request.sql = "SELECT 1";
  MysqlResultSet result;
  
  auto status = executor_->SubmitAndWait(nullptr, request, &result, 1000);
  EXPECT_FALSE(status.OK());
  
  executor_->Stop();
}

TEST_F(MysqlExecutorTest, SubmitAndWaitWithNullResult) {
  ASSERT_TRUE(executor_->Start());
  
  MYSQL* mysql = mysql_init(nullptr);
  ASSERT_NE(mysql, nullptr);
  auto conn = std::make_shared<MysqlConnection>(mysql);
  
  MysqlRequest request;
  request.sql = "SELECT 1";
  
  auto status = executor_->SubmitAndWait(conn, request, nullptr, 1000);
  EXPECT_FALSE(status.OK());
  
  executor_->Stop();
}

TEST_F(MysqlExecutorTest, SubmitAsyncWithNullConnection) {
  ASSERT_TRUE(executor_->Start());
  
  MysqlRequest request;
  request.sql = "SELECT 1";
  
  auto future = executor_->SubmitAsync(nullptr, request);
  EXPECT_TRUE(future.IsFailed());
  
  executor_->Stop();
}

// Note: The following tests require an actual MySQL server running
// They are commented out by default but can be enabled for integration testing

/*
TEST_F(MysqlExecutorTest, ExecuteSimpleQuery) {
  ASSERT_TRUE(executor_->Start());
  
  // Create a real MySQL connection
  MYSQL* mysql = mysql_init(nullptr);
  ASSERT_NE(mysql, nullptr);
  
  MYSQL* result = mysql_real_connect(mysql, "127.0.0.1", "test_user", "test_password",
                                    "test_db", 3306, nullptr, 0);
  ASSERT_NE(result, nullptr);
  
  auto conn = std::make_shared<MysqlConnection>(mysql);
  
  MysqlRequest request;
  request.sql = "SELECT 1 AS num";
  MysqlResultSet result_set;
  
  auto status = executor_->SubmitAndWait(conn, request, &result_set, 5000);
  EXPECT_TRUE(status.OK());
  EXPECT_EQ(result_set.field_count, 1U);
  EXPECT_EQ(result_set.field_names.size(), 1U);
  EXPECT_EQ(result_set.field_names[0], "num");
  EXPECT_EQ(result_set.rows.size(), 1U);
  EXPECT_EQ(result_set.rows[0][0], "1");
  
  executor_->Stop();
}

TEST_F(MysqlExecutorTest, AsyncExecuteQuery) {
  ASSERT_TRUE(executor_->Start());
  
  MYSQL* mysql = mysql_init(nullptr);
  ASSERT_NE(mysql, nullptr);
  
  MYSQL* result = mysql_real_connect(mysql, "127.0.0.1", "test_user", "test_password",
                                    "test_db", 3306, nullptr, 0);
  ASSERT_NE(result, nullptr);
  
  auto conn = std::make_shared<MysqlConnection>(mysql);
  
  MysqlRequest request;
  request.sql = "SELECT 1 AS num";
  
  auto future = executor_->SubmitAsync(conn, request);
  
  // Wait for result
  auto result_future = future::BlockingTryGet(std::move(future), 5000);
  ASSERT_TRUE(result_future.has_value());
  
  auto result_set = result_future->GetValue0();
  EXPECT_EQ(result_set.field_count, 1U);
  EXPECT_EQ(result_set.rows.size(), 1U);
  
  executor_->Stop();
}
*/

}  // namespace trpc::mysql::testing
