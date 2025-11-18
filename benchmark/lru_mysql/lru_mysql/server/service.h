#pragma once

#include "lru_mysql.trpc.pb.h"

namespace trpc {
namespace benchmark {
namespace mysql {

class GreeterServiceImpl : public ::trpc::benchmark::mysql::Greeter {
 public:
  ::trpc::Status GetID(::trpc::ServerContextPtr context, const ::trpc::benchmark::mysql::IDRequest* request, ::trpc::benchmark::mysql::IDReply* reply) override;
};

}  // namespace mysql
}  // namespace benchmark
}  // namespace trpc
