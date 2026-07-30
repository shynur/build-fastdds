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
 * 普通函数, 调用方按常规值传参和取返回值, 中间的 CBOR array 打包和解包由本
 * 封装用 nlohmann/json 完成.
 *
 * 两个 public API:
 * - serve():  注册一个普通函数作为具名处理器.  它按需创建/复用底层的
 *   `urpc2::Urpc2` 实例.
 * - call():   以类型化参数发起 RPC, 并把 CBOR 响应反序列化成想要的返回类型.
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
 * @param args_cbor 已序列化为 CBOR array 的实参（二进制数据）.
 * @return 处理器返回值的 CBOR 二进制数据 (procedure 为 CBOR `null`).
 *
 * 使用一个内部默认 timeout; 复用任一已 serve 的实例作为调用载体, 纯调用方
 * 进程则懒创建一个专用 client 实例.
 */
auto call_raw(
    const std::string& instance_name,
    const std::string& handler_name,
    const std::string& args_cbor
) -> std::string;

/*
 * 把 CBOR array 中的元素按位置解包成 @p A... 各参数, 调用 @p fn, 再把结果
 * 序列化回 CBOR 二进制.  procedure (返回 void) 序列化为 CBOR `null`.
 */
template <typename R, typename... A, std::size_t... I>
auto invoke_from_cbor(
    const std::function<R(A...)>& fn,
    const ::nlohmann::json& args,
    std::index_sequence<I...>
) -> std::string {
    (void)args;  // 零参处理器不会索引 args.
    if constexpr (std::is_void_v<R>) {
        fn(args.at(I).template get<std::decay_t<A>>()...);
        const auto result = ::nlohmann::json(nullptr);
        return std::string(
            ::nlohmann::json::to_cbor(result).begin(),
            ::nlohmann::json::to_cbor(result).end()
        );
    } else {
        const auto result = ::nlohmann::json(
            fn(args.at(I).template get<std::decay_t<A>>()...)
        );
        const auto cbor = ::nlohmann::json::to_cbor(result);
        return std::string(cbor.begin(), cbor.end());
    }
}

}  // namespace detail

/**
 * @brief 注册一个普通函数作为具名 RPC 处理器.
 *
 * @p raw_handler 的参数类型和返回类型即为 RPC 的线上契约.  client 传来的
 * CBOR array 会按位置解包成各参数; 返回值 (procedure 为 `null`) 会序列化成
 * CBOR 回给 client.  因此处理器可以直接返回 `int`, `std::string`, `std::tuple`
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
        [fn = std::move(raw_handler)](std::string args_cbor) -> std::string {
            const auto args = ::nlohmann::json::from_cbor(
                std::vector<std::uint8_t>(args_cbor.begin(), args_cbor.end()),
                true,  // strict
                true,  // allow_exceptions
                ::nlohmann::json::cbor_tag_handler_t::ignore
            );
            return detail::invoke_from_cbor(fn, args, std::index_sequence_for<A...>{});
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
 * 实参按顺序打包成 CBOR array 发送; 响应 CBOR 反序列化为 @p Ret.
 */
template <typename Ret, typename... Args>
auto call(
    const std::string& instance_name,
    const std::string& handler_name,
    Args... args
) -> Ret {
    auto args_json = ::nlohmann::json::array();
    (args_json.push_back(::nlohmann::json(std::move(args))), ...);

    const auto args_cbor = ::nlohmann::json::to_cbor(args_json);
    const auto args_cbor_str = std::string(args_cbor.begin(), args_cbor.end());

    auto response = detail::call_raw(instance_name, handler_name, args_cbor_str);

    if constexpr (std::is_void_v<Ret>) {
        (void)response;
    } else {
        const auto response_cbor = std::vector<std::uint8_t>(response.begin(), response.end());
        return ::nlohmann::json::from_cbor(response_cbor).template get<Ret>();
    }
}

}  // namespace urpc2_rbk
