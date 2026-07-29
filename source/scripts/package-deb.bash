#!/usr/bin/env bash
# scripts/package-deb.bash <version> - 把已构建好的 install-<arch>/ 打成 .deb,
# 内容重映射到包内的 /usr/local/ 前缀下. 库/头/CMake config 的相对结构原样保留,
# 故可重定位的 *-config.cmake 装机后仍能被 find_package 正常解析.
#
# 刻意与构建/测试分离: install-<arch>/ 是构建产物, 且 run-tests.bash 就地把它当
# CMAKE_PREFIX_PATH 引用 (libstdc++ 校验依赖此路径), 故本脚本只「读」它, 不改动.
#
# 用法: bash scripts/package-deb.bash <version>
#   <version>  写入 deb control 的 Version 字段. dpkg 硬性要求数字开头, 故调用方
#              (CI) 已从 release tag 剥掉开头的 'v' (tag 本身保持 v… 不变).
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.bash"

VERSION="${1:?usage: package-deb.bash <version>}"
ARCH="$(detect_arch)"
PREFIX="${REPO_ROOT}/install-${ARCH}"
[ -d "${PREFIX}" ] || { echo "缺少 ${PREFIX}; 请先运行 build-fastdds.bash" >&2; exit 1; }

# deb control 的 Architecture 字段须用 dpkg 规范的架构名 (dpkg 据此校验/安装);
# 文件名则保留仓库惯用的 x64 / arm64 写法 (纯命名, 不影响 dpkg 执行).
case "${ARCH}" in
    x64)   DEB_ARCH="amd64" ;;
    arm64) DEB_ARCH="arm64" ;;
    *)     echo "unsupported arch: ${ARCH}" >&2; exit 1 ;;
esac

PKG="urpc2"

# Maintainer 是 deb 必填字段. 由调用方 (CI) 经环境变量传入真实来源 (仓库地址);
# 独立运行且未设时退回本机 user@host, 不编造联系方式.
MAINTAINER="${DEB_MAINTAINER:-$(id -un)@$(hostname)}"

# 把一个已布置好 usr/local/ 的 stage 目录打成 deb.
#   $1 variant  文件名里的变体标签 (all / necessary), 也用于 control 的 Description.
#   $2 stage    stage 目录 (含 usr/local/ 内容; DEBIAN/ 由本函数补齐).
#   $3 desc     control 的 Description 摘要行.
build_deb() {
    local variant="$1" stage="$2" desc="$3"
    local deb="${REPO_ROOT}/${PKG}-${variant}-v${VERSION}-${ARCH}.deb"
    rm -f "${deb}"
    mkdir -p "${stage}/DEBIAN"

    cat > "${stage}/DEBIAN/control" <<EOF
Package: ${PKG}
Version: ${VERSION}
Architecture: ${DEB_ARCH}
Maintainer: ${MAINTAINER}
Section: libs
Priority: optional
Description: ${desc}
 基于 Fast DDS 的 urpc2 库 (urpc2 / urpc2_rbk) 及示例 urpc2_rbk_example,
 安装到 /usr/local. 变体: ${variant}.
EOF

    # 装机后避让 rbk / spg 环境中可能已有的 urpc2 库. 显式列出路径,
    # 不依赖 /bin/sh 不一定支持的 brace expansion.
    cat > "${stage}/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e

if [ "${1:-}" = configure ]; then
    for library in \
        /opt/data/rbk/3rdlib/liburpc2.so \
        /opt/data/rbk/3rdlib/liburpc2_rbk.so \
        /opt/data/spg/3rdlib/liburpc2.so \
        /opt/data/spg/3rdlib/liburpc2_rbk.so
    do
        if [ -e "${library}" ] || [ -L "${library}" ]; then
            mv -f -- "${library}" "${library}_bak"
        fi
    done
fi

ldconfig
EOF
    chmod 0755 "${stage}/DEBIAN/postinst"

    # /usr/local/lib 已在默认 ld 搜索路径内 (Ubuntu 的 /etc/ld.so.conf.d/libc.conf);
    # 卸机后刷新 ld 缓存.
    cat > "${stage}/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e
ldconfig
EOF
    chmod 0755 "${stage}/DEBIAN/postrm"

    # --root-owner-group: 包内文件强制归 root:root, 无需 fakeroot (跳过宿主实际属主).
    dpkg-deb --root-owner-group --build "${stage}" "${deb}"
    echo ">> built ${deb}"
    dpkg-deb --info "${deb}"
    # 内容预览仅作诊断: head 提前关管道会让上游 dpkg-deb 收到写错误而非零退出,
    # 在 pipefail 下会误判整步失败, 故此预览局部关掉 pipefail 并容忍其退出码.
    ( set +o pipefail; dpkg-deb --contents "${deb}" | head -n 20 ) || true
}

# --- all: 完整包, install-<arch>/ 的全部内容 (bin/ lib/ include/ …) ---
STAGE_ALL="${REPO_ROOT}/deb-all-${ARCH}"
rm -rf "${STAGE_ALL}"
mkdir -p "${STAGE_ALL}/usr/local"
cp -a "${PREFIX}/." "${STAGE_ALL}/usr/local/"
build_deb all "${STAGE_ALL}" "eProsima Fast DDS + Fast-CDR + foonathan_memory + urpc2"

# --- necessary: 由 all 裁剪而来, 删掉整个 Fast DDS 技术栈 (fastdds / Fast-CDR /
#     foonathan_memory) 里除动态链接库以外的所有文件. 只留 libfastdds.so* 与
#     libfastcdr.so* (运行期链接所需), 以及 urpc2 自身 (头/库/cmake/示例, 含其
#     bundled 的 nlohmann 头). foonathan_memory 仅有静态库 .a, 无动态库, 故全删. ---
STAGE_NEC="${REPO_ROOT}/deb-necessary-${ARCH}"
rm -rf "${STAGE_NEC}"
cp -a "${STAGE_ALL}" "${STAGE_NEC}"
rm -rf "${STAGE_NEC}/DEBIAN"   # control/hook 由 build_deb 按 necessary 变体重写
NEC_LOCAL="${STAGE_NEC}/usr/local"
rm -rf \
    "${NEC_LOCAL}/include/fastdds" \
    "${NEC_LOCAL}/include/fastcdr" \
    "${NEC_LOCAL}/include/foonathan_memory" \
    "${NEC_LOCAL}/share/fastdds" \
    "${NEC_LOCAL}/share/fastcdr" \
    "${NEC_LOCAL}/share/foonathan_memory" \
    "${NEC_LOCAL}/lib/cmake/fastcdr" \
    "${NEC_LOCAL}/lib/foonathan_memory"
rm -f "${NEC_LOCAL}"/lib/libfoonathan_memory-*.a
build_deb necessary "${STAGE_NEC}" "urpc2 (含 Fast DDS 运行期动态库, 不含开发文件)"
