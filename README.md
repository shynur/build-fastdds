# build-fastdds

用 `g++` 编译 [Fast DDS](https://github.com/eProsima/Fast-DDS) 及其依赖
(`foonathan_memory` / `Fast-CDR`), 再用 **clang** 编译并运行测试用例.
库与测试共享同一份 `libstdc++`, 因此可正常互链, 运行.

> Fast DDS 的具体版本由 `source/Fast-DDS/` 这个 git submodule 固定 (升级时改 submodule 即可).
> 依赖 `foonathan_memory` 则**不再**作为 submodule: 其所需版本已由 Fast DDS 自己声明
> (`Fast-DDS/fastdds.repos` 里的 `foonathan_memory_vendor` → 该 vendor 的 `externalproject` 的
> `GIT_TAG`), 故 `scripts/fetch-foonathan.bash` 于 CI 期沿此链解析并动态 clone 对应源码.
> 这样升级 Fast DDS submodule 时, `foonathan_memory` 版本自动跟随, 无需在别处同步.

## 配置

构建类型, 各架构的容器镜像与编译器等, 集中在 `source/config.ini` 里配置,
各键的含义与语法说明见该文件内的注释.

## `g++` / clang 分工与 `libstdc++` 一致性

两个编译器都按架构在 `source/config.ini` 里配置: `cxx-<arch>` 是编库用的 `g++`,
`test-cxx-<arch>` 是编测试用的 clang.  下文用 `cxx` / `test-cxx` 指代这两个键选出的编译器.

- **库 (`foonathan_memory` / `Fast-CDR` / `Fast-DDS`) 一律用 `cxx` (`g++`) 编译**; 测试用例用
  `test-cxx` (clang) 编译.  二者链接同一份 `libstdc++.so.6`, 遵循同一套 Itanium C++ ABI,
  故可混合链接, 正常运行.
- **一致性如何保证**: 容器里「只装一个」`g++` 版本 (即 `cxx` 选中的那个).  clang 默认会选
  系统里版本号最高的 GCC 工具链; 既然只有一个, 就没有歧义, clang 自然选中它的 `libstdc++`.
  `run-tests.bash` 还会**机器校验**两点:
  1. clang 编译期 `Selected GCC installation` 的版本号 = `g++` 的版本号;
  2. 生成的可执行文件运行期 `ldd` 到 `libstdc++.so.6` (而非 `libc++`).

## 测试

`source/tests/` 下 DDS 与 RPC 两个用例只提交 IDL 与手写源; 类型/RPC 代码由 `fastddsgen`
从 IDL 生成, **不入库**, 于 CI 期间现生成 (见 `.gitignore`).

## 补丁

`clang-6` 会拒绝 Fast-CDR 公共头里一处「类内显式特化」, 故 `apply-patch.bash` 在 `test-cxx`
为 `clang-6` 时打上 `source/patches/` 下的补丁 (对 `g++` 构建库本身是 **no-op**).

## CI

`.github/workflows/build.yaml` 经 GitHub Actions: 先由 `generate` 作业用 `fastddsgen` 生成测试
类型代码 (架构无关, 只一次), 再为 x64 / arm64 各构建一次 (消费该产物), 跑通 DDS 与 RPC
测试后打包 `install-<arch>.tar.gz` 发布为 release.
