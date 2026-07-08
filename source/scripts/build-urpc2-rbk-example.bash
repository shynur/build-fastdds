#!/usr/bin/env bash
# scripts/build-urpc2-rbk-example.bash - 构建并安装 projects/urpc2_rbk_example,
# 可执行文件装进 install-<arch>/bin/ (与 Fast DDS / urpc2 同一前缀), 随后被
# package-deb.bash 一并打进同一个 deb.
#
# 必须在 build-urpc2.bash 之后运行: 本项目 find_package(urpc2), 从 install-<arch>/
# 找到刚装好的 urpc2. 编译器取 test-cxx (clang), 与 tests/ 用例一致 (与 g++ 构建的
# 库共享同一份 libstdc++, 故可正常互链, 运行).
source "$(dirname "${BASH_SOURCE[0]}")/lib.bash"

ARCH="$(detect_arch)"
BUILD_TYPE="$(cfg CMAKE_BUILD_TYPE)"
TESTCXX_SPEC="$(cfg "test-cxx-${ARCH}")"
TESTCXX="$(resolve_cxx_bin "${TESTCXX_SPEC}")"
PREFIX="${REPO_ROOT}/install-${ARCH}"
BUILD="${REPO_ROOT}/build-${ARCH}/urpc2_rbk_example"
JOBS="$(nproc)"

[ -d "${PREFIX}/lib/cmake/urpc2" ] \
    || { echo "缺少 ${PREFIX} 里的 urpc2; 请先运行 build-urpc2.bash" >&2; exit 1; }

echo ">> building urpc2_rbk_example  CXX=${TESTCXX}  prefix=${PREFIX}"
cmake -S "${REPO_ROOT}/projects/urpc2_rbk_example" -B "${BUILD}" \
    -DCMAKE_CXX_COMPILER="${TESTCXX}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
    -DCMAKE_PREFIX_PATH="${PREFIX}"
cmake --build "${BUILD}" -j"${JOBS}" --target install

echo ">> urpc2_rbk_example installed under ${PREFIX}:"
find "${PREFIX}/bin" -name 'urpc2_rbk_example' | sort
