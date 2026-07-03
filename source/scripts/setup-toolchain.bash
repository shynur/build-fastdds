#!/usr/bin/env bash
# scripts/setup-toolchain.bash - 在构建容器里安装工具链 (由 config.ini 决定编译器).
#
#   - cxx-<arch>      -> 编译 Fast DDS 相关库的编译器 (g++)
#   - test-cxx-<arch> -> 编译测试用例的编译器 (clang)
#   - CMake 固定使用 Kitware 官方 release (Ubuntu 18.04 自带的 3.10 太旧); 版本见下方 CMAKE_VERSION.
#
# 只安装「一个」g++ 版本, 从而保证 clang 自动选中的 libstdc++ 与该 g++ 一致
# (clang 会选系统里版本号最高的 GCC; 只装一个就没有歧义).
source "$(dirname "${BASH_SOURCE[0]}")/lib.bash"

export DEBIAN_FRONTEND=noninteractive

ARCH="$(detect_arch)"
CXX_SPEC="$(cfg "cxx-${ARCH}")"
TESTCXX_SPEC="$(cfg "test-cxx-${ARCH}")"

echo ">> arch=${ARCH}  cxx=${CXX_SPEC}  test-cxx=${TESTCXX_SPEC}"

apt-get update
apt-get install -y --no-install-recommends \
    ca-certificates wget make git patch tar xz-utils file gawk

# 用 gawk 作为容器的 awk (ubuntu:18.04 自带的是 mawk 1.3.3, 不支持 [[:space:]] 等
# POSIX 字符类, 脚本里的解析会静默失配). 装上 gawk 并设为默认, 让所有 awk 调用走 gawk.
update-alternatives --set awk /usr/bin/gawk

apt_install_cxx "${CXX_SPEC}"       # g++-N (会带上匹配的 libstdc++-N-dev)
apt_install_cxx "${TESTCXX_SPEC}"   # clang-M

# --- CMake (Kitware 官方自解压安装脚本) ---
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
