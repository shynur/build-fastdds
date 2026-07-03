# build-fastdds

用 **g++** 编译 [Fast DDS](https://github.com/eProsima/Fast-DDS) 及其依赖
(foonathan_memory / Fast-CDR), 再用 **clang** 编译并运行测试用例. 库与测试共享同一份
`libstdc++`, 因此可正常互链, 运行.

> Fast DDS 的具体版本由 `source/Fast-DDS/` 这个 git submodule 固定 (升级时改 submodule 即可),
> 文档与 CI 都不写死版本号, 而是运行时从 submodule 动态获取.

所有构建相关内容都放在 `source/` 下; **只有 `source/` 有改动时才会触发 CI** (纯文档改动不构建).
每次触发都会经 GitHub Actions 为 **x64** 与 **arm64** 各构建一次, 跑通 DDS 与 RPC 测试后,
把安装目录打包成 `install-{x64,arm64}.tar.gz` 发布为 release.

## 配置

构建类型, 各架构的容器镜像与编译器等, 集中在 `source/config.ini` 里配置,
各键的含义与语法说明见该文件内的注释.

## g++ / clang 分工与 libstdc++ 一致性

- **库 (foonathan_memory / Fast-CDR / Fast-DDS) 一律用 `cxx` (g++) 编译**; 测试用例用
  `test-cxx` (clang) 编译. 二者链接同一份 `libstdc++.so.6`, 遵循同一套 Itanium C++ ABI,
  故可混合链接, 正常运行.
- **一致性如何保证**: 容器里「只装一个」g++ 版本 (即 `cxx`). clang 默认会选系统里版本号
  最高的 GCC 工具链; 既然只有一个, 就没有歧义, clang 自然选中它的 `libstdc++`.
  `run-tests.sh` 还会**机器校验**两点: 1) clang 编译期 `Selected GCC installation` 的版本
  号 == g++ 的版本号; 2) 生成的可执行文件运行期 `ldd` 到 `libstdc++.so.6` (而非 `libc++`).

## 补丁

`patches/fastcdr-in-class-explicit-spec.patch` 只改 Fast-CDR 公共头 `config.h.in` 一处:
让 `TEMPLATE_SPEC` 宏在 clang 下也展开为空. 原始定义在 clang 下会展开成「类内 `template<>`
显式特化」, 而:

| 测试编译器 | 类内显式特化 | 是否需要补丁 |
| ---------- | ------------ | ------------ |
| `clang-6`  | 拒绝 (`error: explicit specialization ... in class scope`) | **需要** |
| `clang-12` | 接受         | **不需要**   |

因此 `apply-patch.sh` **不写死版本**, 而是现场编一段最小代码探测: 测试编译器编不过才打补丁
(满足「如无必要勿用补丁」). 注意: 对用 g++ 编译库本身而言, 补丁前后展开结果完全相同, 是
**no-op** -- 即便打了补丁也绝不会改变 g++ 构建出的库.

> 补充: 在 **arm64** 上, `clang-6` 会**错误编译** Fast DDS 生成的类型支持代码, 运行期抛
> `std::bad_function_call` 崩溃; `clang-7` 及以上正常. 这就是 arm64 选用 `clang-12` 的原因.

## CI 构建流程

`.github/workflows/build.yml`:

1. **meta**: `checkout` (含子模块), `git describe` 从 submodule 动态取出 Fast DDS 版本号,
   生成 release tag `<版本号>+<北京时间 yyyymmddHHMM>` (形如 `vX.Y.Z+202607021830`).
2. **build** (x64 / arm64 各一, 分别跑在 `ubuntu-latest` 与 `ubuntu-24.04-arm` runner 上):
   1. `checkout` (含子模块).
   2. 依 `source/config.ini` 的 `Ubuntu-<arch>` 起 `ubuntu:<VER>` 容器, 挂载 `source/`, 运行
      `scripts/ci-build.sh`: 装工具链 → 按需打补丁 → 用 g++ 编译三件套到 `install-<arch>/`
      → 校验 libstdc++ 一致性 → 用 clang 编译并运行 DDS / RPC 测试.
   3. 打包 `install-<arch>.tar.gz` 并上传为 artifact.
3. **release** (仅 `push`): 下载两份 artifact, 以上面的 tag 建 release, 附件为两个 `.tar.gz`.

> 触发条件: `push` 事件仅在 `source/**` 有改动时才跑 (见 workflow 的 `on.push.paths`);
> 也可在 Actions 页面用 `workflow_dispatch` 手动触发.

## 本地复现

```bash
git clone --recursive https://github.com/<you>/build-fastdds
cd build-fastdds
# 在配置指定的容器里跑完整流程 (x64 为例); 注意挂载的是 source/:
docker run --rm -v "$PWD/source:/repo" -w /repo ubuntu:18.04 bash scripts/ci-build.sh
```

产物在 `source/install-x64/` (或 `source/install-arm64/`), 测试日志会打印在终端.
