# 补丁

供 `scripts/apply-patch.bash` 在编译前按需施加, 只服务于「用 clang 编译测试用例」这一场景.

## `fastcdr-in-class-explicit-spec.patch`

只改 Fast-CDR 公共头 `config.h.in` 一处: 让 `TEMPLATE_SPEC` 宏在 clang 下也展开为空.

- **为何需要**: 原始定义在 clang 下会展开成「类内 `template<>` 显式特化」, 而 `clang-6`
  会拒绝, 报 `error: explicit specialization ... in class scope`.
- **对 `g++` 是 no-op**: 对用 `g++` 编译库本身而言, 补丁前后展开结果完全相同 -- 即便打了补丁,
  也绝不会改变 `g++` 构建出的库.

## 何时施加

补丁针对 `clang-6` 写死: `apply-patch.bash` 仅在 `test-cxx` 为 `clang-6` 时打补丁, 由
`source/config.ini` 的配置判断, 不做现场探测.

> 补充:
> 在 **arm64** 上, `clang-6` 会**错误编译** Fast DDS 生成的类型支持代码, 运行期抛
> `std::bad_function_call` 崩溃.
