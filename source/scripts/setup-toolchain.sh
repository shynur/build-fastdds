#!/usr/bin/env bash
# scripts/setup-toolchain.sh - 在构建容器里安装工具链 (由 config.ini 决定编译器).
#
#   * cxx-{arch}      -> 编译 Fast DDS 相关库的编译器 (g++)
#   * test-cxx-{arch} -> 编译测试用例的编译器 (clang)
#   * CMake 固定使用 Kitware 官方 3.31.12 (Ubuntu 18.04 自带的 3.10 太旧).
#
# 只安装「一个」g++ 版本, 从而保证 clang 自动选中的 libstdc++ 与该 g++ 一致
# (clang 会选系统里版本号最高的 GCC; 只装一个就没有歧义).
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

export DEBIAN_FRONTEND=noninteractive

ARCH="$(detect_arch)"
CXX_SPEC="$(cfg "cxx-${ARCH}")"
TESTCXX_SPEC="$(cfg "test-cxx-${ARCH}")"

echo ">> arch=${ARCH}  cxx=${CXX_SPEC}  test-cxx=${TESTCXX_SPEC}"

apt-get update
apt-get install -y --no-install-recommends \
    ca-certificates wget make git patch tar xz-utils file

apt_install_cxx "${CXX_SPEC}"       # g++-N (会带上匹配的 libstdc++-N-dev)
apt_install_cxx "${TESTCXX_SPEC}"   # clang-M

# --- CMake 3.31.12 (Kitware 官方自解压安装脚本) ---
CMAKE_VERSION="3.31.12"
CMAKE_URL="https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-$(kitware_arch).sh"
echo ">> installing CMake from ${CMAKE_URL}"
wget -qO /tmp/cmake.sh "${CMAKE_URL}"
sh /tmp/cmake.sh --skip-license --prefix=/usr/local
rm -f /tmp/cmake.sh

echo ">> toolchain ready:"
cmake --version | head -n1
"$(resolve_cxx_bin "${CXX_SPEC}")" --version | head -n1
"$(resolve_cxx_bin "${TESTCXX_SPEC}")" --version | head -n1
