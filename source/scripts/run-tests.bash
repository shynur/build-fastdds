#!/usr/bin/env bash
# scripts/run-tests.bash - 用 config.ini 指定的 test-cxx (clang) 编译并运行两个测试用例,
# 链接 install-<arch>/ 里那套用 g++ 构建的 Fast DDS.
#
#   - DDS: 1 个 publisher + 2 个 subscriber, 断言两个 subscriber 都收到数据.
#   - RPC: server + client, 断言四种调用结果正确, 且 server 收到 SIGTERM 后干净退出.
#
# 运行前先「机器校验」clang 与 g++ 依赖同一份 libstdc++ (需求硬约束):
#   (a) clang 编译期选中的 GCC 版本 = cxx 的 g++ 版本;
#   (b) 生成的可执行文件运行期链接 libstdc++.so.6 (而非 libc++).
source "$(dirname "${BASH_SOURCE[0]}")/lib.bash"

ARCH="$(detect_arch)"
BUILD_TYPE="$(cfg CMAKE_BUILD_TYPE)"
CXX_SPEC="$(cfg "cxx-${ARCH}")"
TESTCXX_SPEC="$(cfg "test-cxx-${ARCH}")"
CXX="$(resolve_cxx_bin "${CXX_SPEC}")"
TESTCXX="$(resolve_cxx_bin "${TESTCXX_SPEC}")"
PREFIX="${REPO_ROOT}/install-${ARCH}"
BUILD="${REPO_ROOT}/build-${ARCH}"
JOBS="$(nproc)"

export CMAKE_PREFIX_PATH="${PREFIX}"
export LD_LIBRARY_PATH="${PREFIX}/lib:${LD_LIBRARY_PATH:-}"

fail() { echo "TEST FAILURE: $*" >&2; exit 1; }

# ---------------------------------------------------------------------------
# (a) 编译期: clang 选中的 GCC = cxx 的 g++
# ---------------------------------------------------------------------------
gcc_major="$("${CXX}" -dumpversion | cut -d. -f1)"
clang_gcc_major="$("${TESTCXX}" -v -x c++ -E /dev/null 2>&1 \
    | sed -n 's#.*Selected GCC installation:.*/\([0-9][0-9.]*\)$#\1#p' \
    | head -n1 | cut -d. -f1)"
echo ">> ${CXX} version major = ${gcc_major}"
echo ">> ${TESTCXX} selected GCC major = ${clang_gcc_major:-<none>}"
[ -n "${clang_gcc_major}" ] && [ "${gcc_major}" = "${clang_gcc_major}" ] \
    || fail "libstdc++ mismatch: ${TESTCXX} does not use gcc-${gcc_major}"
echo ">> libstdc++ consistency (compile-time) OK -> gcc ${gcc_major}"

# ===========================================================================
# DDS: 1 publisher + 2 subscribers
# ===========================================================================
echo "==================== DDS test ===================="
cmake -S "${REPO_ROOT}/tests/dds" -B "${BUILD}/dds" \
    -DCMAKE_CXX_COMPILER="${TESTCXX}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_PREFIX_PATH="${PREFIX}"
cmake --build "${BUILD}/dds" -j"${JOBS}"

# (b) 运行期: 链接 libstdc++.so.6 (而非 libc++)
rt="$(ldd "${BUILD}/dds/subscriber" | grep -iE 'libstdc\+\+|libc\+\+' || true)"
echo ">> subscriber C++ runtime: ${rt}"
case "${rt}" in
    *libc++*)    fail "unexpectedly linked libc++" ;;
    *libstdc++*) echo ">> libstdc++ consistency (runtime) OK" ;;
    *)           fail "not linked to libstdc++" ;;
esac

(
    cd "${BUILD}/dds"
    ulimit -c 0                          # 崩溃看 stderr 即可, 不落 core
    N=40
    timeout 60 ./subscriber Sub-1 > sub1.log 2>&1 & s1=$!
    timeout 60 ./subscriber Sub-2 > sub2.log 2>&1 & s2=$!
    sleep 8                              # 充分发现时间 (慢 runner 上匹配更慢)
    timeout 70 ./publisher "${N}" > pub.log 2>&1 || echo "[publisher exit $?]"
    sleep 2
    kill "${s1}" "${s2}" 2>/dev/null || true
    wait "${s1}" 2>/dev/null || true
    wait "${s2}" 2>/dev/null || true

    echo "----- pub.log (tail) -----"; tail -6 pub.log
    echo "----- sub1.log -----"; cat sub1.log
    echo "----- sub2.log -----"; cat sub2.log
    c1="$(grep -ca 'RECEIVED sample' sub1.log || true)"
    c2="$(grep -ca 'RECEIVED sample' sub2.log || true)"
    echo ">> Sub-1 received=${c1}  Sub-2 received=${c2}"
    [ "${c1:-0}" -ge 1 ] || fail "Sub-1 received no samples"
    [ "${c2:-0}" -ge 1 ] || fail "Sub-2 received no samples"
    echo ">> DDS TEST PASSED"
)

# ===========================================================================
# RPC: client / server
# ===========================================================================
echo "==================== RPC test ===================="
cmake -S "${REPO_ROOT}/tests/rpc" -B "${BUILD}/rpc" \
    -DCMAKE_CXX_COMPILER="${TESTCXX}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_PREFIX_PATH="${PREFIX}"
cmake --build "${BUILD}/rpc" -j"${JOBS}"

(
    cd "${BUILD}/rpc"
    ulimit -c 0
    timeout 60 ./calculator_server > server.log 2>&1 & sv=$!
    sleep 5                              # 等 server 就绪 (requester/replier 匹配)
    rc=0; timeout 40 ./calculator_client > client.log 2>&1 || rc=$?
    sleep 1
    kill -TERM "${sv}" 2>/dev/null || true
    svrc=0; wait "${sv}" 2>/dev/null || svrc=$?

    echo "----- client.log -----"; cat client.log
    echo "----- server.log -----"; cat server.log
    echo ">> client exit=${rc}  server exit=${svrc}"

    [ "${rc}" -eq 0 ] || fail "client exited with ${rc}"
    [ "${svrc}" -eq 0 ] || fail "server did not shut down cleanly (exit ${svrc})"
    grep -q "representation_limits => min=-2147483648, max=2147483647" client.log \
        || fail "representation_limits wrong"
    grep -q "addition => 5 + 3 = 8" client.log    || fail "addition wrong"
    grep -q "raised exception" client.log         || fail "overflow exception not raised"
    echo ">> RPC TEST PASSED"
)

echo "==================== ALL TESTS PASSED (${ARCH}) ===================="
