#pragma once

#include "trpc/common/trpc_app.h"

namespace trpc {
namespace benchmark {
namespace mysql {

class LruMysqlServer : public ::trpc::TrpcApp {
 public:
  int Initialize() override;

  void Destroy() override;
};

}  // namespace mysql
}  // namespace benchmark
}  // namespace trpc
