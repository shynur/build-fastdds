#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>

#include <urpc2.hpp>
#include <nlohmann/json.hpp>

/**
 * @file
 * @brief 在 `urpc2::Urpc2` 之上的类型化便捷封装.
 *
 * `urpc2::Urpc2` 把请求体和响应体当作不透明字符串; 处理器和调用方都要自己
 * 完成 string 到 string 的搬运.  `urpc2_rbk` 消除这一步样板代码: 处理器写成
 * 普通函数, 调用方按常规值传参和取返回值, 中间的 JSON array 打包和解包由本
 * 封装用 nlohmann/json 完成.
 *
 * 两个 public API:
 * - serve():  注册一个普通函数作为具名处理器.  它按需创建/复用底层的
 *   `urpc2::Urpc2` 实例.
 * - call():   以类型化参数发起 RPC, 并把 JSON 响应反序列化成想要的返回类型.
 */
namespace urpc2_rbk {

namespace detail {

/*
 * serve() 和 call() 中不依赖用户类型的部分下沉到 urpc2_rbk.cpp, 这样
 * `urpc2::Urpc2` 实例注册表和默认 timeout 等实现细节不会进入 public header.
 */

/**
 * @brief 在名为 @p instance_name 的实例上注册/替换 string 到 string 处理器.
 *
 * 底层 `urpc2::Urpc2` 实例按需创建并缓存, 同名实例复用同一对象.
 */
void serve_handler(
    const std::string& instance_name,
    const std::string& handler_name,
    ::urpc2::Urpc2::Handler handler
);

/**
 * @brief 向 @p instance_name 的 @p handler_name 发起一次 RPC.
 *
 * @param args_json 已序列化为 JSON array 文本的实参.
 * @return 处理器返回值的 JSON 文本 (procedure 为 `"null"`).
 *
 * 使用一个内部默认 timeout; 复用任一已 serve 的实例作为调用载体, 纯调用方
 * 进程则懒创建一个专用 client 实例.
 */
auto call_raw(
    const std::string& instance_name,
    const std::string& handler_name,
    const std::string& args_json
) -> std::string;

/*
 * 把 JSON array 中的元素按位置解包成 @p A... 各参数, 调用 @p fn, 再把结果
 * 序列化回 JSON 文本.  procedure (返回 void) 序列化为 JSON `null`.
 */
template <typename R, typename... A, std::size_t... I>
auto invoke_from_json(
    const std::function<R(A...)>& fn,
    const ::nlohmann::json& args,
    std::index_sequence<I...>
) -> std::string {
    static_cast<void>(args);  // 零参处理器不会索引 args.
    if constexpr (std::is_void_v<R>) {
        fn(args.at(I).template get<std::decay_t<A>>()...);
        return ::nlohmann::json(nullptr).dump();
    } else {
        return ::nlohmann::json(
            fn(args.at(I).template get<std::decay_t<A>>()...)
        ).dump();
    }
}

}  // namespace detail

/**
 * @brief 注册一个普通函数作为具名 RPC 处理器.
 *
 * @p raw_handler 的参数类型和返回类型即为 RPC 的线上契约.  client 传来的
 * JSON array 会按位置解包成各参数; 返回值 (procedure 为 `null`) 会序列化成
 * JSON 回给 client.  因此处理器可以直接返回 `int`, `std::string`, `std::tuple`
 * 等任何 nlohmann/json 能序列化的类型.
 *
 * 传参惯用 `std::function{lambda}`, 让 CTAD 推导出 @p R 和 @p A....
 *
 * @throws std::invalid_argument 如果 @p raw_handler 不可调用.
 */
template <typename R, typename... A>
void serve(
    const std::string& instance_name,
    const std::string& handler_name,
    std::function<R(A...)> raw_handler
) {
    detail::serve_handler(
        instance_name,
        handler_name,
        [fn = std::move(raw_handler)](std::string args_json) -> std::string {
            const auto args = ::nlohmann::json::parse(std::move(args_json));
            return detail::invoke_from_json(fn, args, std::index_sequence_for<A...>{});
        });
}

/**
 * @brief 以类型化参数调用具名处理器, 并反序列化其返回值.
 *
 * @tparam Ret  期望的返回类型.  若为 `std::tuple<...>`, 则用于返回值是多个值
 *              的情形, 支持解包 (`auto [x, y] = call<std::tuple<...>>(...)`).
 *              若为 `void`, 则忽略返回值.
 * @tparam Args 各实参类型.  常显式给出以固定线上契约.
 *
 * 实参按顺序打包成 JSON array 发送; 响应 JSON 反序列化为 @p Ret.
 */
template <typename Ret, typename... Args>
auto call(
    const std::string& instance_name,
    const std::string& handler_name,
    Args... args
) -> Ret {
    auto args_json = ::nlohmann::json::array();
    (args_json.push_back(::nlohmann::json(std::move(args))), ...);

    auto response = detail::call_raw(instance_name, handler_name, args_json.dump());

    if constexpr (std::is_void_v<Ret>) {
        static_cast<void>(response);
    } else {
        return ::nlohmann::json::parse(std::move(response)).template get<Ret>();
    }
}

}  // namespace urpc2_rbk
