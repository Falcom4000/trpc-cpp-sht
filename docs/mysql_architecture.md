# MySQL Service 架构设计文档

## 1. 整体架构概览

tRPC-Cpp MySQL Service 采用**分层架构设计**，将连接管理、任务执行、类型安全等关注点分离，提供高性能、易用的 MySQL 客户端服务。

```
┌─────────────────────────────────────────────────────────────┐
│                    Application Layer                         │
│  (User Code using MysqlServiceProxy)                         │
└──────────────────────┬──────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────┐
│              MysqlServiceProxy (ServiceProxy)               │
│  • 类型安全的 API (Execute/Query/AsyncExecute/AsyncQuery)  │
│  • Filter 支持 (CLIENT_PRE_RPC_INVOKE/POST_RPC_INVOKE)     │
│  • 配置解析和初始化                                         │
└──────────────┬───────────────────────────┬──────────────────┘
               │                           │
       ┌───────▼────────┐        ┌────────▼─────────┐
       │ Connection Pool│        │   Executor       │
       │                 │        │                  │
       │ • 连接生命周期  │        │ • 线程池执行     │
       │ • 健康检查      │        │ • SQL 执行       │
       │ • 连接复用      │        │ • 结果解析       │
       └───────┬─────────┘        └────────┬─────────┘
               │                           │
       ┌───────▼─────────┐        ┌────────▼─────────┐
       │  MysqlConnection│        │  MySQL C-API      │
       │                 │        │  (libmysqlclient) │
       │ • MYSQL* 封装   │        │                   │
       │ • 连接验证      │        │ • mysql_real_query│
       │ • 状态管理      │        │ • mysql_stmt_*    │
       └─────────────────┘        └──────────────────┘
```

## 2. 核心组件详解

### 2.1 MysqlServiceProxy（服务代理层）

**职责：**
- 提供类型安全的 API 接口
- 集成 tRPC 框架的 Filter 机制
- 管理连接池和执行器的生命周期
- 解析配置并初始化底层组件

**关键接口：**

```cpp
// 同步接口
Status Execute(const ClientContextPtr& context, 
               const MysqlStatement& statement,
               MysqlResultSet* result);

// 异步接口
Future<MysqlResultSet> AsyncExecute(const ClientContextPtr& context,
                                    const MysqlStatement& statement);
```

**注意：** `Execute` 方法既可以执行查询（SELECT）也可以执行更新（INSERT/UPDATE/DELETE），根据 SQL 语句类型自动处理。

**执行流程：**

```
用户调用 Execute/AsyncExecute
    ↓
ExecuteInternal() / AsyncExecuteInternal()
    ↓
1. 设置 Request/Response 数据到 Context
2. 运行 Pre-Invoke Filters
    ↓
3. 从连接池 Borrow 连接（返回 RAII Handle）
    ↓
4. 提交任务到 Executor 执行
    ↓
5. 等待结果返回（Fiber 环境下使用 fiber::BlockingTryGet）
    ↓
6. Handle 析构时自动归还连接到连接池（RAII）
    ↓
7. 运行 Post-Invoke Filters
    ↓
返回结果
```

### 2.2 MysqlConnectionPool（连接池层）

**职责：**
- 管理 MySQL 连接的创建、复用和销毁
- 维护空闲连接队列
- 定期健康检查和连接清理
- 控制连接池大小（最小空闲、最大连接数）
- 通过 RAII Handle 机制自动管理连接生命周期

**核心数据结构：**

```cpp
class MysqlConnectionPool {
  // RAII Handle for borrowed connections
  class Handle {
    // 自动归还连接的 RAII 包装器
    // 析构时自动调用 ReturnConnection
  };
  
  MysqlConnectionConfig config_;              // 配置信息
  
  std::mutex mutex_;                          // 保护连接池的互斥锁
  std::condition_variable cv_;                // 等待连接的条件变量
  
  std::deque<MysqlConnectionPtr> idle_connections_;      // 空闲连接队列
  std::atomic<size_t> total_connections_{0};  // 总连接数（原子计数）
  
  std::atomic<bool> stopped_{false};          // 停止标志
  std::thread health_check_thread_;          // 健康检查线程
  
  // 方法
  Handle Borrow(uint32_t timeout_ms = 3000);  // 借用连接（返回 RAII Handle）
};
```

**注意：** 连接池使用 RAII `Handle` 机制管理连接生命周期，不需要显式维护活跃连接集合。连接通过 `Handle` 自动归还。

**连接获取流程：**

```
Borrow(timeout_ms)
    ↓
1. 尝试从 idle_connections_ 获取空闲连接
   ├─ 如果连接有效 → 更新使用时间 → 包装为 Handle → 返回
   └─ 如果连接无效 → 减少计数 → 继续尝试下一个
    ↓
2. 如果空闲连接为空，检查总连接数
   ├─ 未达到 max_connections → 创建新连接 → 包装为 Handle → 返回
   └─ 已达到上限 → 等待空闲连接（带超时）
    ↓
3. 超时或停止 → 返回空的 Handle
    ↓
4. Handle 析构时自动调用 ReturnConnection() 归还连接
```

**健康检查机制：**

```cpp
HealthCheckLoop() {
  const uint32_t check_interval_ms = 10000;  // 每 10 秒检查一次
  
  while (!stopped_) {
    std::this_thread::sleep_for(std::chrono::milliseconds(check_interval_ms));
    
    // 1. 关闭超过 max_idle_time_ms 的空闲连接
    CloseIdleConnections();
    
    // 2. 确保最小空闲连接数
    EnsureMinIdleConnections();
  }
}
```

**连接池配置：**

```cpp
struct MysqlConnectionConfig {
  uint32_t max_connections = 100;           // 最大连接数
  uint32_t min_idle_connections = 5;        // 最小空闲连接数
  uint32_t max_idle_time_ms = 60000;        // 最大空闲时间（60秒）
  uint32_t connect_timeout_ms = 3000;       // 连接超时
  uint32_t read_timeout_ms = 30000;         // 读取超时
  uint32_t write_timeout_ms = 30000;        // 写入超时
};
```

### 2.3 MysqlExecutor（执行器层）

**职责：**
- 在独立线程池中执行 MySQL C-API 调用
- 避免阻塞 Fiber Worker 线程
- 支持同步和异步两种执行模式
- 处理 SQL 执行和结果解析

**核心设计：**

```cpp
class MysqlExecutor {
  std::unique_ptr<ThreadPoolImpl<SQThreadPool>> thread_pool_;  // 线程池
  uint32_t thread_num_;                                        // 线程数（默认8）
};
```

**同步执行流程（SubmitAndWait）：**

```
SubmitAndWait(conn, request, result, timeout_ms)
    ↓
1. 创建 Promise<Status>
    ↓
2. 提交任务到线程池：
   task = [conn, request, result, promise] {
     ExecuteTask(conn, request, result);  // 在线程池中执行
     promise.SetValue(Status());
   }
    ↓
3. 根据运行环境等待结果：
   ├─ Fiber 环境 → fiber::BlockingTryGet(future, timeout_ms)
   │  （只挂起当前 Fiber，不阻塞 pthread）
   └─ 非 Fiber 环境 → future::BlockingTryGet(future, timeout_ms)
      （阻塞 pthread）
    ↓
4. 返回执行状态
```

**异步执行流程（SubmitAsync）：**

```
SubmitAsync(conn, request)
    ↓
1. 创建 Promise<MysqlResultSet>
    ↓
2. 提交任务到线程池：
   task = [conn, request, promise] {
     MysqlResultSet result;
     ExecuteTask(conn, request, &result);
     promise.SetValue(std::move(result));
   }
    ↓
3. 立即返回 Future<MysqlResultSet>
```

**SQL 执行实现：**

```cpp
ExecuteTask(mysql, request, result) {
  // 统一使用 Prepared Statement
    ExecutePreparedSql(mysql, request.sql, request.params, result);
}

ExecutePreparedSql(mysql, sql, params, result) {
  1. mysql_stmt_init(mysql)  // 初始化 statement
  2. mysql_stmt_prepare(stmt, sql, length)  // 准备语句
  3. mysql_stmt_bind_param(stmt, bind_params)  // 绑定参数
  4. mysql_stmt_execute(stmt)  // 执行
  5. mysql_stmt_result_metadata(stmt)  // 获取元数据
  6. mysql_stmt_bind_result(stmt, bind_results)  // 绑定结果
  7. mysql_stmt_fetch(stmt)  // 逐行获取结果
}
```

### 2.4 MysqlConnection（连接封装层）

**职责：**
- 封装 MySQL C-API 的 MYSQL* 句柄
- 提供连接有效性检查（ping）
- 管理连接的最后使用时间
- 自动关闭连接（RAII）

**核心接口：**

```cpp
class MysqlConnection {
  MYSQL* GetRawConnection();           // 获取原始 MYSQL* 句柄
  bool IsValid() const;                // 检查连接是否有效（ping）
  void Reset();                        // 重置连接状态
  void UpdateLastUsedTime();           // 更新最后使用时间
  std::chrono::steady_clock::time_point GetLastUsedTime() const;
};
```

**连接验证：**

```cpp
bool IsValid() const {
  if (!mysql_) return false;
  return mysql_ping(mysql_) == 0;  // 使用 ping 检查连接
}
```

### 2.5 MysqlStatement（类型安全层）

**职责：**
- 提供类型安全的 SQL 语句构建
- 支持参数绑定（类型安全）

**核心组件：**

```cpp
// 类型安全的参数值
class MysqlValue {
  using ValueType = std::variant<
    std::monostate,           // NULL
    int8_t, int16_t, int32_t, int64_t,  // 整数类型
    float, double,            // 浮点类型
    std::string,              // 字符串类型
    std::vector<uint8_t>      // 二进制类型
  >;
};

// SQL 语句和参数绑定
class MysqlStatement {
  std::string sql_;                    // SQL 语句
  std::vector<MysqlValue> params_;      // 参数列表
  
  MysqlStatement& Bind(size_t position, const MysqlValue& value);
  MysqlStatement& Bind(const MysqlValue& value);
};
```

**使用示例：**

```cpp
// 直接构建 SQL 语句并绑定参数
MysqlStatement stmt("SELECT * FROM users WHERE id = ?");
stmt.Bind(1, MysqlValue(123));

// 或者使用链式绑定
MysqlStatement stmt2("SELECT id, name, email FROM users WHERE age > ? AND status = ?");
stmt2.Bind(1, MysqlValue(18))
     .Bind(2, MysqlValue("active"));
```

### 2.6 MysqlResultSet（结果集层）

**职责：**
- 封装查询结果
- 提供结果数据访问接口

**数据结构：**

```cpp
struct MysqlResultSet {
  std::vector<std::string> field_names;              // 字段名列表
  std::vector<std::vector<std::string>> rows;        // 数据行（每行是字段值列表）
  uint64_t affected_rows = 0;                        // 受影响行数（INSERT/UPDATE/DELETE）
  uint64_t insert_id = 0;                            // 自增 ID（INSERT）
  uint32_t field_count = 0;                          // 字段数量
};
```

## 3. 数据流图

### 3.1 同步执行数据流

```
User Code
    │
    │ Execute(context, statement, &result)
    ▼
MysqlServiceProxy::Execute()
    │
    │ 1. 转换 MysqlStatement → MysqlRequest
    │ 2. 运行 Pre-Invoke Filters
    ▼
MysqlServiceProxy::ExecuteInternal()
    │
    │ 3. 借用连接（返回 RAII Handle）
    ▼
MysqlConnectionPool::Borrow()
    │
    │ 返回 Handle（包含 MysqlConnectionPtr）
    ▼
MysqlExecutor::SubmitAndWait()
    │
    │ 4. 提交任务到线程池
    ▼
ThreadPool::AddTask()
    │
    │ 5. 在线程池线程中执行
    ▼
MysqlExecutor::ExecuteTask()
    │
    │ 6. 调用 MySQL C-API
    ▼
mysql_real_query() / mysql_stmt_*()
    │
    │ 7. 解析结果
    ▼
MysqlExecutor::ParseResultSet()
    │
    │ 8. 返回结果（通过 Promise/Future）
    ▼
fiber::BlockingTryGet() / future::BlockingTryGet()
    │
    │ 9. Handle 析构时自动归还连接（RAII）
    ▼
MysqlConnectionPool::ReturnConnection() (自动调用)
    │
    │ 10. 运行 Post-Invoke Filters
    ▼
返回 Status 和 MysqlResultSet
```

### 3.2 异步执行数据流

```
User Code
    │
    │ AsyncExecute(context, statement)
    ▼
MysqlServiceProxy::AsyncExecute()
    │
    │ 1. 转换 MysqlStatement → MysqlRequest
    │ 2. 运行 Pre-Invoke Filters
    ▼
MysqlServiceProxy::AsyncExecuteInternal()
    │
    │ 3. 借用连接（返回 RAII Handle）
    ▼
MysqlConnectionPool::Borrow()
    │
    │ 4. 提交异步任务（Handle 在 Future 回调中保持存活）
    ▼
MysqlExecutor::SubmitAsync()
    │
    │ 立即返回 Future<MysqlResultSet>
    ▼
User Code
    │
    │ 5. 使用 fiber::BlockingGet() 等待
    ▼
Future::Then() 回调
    │
    │ 6. 在线程池线程中执行
    ▼
MysqlExecutor::ExecuteTask()
    │
    │ 7. 调用 MySQL C-API 并解析结果
    ▼
Promise::SetValue(result)
    │
    │ 8. 触发 Then() 回调
    ▼
Future::Then() 回调
    │
    │ 9. Handle 析构时自动归还连接（RAII）
    │ 10. 运行 Post-Invoke Filters
    ▼
返回最终结果
```

## 4. 关键设计决策

### 4.1 为什么使用独立线程池执行 MySQL C-API？

**原因：**
1. **MySQL C-API 是阻塞的**：`mysql_real_query()` 等函数会阻塞调用线程
2. **避免阻塞 Fiber Worker**：在 Fiber 环境中，如果直接调用会阻塞 pthread，影响调度
3. **隔离阻塞操作**：将阻塞操作隔离到独立线程池，不影响主业务线程

**实现：**
- 使用 `ThreadPoolImpl<SQThreadPool>` 作为执行线程池
- 默认 8 个线程（可配置）
- 通过 Promise/Future 机制实现异步通信

### 4.2 为什么在 Fiber 环境中使用 fiber::BlockingTryGet？

**原因：**
1. **不阻塞 pthread**：`fiber::BlockingTryGet()` 只挂起当前 Fiber，不阻塞底层 pthread
2. **保持高并发**：一个 pthread 可以运行多个 Fiber，挂起一个 Fiber 不影响其他 Fiber
3. **自动适配**：框架自动检测运行环境，选择合适的阻塞方式

**对比：**
```cpp
// Fiber 环境
if (trpc::IsRunningInFiberWorker()) {
  status = trpc::fiber::BlockingTryGet(std::move(future), timeout_ms);
  // ✅ 只挂起当前 Fiber，pthread 继续运行其他 Fiber
}

// 非 Fiber 环境
else {
  status = trpc::future::BlockingTryGet(std::move(future), timeout_ms);
  // ⚠️ 阻塞整个 pthread
}
```

### 4.3 连接池的设计考虑

**设计要点：**
1. **连接复用**：减少连接创建/销毁开销
2. **健康检查**：定期检查连接有效性，自动清理无效连接
3. **动态调整**：根据负载动态创建/销毁连接
4. **线程安全**：使用互斥锁保护连接池状态
5. **RAII 管理**：通过 `Handle` 自动管理连接生命周期，防止连接泄漏

**关键机制：**
- **最小空闲连接**：保持一定数量的空闲连接，快速响应请求
- **最大连接数限制**：防止连接数过多导致数据库压力
- **空闲超时清理**：自动清理长时间未使用的连接（默认 60 秒）
- **连接有效性检查**：使用 `mysql_ping()` 验证连接
- **RAII Handle**：连接通过 `Borrow()` 返回 `Handle`，析构时自动归还，无需手动管理

### 4.4 类型安全的设计

**设计目标：**
1. **编译期类型检查**：使用 C++ 类型系统防止参数类型错误
2. **易用的 API**：提供链式参数绑定简化 SQL 语句构建
3. **向后兼容**：支持字符串参数（通过 ToString() 转换）

**实现方式：**
- 使用 `std::variant` 表示类型安全的参数值
- 提供类型转换构造函数（`MysqlValue(int)`, `MysqlValue(string)`, 等）
- 支持链式参数绑定（`stmt.Bind(...).Bind(...)`）

## 5. 性能优化

### 5.1 连接池优化
- **连接复用**：减少连接创建开销
- **预创建连接**：启动时创建最小空闲连接
- **懒创建**：按需创建新连接（不超过最大限制）

### 5.2 执行器优化
- **线程池隔离**：避免阻塞主业务线程
- **批量处理**：支持批量提交任务
- **异步执行**：支持异步接口，提高并发能力

### 5.3 Fiber 环境优化
- **非阻塞等待**：使用 `fiber::BlockingTryGet()` 避免阻塞 pthread
- **自动适配**：根据运行环境自动选择最优策略

## 6. 扩展性设计

### 6.1 Filter 支持
- 支持 tRPC 框架的 Filter 机制
- 可以在 Pre-Invoke 和 Post-Invoke 阶段插入自定义逻辑
- 支持监控、日志、重试等场景

### 6.2 配置灵活性
- 支持通过 YAML 配置文件设置连接参数
- 支持运行时调整连接池大小
- 支持自定义超时时间

### 6.3 错误处理
- 统一的 Status 返回机制
- 详细的错误信息（错误码 + 错误消息）
- 异常安全（RAII 管理资源）

## 7. 使用示例

### 7.1 基本使用

```cpp
// 1. 创建 ServiceProxy（通过框架配置）
auto mysql_proxy = GetProxy<MysqlServiceProxy>("mysql_service");

// 2. 构建 SQL 语句
MysqlStatement stmt("SELECT * FROM users WHERE id = ?");
stmt.Bind(1, MysqlValue(123));

// 3. 执行查询
auto context = MakeClientContext(mysql_proxy);
context->SetTimeout(30000);

MysqlResultSet result;
Status status = mysql_proxy->Execute(context, stmt, &result);

// 4. 处理结果
if (status.OK()) {
  for (const auto& row : result.rows) {
    // 处理每一行数据
  }
}
```

### 7.2 复杂查询示例

```cpp
// 构建复杂查询并绑定多个参数
MysqlStatement stmt(
    "SELECT id, name, email FROM users "
    "WHERE age > ? AND status = ? "
    "ORDER BY created_at DESC LIMIT ?");
stmt.Bind(1, MysqlValue(18))
    .Bind(2, MysqlValue("active"))
    .Bind(3, MysqlValue(100));

auto context = MakeClientContext(mysql_proxy);
MysqlResultSet result;
Status status = mysql_proxy->Execute(context, stmt, &result);
```

### 7.3 异步使用

```cpp
// 异步执行
auto context = MakeClientContext(mysql_proxy);
MysqlStatement stmt("SELECT * FROM users WHERE id = ?");
stmt.Bind(1, MysqlValue(123));

auto future = mysql_proxy->AsyncExecute(context, stmt);

// 在 Fiber 环境中等待
trpc::StartFiberDetached([future = std::move(future)]() mutable {
  auto result = trpc::fiber::BlockingGet(std::move(future));
  // 处理结果
});
```

## 8. 总结

tRPC-Cpp MySQL Service 采用**分层架构**，通过以下设计实现了高性能和易用性：

1. **连接池管理**：自动管理连接生命周期，提供连接复用和健康检查
2. **独立执行器**：在独立线程池中执行阻塞的 MySQL C-API 调用
3. **Fiber 适配**：自动检测运行环境，在 Fiber 中使用非阻塞等待
4. **类型安全**：提供类型安全的 API，防止参数类型错误
5. **框架集成**：集成 tRPC 框架的 Filter 机制，支持扩展

该架构既保证了高性能（连接复用、非阻塞执行），又提供了易用的 API（类型安全、流式构建），同时与 tRPC 框架深度集成，支持 Filter、监控等高级特性。

