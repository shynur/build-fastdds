# AGENTS.md

此目录是一个独立 CMake 项目, 用于测试所使用的 `urpc2` RPC-over-DDS 包装层.

请使用以下文档位置, 避免重复相同说明:

- 公共用户 API: `include/urpc2.hpp`
  - `urpc2::Urpc2`, `Handler`, `register_handler()` 和 `call()` 上的 Doxygen
    注释.
- 架构和测试流程: `README.md`
  - 涵盖 IDL 路由, 生成代码边界, 构建命令, 单进程冒烟测试, 以及三容器网格
    测试.
- 内部实现说明: `src/urpc2.cpp`
  - 普通块注释描述 participant 生命周期, 服务器启动, 出站调用流程, 发现等待
    和处理器分发.
- 测试驱动逻辑: `examples/simple.cpp` 和 `examples/mesh_node.cpp`
  - 注释描述冒烟测试和单工作线程随机网格流量模型.
- 生成的 Fast DDS 文件: `src/types/`
  - 将这些文件视为由 `src/types/processor.idl` 生成的产物.

修改本项目时:

- 公共 API 发生变化时, 更新 `include/urpc2.hpp` 中的 Doxygen.
- 架构, 构建或测试流程发生变化时, 更新 `README.md`.
- 优先在相关实现文件中添加注释, 不要在其他位置重复较长的架构说明.
- 除非用户明确要求, 否则不要在此目录中加入 CI 集成.
