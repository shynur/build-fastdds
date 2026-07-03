#!/usr/bin/env bash
# scripts/apply-patch.sh - 「按需」把 patch 应用到 Fast DDS 源码树.
#
# 该 patch 只改 thirdparty/fastcdr 的公共头 config.h.in 里 TEMPLATE_SPEC 宏的定义:
#   * 原始定义在 clang 下展开为「类内 template<> 显式特化」, clang-6 会拒绝
#     (error: explicit specialization ... in class scope);
#   * patch 后在 clang 下也展开为空 (与 g++ 一致), 即普通成员函数.
#
# test-cxx 是 clang-6 时打这个 patch.
#
# 注意: 对用 g++ 编译库本身而言, patch 前后展开结果完全相同, 是 no-op; 因此即便
# 打了 patch 也绝不会改变 g++ 构建出的库.
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

ARCH="$(detect_arch)"
TESTCXX_SPEC="$(cfg "test-cxx-${ARCH}")"
PATCH_FILE="${REPO_ROOT}/patches/fastcdr-in-class-explicit-spec.patch"

# --- 按配置的 test-cxx 判断: 写死只有 clang-6 需要 patch ---
need_patch=0
case "${TESTCXX_SPEC}" in
    clang-6 | clang-6.* ) need_patch=1 ;;   # clang-6 / clang-6.0
esac

if [ "${need_patch}" -eq 0 ]; then
    echo ">> test-cxx is ${TESTCXX_SPEC}, not clang-6."
else
    echo ">> test-cxx is clang-6, applying patch."
    patch -p1 --forward --directory="${REPO_ROOT}/Fast-DDS" < "${PATCH_FILE}" || {
        # --forward 在「已应用」时返回非 0; 用 reverse-dry-run 确认确实已应用才放行.
        patch -p1 --reverse --dry-run --directory="${REPO_ROOT}/Fast-DDS" < "${PATCH_FILE}" >/dev/null
    }
    grep -n "__GNUC__\|__clang__" \
        "${REPO_ROOT}/Fast-DDS/thirdparty/fastcdr/include/fastcdr/config.h.in"
fi
