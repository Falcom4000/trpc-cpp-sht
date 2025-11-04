# MySQL Client Example

This example demonstrates how to use the tRPC-Cpp MySQL client to interact with a MySQL database.

## Features

- Synchronous queries (SELECT, INSERT, UPDATE, DELETE)
- Asynchronous queries
- Prepared statements
- Connection pooling
- Framework integration (filters, service discovery)

## Prerequisites

1. MySQL server running on `127.0.0.1:3306`
2. Database and user configured:
   ```sql
   CREATE DATABASE test_db;
   CREATE USER 'test_user'@'localhost' IDENTIFIED BY 'test_password';
   GRANT ALL PRIVILEGES ON test_db.* TO 'test_user'@'localhost';
   FLUSH PRIVILEGES;
   ```

## Configuration

Edit `trpc_cpp.yaml` to configure your MySQL connection:

```yaml
client:
  service:
    - name: trpc.mysql.test.service
      target: 127.0.0.1:3306  # MySQL server address
      mysql:
        user: test_user
        password: test_password
        database: test_db
        max_connections: 50
        min_idle_connections: 5
```

## Building

```bash
bazel build //examples/features/mysql_client:mysql_client_demo
```

## Running

```bash
./bazel-bin/examples/features/mysql_client/mysql_client_demo --config=examples/features/mysql_client/trpc_cpp.yaml
```

## Examples Included

1. **Simple SELECT query**: Basic synchronous query
2. **INSERT statement**: Creating tables and inserting data
3. **Prepared statements**: Secure parameter binding
4. **Asynchronous queries**: Non-blocking database operations
5. **Multiple queries**: Batch operations

## API Usage

### Synchronous Query

```cpp
auto mysql_proxy = GetTrpcClient()->GetProxy<trpc::mysql::MysqlServiceProxy>("trpc.mysql.test.service");
auto ctx = MakeClientContext(mysql_proxy);

trpc::mysql::MysqlResultSet result;
auto status = mysql_proxy->Query(ctx, "SELECT * FROM users", &result);

if (status.OK()) {
  for (const auto& row : result.rows) {
    // Process row
  }
}
```

### Asynchronous Query

```cpp
auto future = mysql_proxy->AsyncQuery(ctx, "SELECT * FROM users");
future.Then([](Future<MysqlResultSet>&& f) {
  if (f.IsReady()) {
    auto result = f.GetValue0();
    // Process result
  }
  return MakeReadyFuture<>();
});
```

### Prepared Statement

```cpp
std::vector<std::string> params = {"Alice", "25"};
auto status = mysql_proxy->PreparedExecute(ctx, 
    "INSERT INTO users (name, age) VALUES (?, ?)", 
    params, &result);
```

## Notes

- The connection pool is automatically managed
- Connections are reused across requests
- Failed connections are automatically removed
- The executor uses a separate thread pool to avoid blocking framework threads
- In Fiber mode, synchronous calls don't block the physical thread

## Advanced Features

### Connection Pool Configuration

- `max_connections`: Maximum number of connections (default: 100)
- `min_idle_connections`: Minimum idle connections to maintain (default: 5)
- `max_idle_time_ms`: Maximum time a connection can be idle (default: 60000ms)

### Thread Pool Configuration

- `executor_thread_num`: Number of threads for MySQL operations (default: 8)

### Timeout Configuration

- `connect_timeout_ms`: Connection timeout (default: 3000ms)
- `read_timeout_ms`: Read timeout (default: 30000ms)
- `write_timeout_ms`: Write timeout (default: 30000ms)
