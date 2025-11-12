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

}  // namespace trpc::mysql::testing
