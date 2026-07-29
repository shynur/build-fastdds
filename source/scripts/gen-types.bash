#!/usr/bin/env bash
# scripts/gen-types.bash - 用 fastddsgen 为各测试用例的 IDL 生成类型/RPC 支持代码,
# 就地输出到 <test>/src/types/ (这些生成物不入库, 仅在此步产生).
#
# 前提: fastddsgen 已在 PATH 中 (见 build-fastddsgen.bash), 或用 FASTDDSGEN 指定其路径.
# fastddsgen 还需要一个 C 预处理器 (cpp); 若 PATH 中没有, 本脚本回退探测 gcc 附带的
# cpp-<ver> 并以 -ppPath 传入.
#
# 说明: fastddsgen 的 -d 会「保留输入文件的相对目录结构」, 若传带路径的 IDL 会生成嵌套
# 目录; 故这里改为「cd 进 IDL 所在目录再传文件名」, 输出平铺在该目录.
source "$(dirname "${BASH_SOURCE[0]}")/lib.bash"

GEN="${FASTDDSGEN:-fastddsgen}"
command -v "${GEN}" >/dev/null 2>&1 \
    || { echo "找不到 fastddsgen; 请先运行 build-fastddsgen.bash 并将其 scripts/ 加入 PATH" >&2; exit 1; }

# 预处理器: 优先 PATH 里的 cpp, 否则回退探测 cpp-<ver>.
PP_ARG=()
if ! command -v cpp >/dev/null 2>&1; then
    for c in cpp-17 cpp-13 cpp-12 cpp-11 cpp-10 cpp-9 cpp-7; do
        p="$(command -v "${c}" 2>/dev/null || true)"
        [ -n "${p}" ] && { PP_ARG=(-ppPath "${p}"); echo ">> 使用预处理器 ${p}" >&2; break; }
    done
fi

# gen_one <相对 REPO_ROOT 的目录> <IDL 文件名>
gen_one() {
    local dir="${REPO_ROOT}/$1" idl="$2"
    [ -f "${dir}/${idl}" ] || { echo "找不到 IDL: ${dir}/${idl}" >&2; exit 1; }
    echo ">> 生成 $1/${idl}" >&2
    ( cd "${dir}" && "${GEN}" -replace "${PP_ARG[@]}" "${idl}" )
}

gen_one "tests/dds/src/types" "HelloWorld.idl"
gen_one "tests/rpc/src/types" "calculator.idl"
gen_one "projects/urpc2/src/types" "processor.idl"

echo ">> 类型代码生成完成" >&2
