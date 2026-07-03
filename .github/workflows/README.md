# CI

`build.yaml` 经 GitHub Actions 为 **x64** 与 **arm64** 各构建一次, 跑通 DDS 与 RPC 测试后,
把安装目录打包成 `install-<arch>.tar.gz` 发布为 release.

## 作业

1. **meta**: `checkout` (含子模块), `git describe` 从 submodule 动态取出 Fast DDS 版本号,
   生成 release tag `<版本号>+<北京时间 yyyymmddHHMM>`.
2. **build** (x64 / arm64 各一, 分别跑在 `ubuntu-latest` 与 `ubuntu-24.04-arm` runner 上):
   1. `checkout` (含子模块).
   2. 依 `source/config.ini` 的 `Ubuntu-<arch>` 起 `ubuntu:<VER>` 容器, 挂载 `source/`, 运行
      `scripts/ci-build.sh`: 装工具链 → 按需打补丁 → 用 g++ 编译三件套到 `install-<arch>/`
      → 校验 libstdc++ 一致性 → 用 clang 编译并运行 DDS / RPC 测试.
   3. 打包 `install-<arch>.tar.gz` 并上传为 artifact.
3. **release** (仅 `push`): 下载两份 artifact, 以上面的 tag 建 release, 附件为两个 `.tar.gz`.

## 触发条件

- `push` 事件仅在 `source/**` 有改动时才跑 (见 workflow 的 `on.push.paths`), 纯文档改动不构建.
- 也可在 Actions 页面用 `workflow_dispatch` 手动触发.
