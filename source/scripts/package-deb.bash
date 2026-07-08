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

# 架构后缀 -> deb 规范的架构名.
case "${ARCH}" in
    x64)   DEB_ARCH="amd64" ;;
    arm64) DEB_ARCH="arm64" ;;
    *)     echo "unsupported arch: ${ARCH}" >&2; exit 1 ;;
esac

PKG="urpc2"
STAGE="${REPO_ROOT}/deb-${ARCH}"
DEB="${REPO_ROOT}/${PKG}_${VERSION}_${DEB_ARCH}.deb"

rm -rf "${STAGE}" "${DEB}"
mkdir -p "${STAGE}/usr/local" "${STAGE}/DEBIAN"

# install-<arch>/ 的内容 (bin/ lib/ include/ …) 原样搬进包内 /usr/local/ 下.
cp -a "${PREFIX}/." "${STAGE}/usr/local/"

cat > "${STAGE}/DEBIAN/control" <<EOF
Package: ${PKG}
Version: ${VERSION}
Architecture: ${DEB_ARCH}
Maintainer: build-fastdds CI <noreply@example.com>
Section: libs
Priority: optional
Description: eProsima Fast DDS + Fast-CDR + foonathan_memory + urpc2 (g++ build)
 用 g++ 构建的 Fast DDS 及其依赖 (Fast-CDR / foonathan_memory), 以及基于其上的
 urpc2 库 (urpc2 / urpc2_rbk), 安装到 /usr/local. 由 build-fastdds CI 自动打包.
EOF

# /usr/local/lib 已在默认 ld 搜索路径内 (Ubuntu 的 /etc/ld.so.conf.d/libc.conf);
# 装/卸机后刷新 ld 缓存.
for hook in postinst postrm; do
    cat > "${STAGE}/DEBIAN/${hook}" <<'EOF'
#!/bin/sh
set -e
ldconfig
EOF
    chmod 0755 "${STAGE}/DEBIAN/${hook}"
done

# --root-owner-group: 包内文件强制归 root:root, 无需 fakeroot (跳过宿主实际属主).
dpkg-deb --root-owner-group --build "${STAGE}" "${DEB}"
echo ">> built ${DEB}"
dpkg-deb --info "${DEB}"
dpkg-deb --contents "${DEB}" | head -n 20
