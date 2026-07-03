#!/usr/bin/env bash
# scripts/build-fastdds.sh - 用 config.ini 指定的 cxx (g++) 编译整套 Fast DDS 依赖,
# 全部安装到 install-{arch}/.
#
# 依赖构建顺序 (与既定的, 已验证可用的配方一致):
#   foonathan_memory (静态, PIC)  ->  Fast-CDR (共享)  ->  Fast-DDS (共享)
#
# Fast-CDR 单独构建并安装, 好处是测试用例可以直接 find_package(fastcdr); Fast-DDS
# 内部仍用 THIRDPARTY=ON 的捆绑 fastcdr (两者同源, 都来自已 patch 的 thirdparty/fastcdr).
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

ARCH="$(detect_arch)"
BUILD_TYPE="$(cfg CMAKE_BUILD_TYPE)"
CXX_SPEC="$(cfg "cxx-${ARCH}")"
CXX="$(resolve_cxx_bin "${CXX_SPEC}")"
CC="${CXX/g++/gcc}"          # g++-7 -> gcc-7 (Fast DDS 的 project() 含 C 语言)
PREFIX="${REPO_ROOT}/install-${ARCH}"
BUILD="${REPO_ROOT}/build-${ARCH}"
JOBS="$(nproc)"

echo ">> CXX=${CXX}  CC=${CC}  type=${BUILD_TYPE}  prefix=${PREFIX}  jobs=${JOBS}"
rm -rf "${PREFIX}" "${BUILD}"
mkdir -p "${PREFIX}"

# --- 1) foonathan_memory (静态库 + PIC) ---
cmake -S "${REPO_ROOT}/third_party/foonathan_memory" -B "${BUILD}/foonathan" \
    -DCMAKE_C_COMPILER="${CC}" \
    -DCMAKE_CXX_COMPILER="${CXX}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DBUILD_SHARED_LIBS=OFF \
    -DFOONATHAN_MEMORY_BUILD_TOOLS=OFF \
    -DFOONATHAN_MEMORY_BUILD_EXAMPLES=OFF \
    -DFOONATHAN_MEMORY_BUILD_TESTS=OFF
cmake --build "${BUILD}/foonathan" -j"${JOBS}" --target install

# --- 2) Fast-CDR (共享库) ---
cmake -S "${REPO_ROOT}/Fast-DDS/thirdparty/fastcdr" -B "${BUILD}/fastcdr" \
    -DCMAKE_C_COMPILER="${CC}" \
    -DCMAKE_CXX_COMPILER="${CXX}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
    -DBUILD_SHARED_LIBS=ON
cmake --build "${BUILD}/fastcdr" -j"${JOBS}" --target install

# --- 3) Fast-DDS (+ 捆绑的 asio / tinyxml2 / fastcdr) ---
cmake -S "${REPO_ROOT}/Fast-DDS" -B "${BUILD}/fastdds" \
    -DCMAKE_C_COMPILER="${CC}" \
    -DCMAKE_CXX_COMPILER="${CXX}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
    -DCMAKE_PREFIX_PATH="${PREFIX}" \
    -Dfoonathan_memory_DIR="${PREFIX}/lib/foonathan_memory/cmake" \
    -DBUILD_SHARED_LIBS=ON \
    -DSECURITY=OFF \
    -DNO_TLS=ON \
    -DSHM_TRANSPORT_DEFAULT=OFF \
    -DFASTDDS_STATISTICS=OFF \
    -DSTRICT_REALTIME=OFF \
    -DSQLITE3_SUPPORT=OFF \
    -DENABLE_OLD_LOG_MACROS=OFF \
    -DCOMPILE_TOOLS=OFF \
    -DTHIRDPARTY=ON \
    -DTHIRDPARTY_UPDATE=OFF \
    -DBUILD_TESTING=OFF
cmake --build "${BUILD}/fastdds" -j"${JOBS}" --target install

echo ">> installed artifacts under ${PREFIX}:"
find "${PREFIX}" \( -name '*.so*' -o -name '*.a' \) | sort
find "${PREFIX}" -name '*-config.cmake' | sort
