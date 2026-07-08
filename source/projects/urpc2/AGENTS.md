# AGENTS.md

请使用以下文档位置, 避免重复相同说明:
- 公共用户 API: `include/urpc2.hpp`
  - `urpc2::Urpc2`, `Handler`, `register_handler()`, and `call()` 上的 Doxygen 注释.
- 类型化封装 API: `include/urpc2_rbk.hpp`
  - `urpc2_rbk::serve()` 和 `call()` 上的 Doxygen 注释; 实现细节见 `src/urpc2_rbk.cpp` 的块注释.
- 架构和测试流程: `README.md`
  - 涵盖 IDL 路由, 生成代码边界, 构建命令, 单进程冒烟测试, 以及三容器网格测试.
- 内部实现说明: `src/urpc2.cpp`
  - 普通块注释描述 participant 生命周期, 服务器启动, 出站调用流程, 发现等待和处理器分发.
- 测试驱动逻辑: `examples/simple.cpp` 和 `examples/mesh_node.cpp`
  - 注释描述冒烟测试和单工作线程随机网格流量模型.

修改本项目时:
- 公共 API 发生变化时, 更新 `include/urpc2.hpp` 中的 Doxygen.
- 架构, 构建或测试流程发生变化时, 更新 `README.md`.
- 优先在相关实现文件中添加注释, 不要在其他位置重复较长的架构说明.
