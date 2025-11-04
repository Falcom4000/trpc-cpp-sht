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
#include <cppconn/driver.h>
#include <cppconn/exception.h>

#include "trpc/client/mysql/mysql_connection.h"

namespace trpc::mysql::testing {

class MysqlExecutorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    executor_ = std::make_unique<MysqlExecutor>(2);
    driver_ = get_driver_instance();
  }
  
  void TearDown() override {
    executor_.reset();
  }
  
  std::unique_ptr<MysqlExecutor> executor_;
  sql::Driver* driver_;
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
    auto conn = std::make_shared<MysqlConnection>(sql_conn);
    
    MysqlRequest request;
    request.sql = "SELECT 1";
    
    auto status = executor_->SubmitAndWait(conn, request, nullptr, 1000);
    EXPECT_FALSE(status.OK());
  }
  
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
