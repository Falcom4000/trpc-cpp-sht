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

#include <any>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace trpc::mysql {

/// @brief MySQL data types for parameter binding
enum class MysqlDataType {
  kNull,
  kTinyInt,
  kSmallInt,
  kInt,
  kBigInt,
  kFloat,
  kDouble,
  kString,
  kBlob,
  kDate,
  kTime,
  kDateTime,
  kTimestamp,
};

/// @brief Type-safe parameter value
class MysqlValue {
 public:
  using ValueType = std::variant<
    std::monostate,           // NULL
    int8_t,                   // TINYINT
    int16_t,                  // SMALLINT
    int32_t,                  // INT
    int64_t,                  // BIGINT
    float,                    // FLOAT
    double,                   // DOUBLE
    std::string,              // STRING/VARCHAR/TEXT
    std::vector<uint8_t>      // BLOB/BINARY
  >;
  
  MysqlValue() : value_(std::monostate{}) {}
  
  // Null value
  static MysqlValue Null() { return MysqlValue(); }
  
  // Integer types
  explicit MysqlValue(int8_t v) : value_(v) {}
  explicit MysqlValue(int16_t v) : value_(v) {}
  explicit MysqlValue(int32_t v) : value_(v) {}
  explicit MysqlValue(int64_t v) : value_(v) {}
  
  // Floating point types
  explicit MysqlValue(float v) : value_(v) {}
  explicit MysqlValue(double v) : value_(v) {}
  
  // String types
  explicit MysqlValue(const std::string& v) : value_(v) {}
  explicit MysqlValue(std::string&& v) : value_(std::move(v)) {}
  explicit MysqlValue(const char* v) : value_(std::string(v)) {}
  
  // Binary types
  explicit MysqlValue(const std::vector<uint8_t>& v) : value_(v) {}
  explicit MysqlValue(std::vector<uint8_t>&& v) : value_(std::move(v)) {}
  
  bool IsNull() const { return std::holds_alternative<std::monostate>(value_); }
  
  const ValueType& GetValue() const { return value_; }
  
  MysqlDataType GetType() const {
    if (std::holds_alternative<std::monostate>(value_)) return MysqlDataType::kNull;
    if (std::holds_alternative<int8_t>(value_)) return MysqlDataType::kTinyInt;
    if (std::holds_alternative<int16_t>(value_)) return MysqlDataType::kSmallInt;
    if (std::holds_alternative<int32_t>(value_)) return MysqlDataType::kInt;
    if (std::holds_alternative<int64_t>(value_)) return MysqlDataType::kBigInt;
    if (std::holds_alternative<float>(value_)) return MysqlDataType::kFloat;
    if (std::holds_alternative<double>(value_)) return MysqlDataType::kDouble;
    if (std::holds_alternative<std::string>(value_)) return MysqlDataType::kString;
    if (std::holds_alternative<std::vector<uint8_t>>(value_)) return MysqlDataType::kBlob;
    return MysqlDataType::kNull;
  }
  
  /// @brief Convert value to string representation for prepared statement
  /// @note This is used internally for compatibility with the old string-based API
  std::string ToString() const {
    return std::visit([](const auto& v) -> std::string {
      using T = std::decay_t<decltype(v)>;
      if constexpr (std::is_same_v<T, std::monostate>) {
        return "NULL";
      } else if constexpr (std::is_arithmetic_v<T>) {
        return std::to_string(v);
      } else if constexpr (std::is_same_v<T, std::string>) {
        return v;
      } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
        // For binary data, convert to hex string or base64 would be better
        // For now, just return a placeholder
        return "<binary_data>";
      }
      return "";
    }, value_);
  }
  
 private:
  ValueType value_;
};

/// @brief Type-safe SQL statement builder
class MysqlStatement {
 public:
  MysqlStatement() = default;
  
  /// @brief Create statement from SQL string
  explicit MysqlStatement(const std::string& sql) : sql_(sql) {}
  explicit MysqlStatement(std::string&& sql) : sql_(std::move(sql)) {}
  
  /// @brief Bind parameter by position (1-indexed)
  MysqlStatement& Bind(size_t position, const MysqlValue& value) {
    if (position == 0) {
      throw std::invalid_argument("Parameter position starts from 1");
    }
    if (position > params_.size()) {
      params_.resize(position);
    }
    params_[position - 1] = value;
    return *this;
  }
  
  /// @brief Bind parameter by appending
  MysqlStatement& Bind(const MysqlValue& value) {
    params_.push_back(value);
    return *this;
  }
  
  /// @brief Clear all parameters
  void ClearParams() { params_.clear(); }
  
  /// @brief Get SQL string
  const std::string& GetSql() const { return sql_; }
  
  /// @brief Get parameters
  const std::vector<MysqlValue>& GetParams() const { return params_; }
  
  /// @brief Get parameters as strings (for backward compatibility)
  std::vector<std::string> GetParamsAsStrings() const {
    std::vector<std::string> result;
    result.reserve(params_.size());
    for (const auto& param : params_) {
      result.push_back(param.ToString());
    }
    return result;
  }
  
  /// @brief Check if statement has parameters
  bool HasParams() const { return !params_.empty(); }
  
 private:
  std::string sql_;
  std::vector<MysqlValue> params_;
};

/// @brief SQL statement builder with fluent API
class SqlBuilder {
 public:
  SqlBuilder() = default;
  
  /// @brief SELECT statement
  static SqlBuilder Select(const std::vector<std::string>& columns = {"*"}) {
    SqlBuilder builder;
    builder.sql_ = "SELECT ";
    for (size_t i = 0; i < columns.size(); ++i) {
      if (i > 0) builder.sql_ += ", ";
      builder.sql_ += columns[i];
    }
    return builder;
  }
  
  /// @brief FROM clause
  SqlBuilder& From(const std::string& table) {
    sql_ += " FROM " + table;
    return *this;
  }
  
  /// @brief JOIN clause
  SqlBuilder& Join(const std::string& table, const std::string& condition) {
    sql_ += " JOIN " + table + " ON " + condition;
    return *this;
  }
  
  /// @brief LEFT JOIN clause
  SqlBuilder& LeftJoin(const std::string& table, const std::string& condition) {
    sql_ += " LEFT JOIN " + table + " ON " + condition;
    return *this;
  }
  
  /// @brief RIGHT JOIN clause
  SqlBuilder& RightJoin(const std::string& table, const std::string& condition) {
    sql_ += " RIGHT JOIN " + table + " ON " + condition;
    return *this;
  }
  
  /// @brief WHERE clause with parameter
  SqlBuilder& Where(const std::string& condition) {
    if (where_added_) {
      sql_ += " AND " + condition;
    } else {
      sql_ += " WHERE " + condition;
      where_added_ = true;
    }
    return *this;
  }
  
  /// @brief WHERE with parameter binding
  SqlBuilder& Where(const std::string& column, const std::string& op, const MysqlValue& value) {
    Where(column + " " + op + " ?");
    params_.push_back(value);
    return *this;
  }
  
  /// @brief ORDER BY clause
  SqlBuilder& OrderBy(const std::string& column, const std::string& direction = "") {
    if (!order_by_added_) {
      sql_ += " ORDER BY ";
      order_by_added_ = true;
    } else {
      sql_ += ", ";
    }
    sql_ += column;
    if (!direction.empty()) sql_ += " " + direction;
    return *this;
  }
  
  /// @brief LIMIT clause
  SqlBuilder& Limit(size_t limit) {
    sql_ += " LIMIT " + std::to_string(limit);
    return *this;
  }
  
  /// @brief OFFSET clause
  SqlBuilder& Offset(size_t offset) {
    sql_ += " OFFSET " + std::to_string(offset);
    return *this;
  }
  
  /// @brief LIMIT with OFFSET (deprecated, use Limit().Offset() instead)
  SqlBuilder& Limit(size_t limit, size_t offset) {
    sql_ += " LIMIT " + std::to_string(limit) + " OFFSET " + std::to_string(offset);
    return *this;
  }
  
  /// @brief INSERT statement
  static SqlBuilder Insert(const std::string& table, const std::vector<std::string>& columns) {
    SqlBuilder builder;
    builder.sql_ = "INSERT INTO " + table + " (";
    for (size_t i = 0; i < columns.size(); ++i) {
      if (i > 0) builder.sql_ += ", ";
      builder.sql_ += columns[i];
    }
    builder.sql_ += ") VALUES (";
    for (size_t i = 0; i < columns.size(); ++i) {
      if (i > 0) builder.sql_ += ", ";
      builder.sql_ += "?";
    }
    builder.sql_ += ")";
    return builder;
  }
  
  /// @brief UPDATE statement
  static SqlBuilder Update(const std::string& table) {
    SqlBuilder builder;
    builder.sql_ = "UPDATE " + table;
    return builder;
  }
  
  /// @brief SET clause for UPDATE
  SqlBuilder& Set(const std::string& column, const MysqlValue& value) {
    if (!set_added_) {
      sql_ += " SET ";
      set_added_ = true;
    } else {
      sql_ += ", ";
    }
    sql_ += column + " = ?";
    params_.push_back(value);
    return *this;
  }
  
  /// @brief DELETE statement
  static SqlBuilder Delete(const std::string& table) {
    SqlBuilder builder;
    builder.sql_ = "DELETE FROM " + table;
    return builder;
  }
  
  /// @brief Build the final statement
  MysqlStatement Build() {
    MysqlStatement stmt(sql_);
    // Bind all parameters collected during building
    for (const auto& param : params_) {
      stmt.Bind(param);
    }
    return stmt;
  }
  
  /// @brief Get SQL string (for debugging)
  const std::string& GetSql() const { return sql_; }
  
 private:
  std::string sql_;
  std::vector<MysqlValue> params_;
  bool where_added_ = false;
  bool order_by_added_ = false;
  bool set_added_ = false;
};

}  // namespace trpc::mysql
