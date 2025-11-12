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
  
  {
    auto handle = pool.Borrow(1000);
    ASSERT_TRUE(handle);
    auto conn = handle.Get();
    ASSERT_NE(conn, nullptr);
    EXPECT_TRUE(conn->IsValid());
    
    auto stats = pool.GetStats();
    EXPECT_EQ(stats.active_count, 1u);
  }
  // Connection automatically returned when handle goes out of scope
  
  auto stats = pool.GetStats();
  EXPECT_EQ(stats.active_count, 0u);
  EXPECT_GT(stats.idle_count, 0u);
  
  pool.Stop();
}

TEST_F(MysqlConnectionPoolTest, GetConnectionTimeout) {
  config_.max_connections = 1;
  MysqlConnectionPool pool(config_);
  ASSERT_TRUE(pool.Start());
  
  auto handle1 = pool.Borrow(1000);
  ASSERT_TRUE(handle1);
  
  // Try to get another connection when pool is exhausted
  auto handle2 = pool.Borrow(100);
  EXPECT_FALSE(handle2);  // Should timeout
  
  // Connection automatically returned when handle1 goes out of scope
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
        auto handle = pool.Borrow(1000);
        if (handle) {
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
          // Connection automatically returned when handle goes out of scope
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
