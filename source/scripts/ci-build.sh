#!/usr/bin/env bash
# scripts/ci-build.sh - 容器内的唯一入口: 依次完成 安装工具链 -> 获取 foonathan_memory ->
# (按需) 打 patch -> 编译 Fast DDS -> 编译并运行测试.
# CI 只需 `docker run ... bash scripts/ci-build.sh`.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

bash "${here}/setup-toolchain.sh"
bash "${here}/fetch-foonathan.sh"     # 动态获取 foonathan_memory 源码 (需 setup 已装好 git)
bash "${here}/apply-patch.sh"
bash "${here}/build-fastdds.sh"
bash "${here}/run-tests.sh"
