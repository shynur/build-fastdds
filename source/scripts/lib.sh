# scripts/lib.sh - 被其它脚本 source 的公共函数库 (纯 POSIX-ish bash)
#
# 约定:
#   * config.ini 是「扁平」的 key=value (无 section), 用 cfg() 读取.
#   * 架构后缀 x64 / arm64 由 detect_arch() 根据 uname -m 推断.
#   * 编译器字段 (g++-7 / clang-6 / clang-12) 通过 resolve_* 映射成
#     实际的可执行文件名与 apt 包名 (Ubuntu 上 clang 6 的包/程序名带 .0 后缀).

set -euo pipefail

# 仓库根目录 (本文件位于 <root>/scripts/lib.sh)
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# 读取 config.ini 中某个 key 的值 (去除首尾空白与行内注释).
cfg() {
    local key="$1" file="${2:-$REPO_ROOT/config.ini}"
    sed -n "s/^[[:space:]]*${key}[[:space:]]*=[[:space:]]*\([^#]*\).*/\1/p" "$file" \
        | head -n1 \
        | sed 's/[[:space:]]*$//'
}

# uname -m -> 配置里使用的架构后缀.
detect_arch() {
    case "$(uname -m)" in
        x86_64) echo "x64" ;;
        aarch64 | arm64) echo "arm64" ;;
        *) echo "unsupported arch: $(uname -m)" >&2; return 1 ;;
    esac
}

# Kitware 官方 release 里使用的架构名.
kitware_arch() {
    case "$(uname -m)" in
        x86_64) echo "x86_64" ;;
        aarch64 | arm64) echo "aarch64" ;;
        *) echo "unsupported arch: $(uname -m)" >&2; return 1 ;;
    esac
}

# 编译器字段 -> C++ 可执行文件名.
#   g++-7    -> g++-7
#   clang-6  -> clang++-6.0 (若不存在则退回 clang++-6)
#   clang-12 -> clang++-12
resolve_cxx_bin() {
    local spec="$1"
    case "$spec" in
        g++-*)
            echo "$spec"
            ;;
        clang-*)
            local v="${spec#clang-}"
            local c
            for c in "clang++-${v}" "clang++-${v}.0"; do
                if command -v "$c" >/dev/null 2>&1; then
                    echo "$c"
                    return 0
                fi
            done
            echo "cannot locate C++ compiler for '$spec'" >&2
            return 1
            ;;
        *)
            echo "unknown compiler spec: $spec" >&2
            return 1
            ;;
    esac
}

# 编译器字段 -> apt 包名 (可能有多个候选, 逐个尝试).
apt_install_cxx() {
    local spec="$1"
    case "$spec" in
        g++-*)
            apt-get install -y --no-install-recommends "$spec"
            ;;
        clang-*)
            local v="${spec#clang-}"
            # Ubuntu 18.04 的包名是 clang-6.0; 20.04 是 clang-12.
            apt-get install -y --no-install-recommends "clang-${v}" \
                || apt-get install -y --no-install-recommends "clang-${v}.0"
            ;;
        *)
            echo "unknown compiler spec: $spec" >&2
            return 1
            ;;
    esac
}
