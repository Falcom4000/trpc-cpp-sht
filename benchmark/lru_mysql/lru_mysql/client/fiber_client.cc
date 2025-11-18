#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include <mutex>

#include "gflags/gflags.h"

#include "trpc/client/make_client_context.h"
#include "trpc/client/trpc_client.h"
#include "trpc/common/runtime_manager.h"
#include "trpc/coroutine/fiber.h"
#include "trpc/log/trpc_log.h"

#include "lru_mysql.trpc.pb.h"

DEFINE_string(client_config, "trpc_cpp.yaml", "framework client config file, --client_config=trpc_cpp.yaml");
DEFINE_string(service_name, "trpc.benchmark.mysql.Greeter", "callee service name");
DEFINE_int32(concurrency, 200, "number of concurrent fibers");
DEFINE_int32(duration, 60, "test duration in seconds");
DEFINE_string(msg, "test_msg", "message to send in request");

// Statistics
std::atomic<uint64_t> total_requests{0};
std::atomic<uint64_t> success_requests{0};
std::atomic<uint64_t> failed_requests{0};
std::atomic<bool> stop_flag{false};

// Latency statistics
std::atomic<uint64_t> total_latency_us{0};  // Total latency in microseconds
std::atomic<uint64_t> min_latency_us{UINT64_MAX};
std::atomic<uint64_t> max_latency_us{0};

// Latency samples for percentile calculation (lock-free ring buffer)
constexpr size_t MAX_SAMPLES = 10000;
std::vector<uint64_t> latency_samples;
std::atomic<size_t> sample_index{0};
std::mutex samples_mutex;

int GreeterGetID(const std::shared_ptr<::trpc::benchmark::mysql::GreeterServiceProxy>& proxy, 
                 const std::string& msg) {
  // Record start time
  auto start_time = std::chrono::steady_clock::now();
  
  ::trpc::ClientContextPtr client_ctx = ::trpc::MakeClientContext(proxy);
  client_ctx->SetTimeout(5000);  // 5 seconds timeout
  
  ::trpc::benchmark::mysql::IDRequest req;
  req.set_msg(msg);
  
  ::trpc::benchmark::mysql::IDReply rsp;
  ::trpc::Status status = proxy->GetID(client_ctx, req, &rsp);
  
  // Calculate latency
  auto end_time = std::chrono::steady_clock::now();
  auto latency_us = std::chrono::duration_cast<std::chrono::microseconds>(
      end_time - start_time).count();
  
  total_requests.fetch_add(1, std::memory_order_relaxed);
  
  if (!status.OK()) {
    failed_requests.fetch_add(1, std::memory_order_relaxed);
    // Log first few errors for debugging
    static std::atomic<int> error_count{0};
    int count = error_count.fetch_add(1, std::memory_order_relaxed);
    if (count < 5) {
      std::cerr << "Request failed: " << status.ErrorMessage() 
                << ", reply: " << rsp.msg() << std::endl;
    }
    return -1;
  }
  
  success_requests.fetch_add(1, std::memory_order_relaxed);
  
  // Update latency statistics
  total_latency_us.fetch_add(latency_us, std::memory_order_relaxed);
  
  // Update min/max latency
  uint64_t current_min = min_latency_us.load(std::memory_order_relaxed);
  while (latency_us < current_min && 
         !min_latency_us.compare_exchange_weak(current_min, latency_us, 
                                               std::memory_order_relaxed)) {
    // Retry
  }
  
  uint64_t current_max = max_latency_us.load(std::memory_order_relaxed);
  while (latency_us > current_max && 
         !max_latency_us.compare_exchange_weak(current_max, latency_us, 
                                               std::memory_order_relaxed)) {
    // Retry
  }
  
  // Sample latency for percentile calculation (lock-free sampling)
  // Only sample a fraction of requests to avoid contention
  static thread_local uint32_t sample_counter = 0;
  if (++sample_counter % 10 == 0) {  // Sample every 10th request
    size_t idx = sample_index.fetch_add(1, std::memory_order_relaxed) % MAX_SAMPLES;
    std::lock_guard<std::mutex> lock(samples_mutex);
    if (latency_samples.size() < MAX_SAMPLES) {
      latency_samples.push_back(latency_us);
    } else {
      latency_samples[idx] = latency_us;
    }
  }
  
  return 0;
}

// Worker fiber function
void WorkerFiber(const std::shared_ptr<::trpc::benchmark::mysql::GreeterServiceProxy>& proxy) {
  int current_id = 1;  // Start from id 1
  const int max_id = 100;  // Query up to id 100
  
  while (!stop_flag.load(std::memory_order_relaxed)) {
    // Query current id
    std::string msg = "msg_" + std::to_string(current_id);
    GreeterGetID(proxy, msg);
    
    // Move to next id, wrap around to 1 after 100
    current_id++;
    if (current_id > max_id) {
      current_id = 1;
    }
    
    // Small yield to allow other fibers to run
    ::trpc::FiberYield();
  }
}

// Calculate percentile from sorted samples
uint64_t CalculatePercentile(const std::vector<uint64_t>& samples, double percentile) {
  if (samples.empty()) return 0;
  
  std::vector<uint64_t> sorted_samples = samples;
  std::sort(sorted_samples.begin(), sorted_samples.end());
  
  size_t index = static_cast<size_t>(sorted_samples.size() * percentile);
  if (index >= sorted_samples.size()) {
    index = sorted_samples.size() - 1;
  }
  return sorted_samples[index];
}

// Statistics reporter fiber
void StatsReporterFiber() {
  auto start_time = std::chrono::steady_clock::now();
  auto last_report_time = start_time;
  uint64_t last_total = 0;
  uint64_t last_success = 0;
  uint64_t last_failed = 0;
  uint64_t last_total_latency_us = 0;
  
  while (!stop_flag.load(std::memory_order_relaxed)) {
    ::trpc::FiberSleepFor(std::chrono::seconds(1));
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
    
    uint64_t current_total = total_requests.load(std::memory_order_relaxed);
    uint64_t current_success = success_requests.load(std::memory_order_relaxed);
    uint64_t current_failed = failed_requests.load(std::memory_order_relaxed);
    uint64_t current_total_latency_us = total_latency_us.load(std::memory_order_relaxed);
    
    uint64_t qps = current_total - last_total;
    uint64_t success_qps = current_success - last_success;
    uint64_t failed_qps = current_failed - last_failed;
    
    double success_rate = current_total > 0 ? 
        (100.0 * current_success / current_total) : 0.0;
    
    // Calculate average latency for this period
    uint64_t latency_delta = current_total_latency_us - last_total_latency_us;
    double avg_latency_ms = (qps > 0) ? (latency_delta / 1000.0 / qps) : 0.0;
    
    std::cout << "[Stats] Elapsed: " << elapsed << "s | "
              << "Total: " << current_total << " | "
              << "QPS: " << qps << " | "
              << "Success QPS: " << success_qps << " | "
              << "Failed QPS: " << failed_qps << " | "
              << "Success Rate: " << std::fixed << std::setprecision(2) 
              << success_rate << "% | "
              << "Avg Latency: " << std::fixed << std::setprecision(2)
              << avg_latency_ms << "ms" << std::endl;
    
    last_total = current_total;
    last_success = current_success;
    last_failed = current_failed;
    last_total_latency_us = current_total_latency_us;
    last_report_time = now;
  }
}

int Run() {
  auto proxy = ::trpc::GetTrpcClient()->GetProxy<::trpc::benchmark::mysql::GreeterServiceProxy>(FLAGS_service_name);
  
  if (!proxy) {
    std::cerr << "Failed to get service proxy" << std::endl;
    return -1;
  }
  
  // Initialize latency samples vector
  latency_samples.reserve(MAX_SAMPLES);
  
  std::cout << "=== Starting Benchmark ===" << std::endl;
  std::cout << "Concurrency: " << FLAGS_concurrency << " fibers" << std::endl;
  std::cout << "Duration: " << FLAGS_duration << " seconds" << std::endl;
  std::cout << "Query Pattern: Each fiber cycles through querying id 1-100 (msg_1 to msg_100)" << std::endl;
  std::cout << "Service: " << FLAGS_service_name << std::endl;
  std::cout << "===========================" << std::endl;
  
  // Start statistics reporter
  ::trpc::StartFiberDetached([&] {
    StatsReporterFiber();
  });
  
  // Start worker fibers
  // Each fiber will cycle through querying id 1-100
  ::trpc::FiberLatch workers_latch(FLAGS_concurrency);
  for (int i = 0; i < FLAGS_concurrency; ++i) {
    bool started = ::trpc::StartFiberDetached([&proxy, &workers_latch] {
      WorkerFiber(proxy);
      workers_latch.CountDown();
    });
    
    if (!started) {
      std::cerr << "Failed to start worker fiber " << i << std::endl;
      workers_latch.CountDown();
    }
  }
  
  // Run for specified duration
  ::trpc::FiberSleepFor(std::chrono::seconds(FLAGS_duration));
  
  // Stop all workers
  stop_flag.store(true, std::memory_order_relaxed);
  
  // Wait for all workers to finish
  workers_latch.Wait();
  
  // Final statistics
  uint64_t final_total = total_requests.load(std::memory_order_relaxed);
  uint64_t final_success = success_requests.load(std::memory_order_relaxed);
  uint64_t final_failed = failed_requests.load(std::memory_order_relaxed);
  uint64_t final_total_latency_us = total_latency_us.load(std::memory_order_relaxed);
  uint64_t final_min_latency_us = min_latency_us.load(std::memory_order_relaxed);
  uint64_t final_max_latency_us = max_latency_us.load(std::memory_order_relaxed);
  
  double avg_qps = FLAGS_duration > 0 ? (double)final_total / FLAGS_duration : 0.0;
  double success_rate = final_total > 0 ? (100.0 * final_success / final_total) : 0.0;
  double avg_latency_ms = final_success > 0 ? 
      (final_total_latency_us / 1000.0 / final_success) : 0.0;
  
  // Calculate percentiles from samples
  std::lock_guard<std::mutex> lock(samples_mutex);
  uint64_t p50_latency_us = 0, p90_latency_us = 0, p99_latency_us = 0;
  if (!latency_samples.empty()) {
    p50_latency_us = CalculatePercentile(latency_samples, 0.50);
    p90_latency_us = CalculatePercentile(latency_samples, 0.90);
    p99_latency_us = CalculatePercentile(latency_samples, 0.99);
  }
  
  std::cout << "\n=== Final Statistics ===" << std::endl;
  std::cout << "Total Requests: " << final_total << std::endl;
  std::cout << "Success Requests: " << final_success << std::endl;
  std::cout << "Failed Requests: " << final_failed << std::endl;
  std::cout << "Average QPS: " << std::fixed << std::setprecision(2) << avg_qps << std::endl;
  std::cout << "Success Rate: " << std::fixed << std::setprecision(2) << success_rate << "%" << std::endl;
  std::cout << "\n=== Latency Statistics ===" << std::endl;
  std::cout << "Average Latency: " << std::fixed << std::setprecision(2) 
            << avg_latency_ms << " ms" << std::endl;
  if (final_min_latency_us != UINT64_MAX) {
    std::cout << "Min Latency: " << std::fixed << std::setprecision(2) 
              << (final_min_latency_us / 1000.0) << " ms" << std::endl;
  }
  std::cout << "Max Latency: " << std::fixed << std::setprecision(2) 
            << (final_max_latency_us / 1000.0) << " ms" << std::endl;
  if (!latency_samples.empty()) {
    std::cout << "P50 Latency: " << std::fixed << std::setprecision(2) 
              << (p50_latency_us / 1000.0) << " ms" << std::endl;
    std::cout << "P90 Latency: " << std::fixed << std::setprecision(2) 
              << (p90_latency_us / 1000.0) << " ms" << std::endl;
    std::cout << "P99 Latency: " << std::fixed << std::setprecision(2) 
              << (p99_latency_us / 1000.0) << " ms" << std::endl;
    std::cout << "Samples: " << latency_samples.size() << std::endl;
  }
  std::cout << "=========================" << std::endl;
  
  return 0;
}

void ParseClientConfig(int argc, char* argv[]) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::CommandLineFlagInfo info;
  if (GetCommandLineFlagInfo("client_config", &info) && info.is_default) {
    TRPC_FMT_ERROR("start client with config, for example:{} --client_config=/client/config/filepath", argv[0]);
    exit(-1);
  }
  
  std::cout << "FLAGS_service_name: " << FLAGS_service_name << std::endl;
  std::cout << "FLAGS_client_config: " << FLAGS_client_config << std::endl;

  int ret = ::trpc::TrpcConfig::GetInstance()->Init(FLAGS_client_config);
  if (ret != 0) {
    std::cerr << "load client_config failed." << std::endl;
    exit(-1);
  }
}

int main(int argc, char* argv[]) {
  ParseClientConfig(argc, argv);
  // If the business code is running in trpc pure client mode,
  // the business code needs to be running in the `RunInTrpcRuntime` function
  // This function can be seen as a program entry point and should be called only once.
  return ::trpc::RunInTrpcRuntime([]() {
    return Run();
  });
}
