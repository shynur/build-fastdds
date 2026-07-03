# 测试

DDS (`dds/`) 与 RPC (`rpc/`) 两个用例.  每个用例只提交 IDL 与手写源; 由 `fastddsgen`
从 IDL 生成的类型/RPC 代码 **不入库** (见 `.gitignore`), 于 CI 期间现生成.

## 目录布局

两个用例统一采用:

- `<test>/src/` -- 手写源 (publisher / subscriber / client / server 及各自的 `CMakeLists.txt`);
- `<test>/src/types/` -- IDL 与其生成物 (`*.hpp` / `*.cxx` / `*.ipp`).

## 生成器 (`fastddsgen`)

**版本不写死**: 取自 Fast-DDS submodule 自带的 `source/Fast-DDS/fastdds.repos`
(与 `fastdds` / `fastcdr` 并列固定), 升级 Fast-DDS submodule 时自动跟随.

**两段式**: `fastddsgen` 是 Java 工具, 故与只装 `g++`/clang 的构建容器分离.  CI 的 `generate`
作业 (JDK 环境) 构建生成器并生成代码, 打包为 artifact 喂给各架构的 `build` 作业.

## 本地生成

要完整构建/测试时, 先在有 **JDK 11 + git** 的环境里生成一次:

```sh
export PATH="$(bash source/scripts/build-fastddsgen.sh):$PATH"   # 构建 fastddsgen (版本取自 fastdds.repos)
bash source/scripts/gen-types.sh                                  # 就地生成到 tests/*/src/types/
```
