#!/usr/bin/env bash
# scripts/apply-patch.sh - "按需"把 patch 应用到 Fast DDS 源码树.
#
# 该 patch 只改 thirdparty/fastcdr 的公共头 config.h.in 里 TEMPLATE_SPEC 宏的定义:
#   * 原始定义在 clang 下展开为"类内 template<> 显式特化";
#   * patch 后在 clang 下也展开为空 (与 g++ 一致), 即普通成员函数.
#
# 是否需要 patch 取决于"测试编译器"是否接受类内显式特化:
#   * clang-6  拒绝  (error: explicit specialization ... in class scope) -> 需要 patch
#   * clang-12 接受                                                       -> 不需要 patch
# 我们不写死版本, 而是用一段最小代码现场探测 (probe): 编不过才打 patch.
# 这样既满足"如无必要勿用 patch", 又能自适应任意编译器.
#
# 注意: 对用 g++ 编译库本身而言, patch 前后展开结果完全相同, 是 no-op; 因此即便
# 打了 patch 也绝不会改变 g++ 构建出的库.
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

ARCH="$(detect_arch)"
TESTCXX_SPEC="$(cfg "test-cxx-${ARCH}")"
TESTCXX="$(resolve_cxx_bin "${TESTCXX_SPEC}")"
PATCH_FILE="${REPO_ROOT}/patches/fastcdr-in-class-explicit-spec.patch"

# --- probe: 测试编译器是否接受类内显式特化 (Fast-CDR TEMPLATE_SPEC 的本质) ---
probe="$(mktemp --suffix=.cpp)"
cat > "${probe}" <<'EOF'
#include <string>
struct Cdr {
    template<typename T> Cdr& serialize(const T&) { return *this; }
    template<> Cdr& serialize(const std::string&) { return *this; }
};
EOF
echo ">> probing whether ${TESTCXX} accepts in-class explicit specialization..."
if "${TESTCXX}" -std=c++14 -fsyntax-only "${probe}" >/dev/null 2>&1; then
    echo ">> ${TESTCXX} accepts it: patch NOT needed, skipping."
else
    echo ">> ${TESTCXX} rejects it: applying patch."
    patch -p1 --forward --directory="${REPO_ROOT}/Fast-DDS" < "${PATCH_FILE}" || {
        # --forward 在"已应用"时返回非 0; 用 reverse-dry-run 确认确实已应用才放行.
        patch -p1 --reverse --dry-run --directory="${REPO_ROOT}/Fast-DDS" < "${PATCH_FILE}" >/dev/null
    }
    grep -n "__GNUC__\|__clang__" \
        "${REPO_ROOT}/Fast-DDS/thirdparty/fastcdr/include/fastcdr/config.h.in"
fi
rm -f "${probe}"
