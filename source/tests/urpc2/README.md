# urpc2

`urpc2` 是一个小型 CMake 项目, 将 Fast DDS RPC 包装为名为
`urpc2::Urpc2` 的直接 C++ 门面. 它有意保持简单: 用户创建一个具有唯一
名称的端点, 在端点上注册字符串到字符串的处理器, 然后调用另一个具名端点
上的处理器.

请求和响应字符串对框架来说是不透明的. 示例使用形似 JSON 的文本, 但本项目
不会解析 JSON, 也不依赖 JSON 库.

## 公共 API

公共 API 在 `include/urpc2.hpp` 中通过 Doxygen 注释记录.

要点:

- `Urpc2{name}` 会创建一个服务名为 `name` 的 DDS RPC 服务.
- 端点名称在 DDS 域内必须唯一.
- 处理器名称只在所属端点内有效.
- `register_handler()` 会保存或替换一个处理器.
- `call(receiver_name, handler_name, args)` 会同步等待回复.

## 架构

IDL 文件是 `src/types/processor.idl`:

```idl
module urpc2 {
    interface Processor {
        string router(in string handler_name, in string args);
    };
};
```

生成的 Fast DDS RPC 代码位于 `src/types/`. 生成的服务只有一个操作:
`Processor::router()`. `Urpc2` 将该操作用作一个小型应用层路由器:

1. 调用方传入接收端点名称和处理器名称.
2. 生成的客户端调用接收方服务的 `router()` 操作.
3. 接收方的生成服务器调用 `Urpc2::Impl::dispatch()`.
4. `dispatch()` 在本地处理器注册表中查找处理器名称.
5. 被选中的处理器接收不透明参数字符串, 并返回不透明结果字符串.

`src/urpc2.cpp` 中的当前实现更重视清晰性而不是性能:

- 每个 `Urpc2` 拥有一个服务器侧 `DomainParticipant`.
- 服务器在一个后台线程上运行.
- 每次出站调用都会创建一个短生命周期的 participant 和生成客户端.
- 发送请求前会使用固定的发现等待时间.

在第一版用于正确性测试时, 这避免了共享长生命周期的生成客户端. 后续版本可以
用缓存客户端以及显式的发现/匹配管理替换这一做法.

## 构建

将本项目作为独立 CMake 项目配置. 让 CMake 指向 Fast DDS 安装前缀:

```sh
cmake -S source/tests/urpc2 -B /tmp/urpc2-build \
  -G Ninja \
  -DCMAKE_PREFIX_PATH=/opt/install-x64 \
  -DCMAKE_CXX_COMPILER=clang++-6.0 \
  -DCMAKE_BUILD_TYPE=Release

cmake --build /tmp/urpc2-build -j2
```

本项目会构建:

- `urpc2_processor_gen`: 生成 RPC 代码的静态库.
- `urpc2`: 静态包装库.
- `urpc2_simple`: 单进程冒烟示例.
- `urpc2_mesh_node`: 用于多进程测试的单进程网格节点.

## 测试

### 单进程冒烟测试

`examples/simple.cpp` 会在一个进程中启动两个端点:

- `alice` 注册 `sub`.
- `bob` 注册 `add`.
- 每个端点都通过端点名称和处理器名称调用另一个端点.

运行:

```sh
LD_LIBRARY_PATH=/opt/install-x64/lib:/opt/install-x64/lib64 \
  /tmp/urpc2-build/urpc2_simple
```

### 三容器网格测试

`examples/mesh_node.cpp` 是多进程测试驱动. 启动三个容器, 并在每个容器中运行
两个节点进程:

- `urpc2-mesh-1`: `node1`, `node2`
- `urpc2-mesh-2`: `node3`, `node4`
- `urpc2-mesh-3`: `node5`, `node6`

每个进程都会:

1. 创建 `Urpc2{node_name}`.
2. 注册相同的 `echo` 处理器.
3. 等待对等节点启动.
4. 运行多轮调用.
5. 每轮打乱五个对等节点名称.
6. 按顺序调用对等节点, 并在调用之间加入短暂的随机间隔.
7. 完成出站调用后继续短暂提供服务.

关键细节是, 每个进程只有一个活动调用循环, 但六个进程会并发运行. 这样无需在
单个进程内添加多个客户端工作线程, 也能产生交错且乱序的 RPC 流.

节点命令示例:

```sh
LD_LIBRARY_PATH=/opt/install-x64/lib:/opt/install-x64/lib64 \
  /work/urpc2-build/urpc2_mesh_node \
  node1 node1 node2 node3 node4 node5 node6 --rounds=3
```

单个节点的预期日志形态:

```text
READY node1 peers=5 rounds=3
ROUND node1 round=1
CALL node1 -> node4 round=1 call=1 attempt=1
OK node1 -> node4 round=1 call=1 attempt=1
...
DONE node1
```

对于 `--rounds=3`, 每个节点应产生:

- `CALL`: 15
- `OK`: 15
- `DONE`: 1
- `RETRY`, `FAIL`, `ERROR`, `TERMINATE`: 0

日志会写入每个容器内的 `/work/mesh-logs/*.log`.

## 生成文件

`src/types/` 下的文件由 `processor.idl` 生成. 除非正在刷新生成输出本身, 否则
不要手动编辑这些文件.
