#!/usr/bin/env bash
# scripts/build-urpc2.bash - 构建并安装 projects/urpc2, 装进 install-<arch>/ (与 Fast DDS
# 同一前缀), 从而随后被 package-deb.bash 一并打进同一个 deb.
#
# 必须在 build-fastdds.bash 之后运行: urpc2 靠 find_package(fastdds/fastcdr) 从
# install-<arch>/ 找到那套 g++ 构建的库. 编译器同样取 cxx (g++), 与库共享 libstdc++.
#
# 只构建 install 目标 (urpc2 / urpc2_rbk 两个库及其依赖); examples 下的可执行文件不是
# install 依赖, --target install 不会牵连编译它们.
source "$(dirname "${BASH_SOURCE[0]}")/lib.bash"

ARCH="$(detect_arch)"
BUILD_TYPE="$(cfg CMAKE_BUILD_TYPE)"
CXX_SPEC="$(cfg "cxx-${ARCH}")"
CXX="$(resolve_cxx_bin "${CXX_SPEC}")"
CC="${CXX/g++/gcc}"
PREFIX="${REPO_ROOT}/install-${ARCH}"
BUILD="${REPO_ROOT}/build-${ARCH}/urpc2"
JOBS="$(nproc)"

[ -d "${PREFIX}" ] || { echo "缺少 ${PREFIX}; 请先运行 build-fastdds.bash" >&2; exit 1; }

echo ">> building urpc2  CXX=${CXX}  prefix=${PREFIX}"
cmake -S "${REPO_ROOT}/projects/urpc2" -B "${BUILD}" \
    -DCMAKE_C_COMPILER="${CC}" \
    -DCMAKE_CXX_COMPILER="${CXX}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
    -DCMAKE_PREFIX_PATH="${PREFIX}"
cmake --build "${BUILD}" -j"${JOBS}" --target install

echo ">> urpc2 installed under ${PREFIX}:"
find "${PREFIX}" \( -name 'liburpc2*.so*' -o -name 'urpc2*.cmake' -o -name 'urpc2*.hpp' \) | sort
