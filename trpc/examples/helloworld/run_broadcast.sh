#! /bin/bash

bazel build //trpc/examples/helloworld/...

killall helloworld_svr

# 启动下游多个节点
./bazel-bin/trpc/examples/helloworld/helloworld_svr --config=./trpc/examples/helloworld/broadcast_server_conf/trpc_cpp_server1.yaml &

./bazel-bin/trpc/examples/helloworld/helloworld_svr --config=./trpc/examples/helloworld/broadcast_server_conf/trpc_cpp_server2.yaml &

./bazel-bin/trpc/examples/helloworld/helloworld_svr --config=./trpc/examples/helloworld/broadcast_server_conf/trpc_cpp_server3.yaml &

./bazel-bin/trpc/examples/helloworld/helloworld_svr --config=./trpc/examples/helloworld/broadcast_server_conf/trpc_cpp_server4.yaml &

./bazel-bin/trpc/examples/helloworld/helloworld_svr --config=./trpc/examples/helloworld/broadcast_server_conf/trpc_cpp_server5.yaml &


# 全部广播每个节点都成功
sleep 1
./bazel-bin/trpc/examples/helloworld/test/future_client --run_broadcast=true --addr='0.0.0.0:15001,0.0.0.0:15002,0.0.0.0:15003,0.0.0.0:15004,0.0.0.0:15005'

sleep 1
./bazel-bin/trpc/examples/helloworld/test/fiber_client --run_broadcast=true --config=trpc/examples/helloworld/test/trpc_cpp_fiber_client.yaml --addr='0.0.0.0:15001,0.0.0.0:15002,0.0.0.0:15003,0.0.0.0:15004,0.0.0.0:15005'

# 指定客户端的address，模拟广播时节点成功某些失败
sleep 1
./bazel-bin/trpc/examples/helloworld/test/future_client --run_broadcast=true --addr='0.0.0.0:15001,0.0.0.0:10002'

sleep 1
./bazel-bin/trpc/examples/helloworld/test/fiber_client --run_broadcast=true --config=trpc/examples/helloworld/test/trpc_cpp_fiber_client.yaml --addr='0.0.0.0:15001,0.0.0.0:10002'

killall helloworld_svr
