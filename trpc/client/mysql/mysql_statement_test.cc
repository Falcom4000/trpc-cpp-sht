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

#include "trpc/client/mysql/mysql_statement.h"

#include "gtest/gtest.h"

namespace trpc::mysql::testing {

class MysqlStatementTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

// Test MysqlValue basic types
TEST_F(MysqlStatementTest, MysqlValueBasicTypes) {
  // Null value
  auto null_val = MysqlValue::Null();
  EXPECT_TRUE(null_val.IsNull());
  EXPECT_EQ(null_val.GetType(), MysqlDataType::kNull);
  EXPECT_EQ(null_val.ToString(), "NULL");
  
  // Integer types
  MysqlValue int8_val(static_cast<int8_t>(127));
  EXPECT_FALSE(int8_val.IsNull());
  EXPECT_EQ(int8_val.GetType(), MysqlDataType::kTinyInt);
  EXPECT_EQ(int8_val.ToString(), "127");
  
  MysqlValue int16_val(static_cast<int16_t>(32767));
  EXPECT_EQ(int16_val.GetType(), MysqlDataType::kSmallInt);
  EXPECT_EQ(int16_val.ToString(), "32767");
  
  MysqlValue int32_val(2147483647);
  EXPECT_EQ(int32_val.GetType(), MysqlDataType::kInt);
  EXPECT_EQ(int32_val.ToString(), "2147483647");
  
  MysqlValue int64_val(static_cast<int64_t>(9223372036854775807));
  EXPECT_EQ(int64_val.GetType(), MysqlDataType::kBigInt);
  
  // Float types
  MysqlValue float_val(3.14f);
  EXPECT_EQ(float_val.GetType(), MysqlDataType::kFloat);
  
  MysqlValue double_val(3.141592653589793);
  EXPECT_EQ(double_val.GetType(), MysqlDataType::kDouble);
  
  // String type
  MysqlValue string_val(std::string("hello"));
  EXPECT_EQ(string_val.GetType(), MysqlDataType::kString);
  EXPECT_EQ(string_val.ToString(), "hello");
  
  MysqlValue cstr_val("world");
  EXPECT_EQ(cstr_val.GetType(), MysqlDataType::kString);
  EXPECT_EQ(cstr_val.ToString(), "world");
  
  // Binary type
  std::vector<uint8_t> binary_data = {0x01, 0x02, 0x03, 0x04};
  MysqlValue binary_val(binary_data);
  EXPECT_EQ(binary_val.GetType(), MysqlDataType::kBlob);
  EXPECT_EQ(binary_val.ToString(), "<binary_data>");
}

// Test MysqlStatement basic usage
TEST_F(MysqlStatementTest, StatementBasicUsage) {
  MysqlStatement stmt("SELECT * FROM users WHERE id = ?");
  EXPECT_EQ(stmt.GetSql(), "SELECT * FROM users WHERE id = ?");
  EXPECT_FALSE(stmt.HasParams());
  
  stmt.Bind(MysqlValue(123));
  EXPECT_TRUE(stmt.HasParams());
  EXPECT_EQ(stmt.GetParams().size(), 1);
  
  auto params = stmt.GetParamsAsStrings();
  EXPECT_EQ(params.size(), 1);
  EXPECT_EQ(params[0], "123");
}

// Test MysqlStatement parameter binding
TEST_F(MysqlStatementTest, StatementParameterBinding) {
  MysqlStatement stmt("INSERT INTO users (name, age, email) VALUES (?, ?, ?)");
  
  stmt.Bind(MysqlValue("Alice"))
      .Bind(MysqlValue(25))
      .Bind(MysqlValue("alice@example.com"));
  
  EXPECT_EQ(stmt.GetParams().size(), 3);
  
  auto params = stmt.GetParamsAsStrings();
  EXPECT_EQ(params[0], "Alice");
  EXPECT_EQ(params[1], "25");
  EXPECT_EQ(params[2], "alice@example.com");
}

// Test MysqlStatement position binding
TEST_F(MysqlStatementTest, StatementPositionBinding) {
  MysqlStatement stmt("UPDATE users SET name = ?, age = ? WHERE id = ?");
  
  stmt.Bind(1, MysqlValue("Bob"))
      .Bind(2, MysqlValue(30))
      .Bind(3, MysqlValue(456));
  
  EXPECT_EQ(stmt.GetParams().size(), 3);
  
  auto params = stmt.GetParamsAsStrings();
  EXPECT_EQ(params[0], "Bob");
  EXPECT_EQ(params[1], "30");
  EXPECT_EQ(params[2], "456");
}

// Test MysqlStatement clear parameters
TEST_F(MysqlStatementTest, StatementClearParams) {
  MysqlStatement stmt("SELECT * FROM users WHERE id = ?");
  stmt.Bind(MysqlValue(123));
  
  EXPECT_TRUE(stmt.HasParams());
  
  stmt.ClearParams();
  EXPECT_FALSE(stmt.HasParams());
  EXPECT_EQ(stmt.GetParams().size(), 0);
}

// Test SqlBuilder SELECT
TEST_F(MysqlStatementTest, SqlBuilderSelect) {
  auto stmt = SqlBuilder::Select({"id", "name", "age"})
                  .From("users")
                  .Where("age", ">", MysqlValue(18))
                  .Where("name", "LIKE", MysqlValue("%john%"))
                  .OrderBy("age", "DESC")
                  .Limit(10)
                  .Build();
  
  std::string expected_sql = "SELECT id, name, age FROM users WHERE age > ? AND name LIKE ? ORDER BY age DESC LIMIT 10";
  EXPECT_EQ(stmt.GetSql(), expected_sql);
  EXPECT_EQ(stmt.GetParams().size(), 2);
  
  auto params = stmt.GetParamsAsStrings();
  EXPECT_EQ(params[0], "18");
  EXPECT_EQ(params[1], "%john%");
}

// Test SqlBuilder INSERT
TEST_F(MysqlStatementTest, SqlBuilderInsert) {
  auto stmt = SqlBuilder::Insert("users", {"name", "age", "email"})
                  .Build();
  
  stmt.Bind(MysqlValue("Charlie"))
      .Bind(MysqlValue(28))
      .Bind(MysqlValue("charlie@example.com"));
  
  std::string expected_sql = "INSERT INTO users (name, age, email) VALUES (?, ?, ?)";
  EXPECT_EQ(stmt.GetSql(), expected_sql);
  EXPECT_EQ(stmt.GetParams().size(), 3);
}

// Test SqlBuilder UPDATE
TEST_F(MysqlStatementTest, SqlBuilderUpdate) {
  auto stmt = SqlBuilder::Update("users")
                  .Set("name", MysqlValue("David"))
                  .Set("age", MysqlValue(35))
                  .Where("id", "=", MysqlValue(789))
                  .Build();
  
  std::string expected_sql = "UPDATE users SET name = ?, age = ? WHERE id = ?";
  EXPECT_EQ(stmt.GetSql(), expected_sql);
  EXPECT_EQ(stmt.GetParams().size(), 3);
  
  auto params = stmt.GetParamsAsStrings();
  EXPECT_EQ(params[0], "David");
  EXPECT_EQ(params[1], "35");
  EXPECT_EQ(params[2], "789");
}

// Test SqlBuilder DELETE
TEST_F(MysqlStatementTest, SqlBuilderDelete) {
  auto stmt = SqlBuilder::Delete("users")
                  .Where("age", "<", MysqlValue(18))
                  .Build();
  
  std::string expected_sql = "DELETE FROM users WHERE age < ?";
  EXPECT_EQ(stmt.GetSql(), expected_sql);
  EXPECT_EQ(stmt.GetParams().size(), 1);
  
  auto params = stmt.GetParamsAsStrings();
  EXPECT_EQ(params[0], "18");
}

// Test SqlBuilder complex query
TEST_F(MysqlStatementTest, SqlBuilderComplexQuery) {
  auto stmt = SqlBuilder::Select({"u.id", "u.name", "o.total"})
                  .From("users u")
                  .Join("orders o", "u.id = o.user_id")
                  .Where("u.age", ">=", MysqlValue(18))
                  .Where("o.total", ">", MysqlValue(100.0))
                  .OrderBy("o.total", "DESC")
                  .Limit(20)
                  .Offset(40)
                  .Build();
  
  std::string expected_sql = "SELECT u.id, u.name, o.total FROM users u "
                            "JOIN orders o ON u.id = o.user_id "
                            "WHERE u.age >= ? AND o.total > ? "
                            "ORDER BY o.total DESC LIMIT 20 OFFSET 40";
  EXPECT_EQ(stmt.GetSql(), expected_sql);
  EXPECT_EQ(stmt.GetParams().size(), 2);
}

// Test SQL injection prevention
TEST_F(MysqlStatementTest, SqlInjectionPrevention) {
  // Simulate malicious input
  std::string malicious_input = "'; DROP TABLE users; --";
  
  auto stmt = SqlBuilder::Select({"*"})
                  .From("users")
                  .Where("name", "=", MysqlValue(malicious_input))
                  .Build();
  
  // The SQL should still be safe with parameter binding
  EXPECT_EQ(stmt.GetSql(), "SELECT * FROM users WHERE name = ?");
  
  auto params = stmt.GetParamsAsStrings();
  EXPECT_EQ(params[0], malicious_input);
  // The parameter will be properly escaped by MySQL prepared statement
}

// Test NULL handling
TEST_F(MysqlStatementTest, NullHandling) {
  auto stmt = SqlBuilder::Insert("users", {"name", "age", "email"})
                  .Build();
  
  stmt.Bind(MysqlValue("Eve"))
      .Bind(MysqlValue::Null())  // NULL age
      .Bind(MysqlValue("eve@example.com"));
  
  EXPECT_EQ(stmt.GetParams().size(), 3);
  EXPECT_TRUE(stmt.GetParams()[1].IsNull());
  
  auto params = stmt.GetParamsAsStrings();
  EXPECT_EQ(params[1], "NULL");
}

}  // namespace trpc::mysql::testing
