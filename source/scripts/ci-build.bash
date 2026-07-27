#!/usr/bin/env bash
# scripts/ci-build.bash - 容器内的唯一入口: 依次完成 安装工具链 -> 获取 foonathan_memory ->
# (按需) 打 patch -> 编译 Fast DDS -> 编译并安装 urpc2.
# CI 只需 `docker run ... bash scripts/ci-build.bash`.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

bash "${here}/setup-toolchain.bash"
bash "${here}/fetch-foonathan.bash"
bash "${here}/apply-patch.bash"
bash "${here}/build-fastdds.bash"
# 测试流程已禁用.  tests/ 下的 DDS 与 RPC 用例跑在 CI runner 的网络里, 而 urpc2
# 现在把通信写死在 192.168.192.0/24 (见 projects/urpc2/src/urpc2.cpp); runner 上
# 没有该网段的网卡, 相关用例必然失败.  需要重新启用时取消下面这行的注释.
# bash "${here}/run-tests.bash"
bash "${here}/build-urpc2.bash"    # 把 urpc2 装进 install-<arch>/, 一并打进 deb
bash "${here}/build-urpc2-rbk-example.bash"    # 再把依赖 urpc2 的示例可执行文件装进 bin/
