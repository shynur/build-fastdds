#!/usr/bin/env bash
# scripts/fetch-foonathan.sh - 动态获取「所需版本」的 foonathan_memory 源码到
# third_party/foonathan_memory/ (取代原先的 git submodule).
#
# 版本「不写死」, 完全由 Fast-DDS submodule 的声明决定, 逐级解析:
#   Fast-DDS/fastdds.repos  声明 foonathan_memory_vendor 的 url + version
#     -> 浅克隆该 vendor (eProsima 对 foonathan/memory 的封装仓库), 读其 CMakeLists.txt
#       -> externalproject_add(foo_mem-ext ...) 里的 GIT_REPOSITORY + GIT_TAG
#          就是 foonathan/memory 真正的仓库 URL 与 tag
#   -> git clone 该 tag 的源码, 交给 build-fastdds.sh 编译.
# 因此升级 Fast-DDS submodule 时, foonathan_memory 版本自动跟随, 无需在别处同步.
#
# 需要 git 与网络; 在 setup-toolchain.sh 之后运行 (那步已装好 git/ca-certificates).
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

REPOS="${REPO_ROOT}/Fast-DDS/fastdds.repos"
[ -f "${REPOS}" ] || { echo "找不到 ${REPOS} (Fast-DDS submodule 未 checkout?)" >&2; exit 1; }

DEST="${REPO_ROOT}/third_party/foonathan_memory"

# --- 1. 从 fastdds.repos 解析 vendor 的 url + version ---
# 定位 foonathan_memory_vendor: 块, 取其后的第一个 url: / version: 字段.
# (awk 由 setup-toolchain 装的 gawk 提供, 故可放心用 [[:space:]] 这类 POSIX 字符类.)
vendor_field() {   # $1 = 字段名 (url / version)
    awk -v k="$1:" '
        /^[[:space:]]*foonathan_memory_vendor:/ { f = 1 }
        f && $1 == k { print $2; exit }
    ' "${REPOS}"
}
VENDOR_URL="$(vendor_field url)"
VENDOR_VER="$(vendor_field version)"
[ -n "${VENDOR_URL}" ] && [ -n "${VENDOR_VER}" ] \
    || { echo "无法从 ${REPOS} 解析 foonathan_memory_vendor 的 url/version" >&2; exit 1; }
echo ">> foonathan_memory_vendor (取自 fastdds.repos): ${VENDOR_URL} @ ${VENDOR_VER}" >&2

# --- 2. 浅克隆 vendor, 从其 CMakeLists 解析真正的 foonathan/memory url + tag ---
VENDOR_DIR="$(mktemp -d)"
trap 'rm -rf "${VENDOR_DIR}"' EXIT
git clone --depth 1 -b "${VENDOR_VER}" "${VENDOR_URL}" "${VENDOR_DIR}" >&2

VCM="${VENDOR_DIR}/CMakeLists.txt"
[ -f "${VCM}" ] || { echo "vendor 仓库缺少 CMakeLists.txt: ${VCM}" >&2; exit 1; }
# externalproject_add(foo_mem-ext ... GIT_REPOSITORY <url> GIT_TAG <tag> ...); 各参数分行.
# CMake 命令名大小写不敏感, 故定位块时统一小写比较.
ext_field() {      # $1 = GIT_REPOSITORY / GIT_TAG
    awk -v k="$1" '
        tolower($0) ~ /externalproject_add\([[:space:]]*foo_mem-ext/ { f = 1 }
        f && $1 == k { print $2; exit }
    ' "${VCM}"
}
MEM_URL="$(ext_field GIT_REPOSITORY)"
MEM_TAG="$(ext_field GIT_TAG)"
[ -n "${MEM_URL}" ] && [ -n "${MEM_TAG}" ] \
    || { echo "无法从 ${VCM} 解析 foonathan/memory 的 GIT_REPOSITORY/GIT_TAG" >&2; exit 1; }
echo ">> foonathan/memory (取自 vendor 的 externalproject): ${MEM_URL} @ ${MEM_TAG}" >&2

# --- 3. 克隆 memory 源码到 DEST (幂等) ---
# 记录已获取的 tag; 若目录已在同一 tag 上则跳过, 便于本地重复运行.
STAMP="${DEST}/.fetched-tag"
if [ -f "${DEST}/CMakeLists.txt" ] && [ "$(cat "${STAMP}" 2>/dev/null)" = "${MEM_TAG}" ]; then
    echo ">> 复用已获取的 foonathan_memory (${MEM_TAG}): ${DEST}" >&2
    exit 0
fi
rm -rf "${DEST}"
mkdir -p "$(dirname "${DEST}")"
git clone --depth 1 -b "${MEM_TAG}" "${MEM_URL}" "${DEST}" >&2
echo "${MEM_TAG}" > "${STAMP}"
echo ">> foonathan_memory 源码就绪: ${DEST} (${MEM_TAG})" >&2
