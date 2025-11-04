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

#include "trpc/client/mysql/mysql_connection_pool.h"

#include "gtest/gtest.h"

namespace trpc::mysql::testing {

class MysqlConnectionPoolTest : public ::testing::Test {
 protected:
  void SetUp() override {
    mysql_library_init(0, nullptr, nullptr);
    
    config_.host = "127.0.0.1";
    config_.port = 3306;
    config_.user = "test_user";
    config_.password = "test_password";
    config_.database = "test_db";
    config_.max_connections = 10;
    config_.min_idle_connections = 2;
    config_.max_idle_time_ms = 5000;
  }
  
  void TearDown() override {
    mysql_library_end();
  }
  
  MysqlConnectionConfig config_;
};

TEST_F(MysqlConnectionPoolTest, Constructor) {
  MysqlConnectionPool pool(config_);
  auto stats = pool.GetStats();
  EXPECT_EQ(stats.idle_count, 0u);
  EXPECT_EQ(stats.active_count, 0u);
  EXPECT_EQ(stats.total_count, 0u);
}

TEST_F(MysqlConnectionPoolTest, StartWithInvalidMaxConnections) {
  config_.max_connections = 0;
  MysqlConnectionPool pool(config_);
  EXPECT_FALSE(pool.Start());
}

TEST_F(MysqlConnectionPoolTest, GetStats) {
  MysqlConnectionPool pool(config_);
  auto stats = pool.GetStats();
  
  EXPECT_EQ(stats.idle_count, 0u);
  EXPECT_EQ(stats.active_count, 0u);
  EXPECT_EQ(stats.total_count, 0u);
}

TEST_F(MysqlConnectionPoolTest, StopBeforeStart) {
  MysqlConnectionPool pool(config_);
  pool.Stop();  // Should not crash
  
  auto stats = pool.GetStats();
  EXPECT_EQ(stats.total_count, 0u);
}

TEST_F(MysqlConnectionPoolTest, MultipleStopCalls) {
  MysqlConnectionPool pool(config_);
  pool.Stop();
  pool.Stop();  // Second stop should be safe
  
  auto stats = pool.GetStats();
  EXPECT_EQ(stats.total_count, 0u);
}

// Note: The following tests require an actual MySQL server running
// They are commented out by default but can be enabled for integration testing

/*
TEST_F(MysqlConnectionPoolTest, StartSuccess) {
  MysqlConnectionPool pool(config_);
  EXPECT_TRUE(pool.Start());
  
  auto stats = pool.GetStats();
  EXPECT_EQ(stats.idle_count, config_.min_idle_connections);
  EXPECT_EQ(stats.active_count, 0u);
  
  pool.Stop();
}

TEST_F(MysqlConnectionPoolTest, GetAndReturnConnection) {
  MysqlConnectionPool pool(config_);
  ASSERT_TRUE(pool.Start());
  
  auto conn = pool.GetConnection(1000);
  ASSERT_NE(conn, nullptr);
  EXPECT_TRUE(conn->IsValid());
  
  auto stats = pool.GetStats();
  EXPECT_EQ(stats.active_count, 1u);
  
  pool.ReturnConnection(conn);
  
  stats = pool.GetStats();
  EXPECT_EQ(stats.active_count, 0u);
  EXPECT_GT(stats.idle_count, 0u);
  
  pool.Stop();
}

TEST_F(MysqlConnectionPoolTest, GetConnectionTimeout) {
  config_.max_connections = 1;
  MysqlConnectionPool pool(config_);
  ASSERT_TRUE(pool.Start());
  
  auto conn1 = pool.GetConnection(1000);
  ASSERT_NE(conn1, nullptr);
  
  // Try to get another connection when pool is exhausted
  auto conn2 = pool.GetConnection(100);
  EXPECT_EQ(conn2, nullptr);  // Should timeout
  
  pool.ReturnConnection(conn1);
  pool.Stop();
}

TEST_F(MysqlConnectionPoolTest, ConcurrentGetAndReturn) {
  MysqlConnectionPool pool(config_);
  ASSERT_TRUE(pool.Start());
  
  const int thread_count = 5;
  const int iterations = 10;
  std::vector<std::thread> threads;
  
  for (int i = 0; i < thread_count; ++i) {
    threads.emplace_back([&pool]() {
      for (int j = 0; j < iterations; ++j) {
        auto conn = pool.GetConnection(1000);
        if (conn) {
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
          pool.ReturnConnection(conn);
        }
      }
    });
  }
  
  for (auto& thread : threads) {
    thread.join();
  }
  
  auto stats = pool.GetStats();
  EXPECT_EQ(stats.active_count, 0u);
  EXPECT_GT(stats.idle_count, 0u);
  
  pool.Stop();
}
*/

}  // namespace trpc::mysql::testing
