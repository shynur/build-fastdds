#!/usr/bin/env bash
# scripts/build-fastddsgen.sh - 从源码构建 eProsima Fast-DDS-Gen (fastddsgen).
#
# 版本「不写死」: 取自 Fast-DDS submodule 自带的 vcstool 清单 Fast-DDS/fastdds.repos
# 里 fastddsgen 的 version 字段 (与 fastdds / fastcdr 并列固定).  因此升级 Fast-DDS
# submodule 时, 配套的生成器版本自动跟随, 无需在别处同步.
#
# 需要 JDK (OpenJDK 11) 与 git.  生成器是 Java 工具, 故刻意与只装 g++/clang 的构建
# 容器分离 -- 本脚本在「另一套环境」(CI 的 generate 作业, 或本地开发机) 里运行.
#
# 输出契约:
#   - 所有日志走 stderr;
#   - stdout 「只」打印一行: 生成器 scripts/ 目录的绝对路径 (内含可执行的 fastddsgen 包装脚本).
#   用法: export PATH="$(bash scripts/build-fastddsgen.sh):$PATH"
#
# 缓存: 默认构建到 /tmp/fastddsgen-<版本>, 可用 FASTDDSGEN_DIR 覆盖; 若目录内已有产物则跳过重建.
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

REPOS="${REPO_ROOT}/Fast-DDS/fastdds.repos"
[ -f "${REPOS}" ] || { echo "找不到 ${REPOS} (Fast-DDS submodule 未 checkout?)" >&2; exit 1; }

# 解析 fastddsgen 的 version: 定位 fastddsgen: 块后的第一个 version: 字段.
GEN_VER="$(awk '/^[[:space:]]*fastddsgen:/{f=1} f&&/version:/{print $2; exit}' "${REPOS}")"
[ -n "${GEN_VER}" ] || { echo "无法从 ${REPOS} 解析 fastddsgen 版本" >&2; exit 1; }
echo ">> fastddsgen 版本 (取自 fastdds.repos): ${GEN_VER}" >&2

WORK="${FASTDDSGEN_DIR:-/tmp/fastddsgen-${GEN_VER}}"

if [ -x "${WORK}/scripts/fastddsgen" ] && [ -f "${WORK}/share/fastddsgen/java/fastddsgen.jar" ]; then
    echo ">> 复用已构建的生成器: ${WORK}" >&2
else
    echo ">> 构建 fastddsgen ${GEN_VER} 到 ${WORK}" >&2
    rm -rf "${WORK}"
    git clone --depth 1 -b "${GEN_VER}" \
        https://github.com/eProsima/Fast-DDS-Gen.git "${WORK}" >&2
    # 只需 idl-parser (构建必需); dds-types-test 仅测试用, 跳过.
    git -C "${WORK}" submodule update --init --depth 1 thirdparty/idl-parser >&2
    ( cd "${WORK}" && ./gradlew assemble --no-daemon ) >&2
fi

# stdout: 供调用方塞进 PATH.
echo "${WORK}/scripts"
