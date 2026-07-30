#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <urpc2.hpp>
#include <nlohmann/json.hpp>

/**
 * @file
 * @brief 在 `urpc2::Urpc2` 之上的类型化便捷封装.
 *
 * `urpc2::Urpc2` 把请求体和响应体当作不透明字符串; 处理器和调用方都要自己
 * 完成 string 到 string 的搬运.  `urpc2_rbk` 消除这一步样板代码: 处理器写成
 * 普通函数, 调用方按常规值传参和取返回值, 中间的序列化由本封装用 nlohmann/json
 * 完成.
 *
 * 两个 public API:
 * - serve():  注册一个普通函数作为具名处理器.  它按需创建/复用底层的
 *   `urpc2::Urpc2` 实例.
 * - call():   以类型化参数发起 RPC, 并把返回值反序列化成想要的类型.
 *
 * 在线 (wire) 格式: CBOR.  其中 `std::string` 和 `std::string_view` 类型
 * 走 CBOR 的 bytes token (nlohmann::json::binary, major type 2), 不做 UTF-8
 * 校验; 其他类型走对应的 text / number / array 路径.
 * 注: `const char*` 字面量与字符数组不享受二进制透传, 需要传二进制请先
 * 装入 `std::string`.
 */
namespace urpc2_rbk {

namespace detail {

/*
 * serve() 和 call() 中不依赖用户类型的部分下沉到 urpc2_rbk.cpp, 这样
 * `urpc2::Urpc2` 实例注册表和默认 timeout 等实现细节不会进入 public header.
 */

inline auto cbor_decode(const std::string& bytes) -> ::nlohmann::json {
    return ::nlohmann::json::from_cbor(
        reinterpret_cast<const std::uint8_t*>(bytes.data()),
        reinterpret_cast<const std::uint8_t*>(bytes.data()) + bytes.size()
    );
}

inline auto cbor_encode(const ::nlohmann::json& value) -> std::string {
    const auto bytes = ::nlohmann::json::to_cbor(value);
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

template <typename T>
auto encode_arg(T&& arg) -> ::nlohmann::json {
    using U = std::decay_t<T>;
    if constexpr (std::is_same_v<U, std::string>) {
        std::vector<std::uint8_t> bytes(arg.size());
        if (!arg.empty()) {
            std::memcpy(bytes.data(), arg.data(), arg.size());
        }
        return ::nlohmann::json::binary(std::move(bytes));
    } else if constexpr (std::is_same_v<U, std::string_view>) {
        std::vector<std::uint8_t> bytes(arg.size());
        if (!arg.empty()) {
            std::memcpy(bytes.data(), arg.data(), arg.size());
        }
        return ::nlohmann::json::binary(std::move(bytes));
    } else {
        return ::nlohmann::json(std::forward<T>(arg));
    }
}

template <typename T>
auto decode_arg(const ::nlohmann::json& j) -> T {
    if constexpr (std::is_same_v<T, std::string>) {
        if (j.is_binary()) {
            const auto& bin = j.get_ref<const ::nlohmann::json::binary_t&>();
            std::string out(bin.size(), '\0');
            if (!bin.empty()) {
                std::memcpy(out.data(), bin.data(), bin.size());
            }
            return out;
        }
        return j.template get<T>();
    } else if constexpr (std::is_same_v<T, std::string_view>) {
        if (j.is_binary()) {
            const auto& bin = j.get_ref<const ::nlohmann::json::binary_t&>();
            return std::string_view{reinterpret_cast<const char*>(bin.data()), bin.size()};
        }
        const auto& s = j.get_ref<const std::string&>();
        return std::string_view{s.data(), s.size()};
    } else {
        return j.template get<T>();
    }
}

template <typename R>
auto encode_return(R&& result) -> ::nlohmann::json {
    using U = std::decay_t<R>;
    if constexpr (std::is_same_v<U, std::string>) {
        std::vector<std::uint8_t> bytes(result.size());
        if (!result.empty()) {
            std::memcpy(bytes.data(), result.data(), result.size());
        }
        return ::nlohmann::json::binary(std::move(bytes));
    } else if constexpr (std::is_same_v<U, std::string_view>) {
        std::vector<std::uint8_t> bytes(result.size());
        if (!result.empty()) {
            std::memcpy(bytes.data(), result.data(), result.size());
        }
        return ::nlohmann::json::binary(std::move(bytes));
    } else {
        return ::nlohmann::json(std::forward<R>(result));
    }
}

template <typename Ret>
auto decode_return(const ::nlohmann::json& j) -> Ret {
    if constexpr (std::is_same_v<Ret, std::string>) {
        if (j.is_binary()) {
            const auto& bin = j.get_ref<const ::nlohmann::json::binary_t&>();
            std::string out(bin.size(), '\0');
            if (!bin.empty()) {
                std::memcpy(out.data(), bin.data(), bin.size());
            }
            return out;
        }
        return j.template get<Ret>();
    } else if constexpr (std::is_same_v<Ret, std::string_view>) {
        if (j.is_binary()) {
            const auto& bin = j.get_ref<const ::nlohmann::json::binary_t&>();
            return std::string_view{reinterpret_cast<const char*>(bin.data()), bin.size()};
        }
        const auto& s = j.get_ref<const std::string&>();
        return std::string_view{s.data(), s.size()};
    } else {
        return j.template get<Ret>();
    }
}

/*
 * 把 CBOR array 中的元素按位置解包成 @p A... 各参数, 调用 @p fn, 再把结果
 * 序列化回 CBOR 字节.  procedure (返回 void) 序列化为 CBOR `null`.
 */
template <typename R, typename... A, std::size_t... I>
auto invoke_from_cbor(
    const std::function<R(A...)>& fn,
    const ::nlohmann::json& args,
    std::index_sequence<I...>
) -> std::string {
    (void)args;  // 零参处理器不会索引 args.
    if constexpr (std::is_void_v<R>) {
        fn(decode_arg<std::decay_t<A>>(args.at(I))...);
        return cbor_encode(::nlohmann::json(nullptr));
    } else {
        return cbor_encode(encode_return(
            fn(decode_arg<std::decay_t<A>>(args.at(I))...)));
    }
}

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
 * @param args_cbor 已按 CBOR 编码的实参数组 (按 `nlohmann::json::to_cbor` 产出).
 * @return 处理器返回值的 CBOR 编码 (procedure 为 JSON `null` 的 CBOR).
 *
 * 使用一个内部默认 timeout; 复用任一已 serve 的实例作为调用载体, 纯调用方
 * 进程则懒创建一个专用 client 实例.
 */
auto call_raw(
    const std::string& instance_name,
    const std::string& handler_name,
    const std::string& args_cbor
) -> std::string;

}  // namespace detail

/**
 * @brief 注册一个普通函数作为具名 RPC 处理器.
 *
 * @p raw_handler 的参数类型和返回类型即为 RPC 的线上契约.  client 传来的
 * CBOR array 会按位置解包成各参数; 返回值 (procedure 为 `null`) 会序列化成
 * CBOR 回给 client.  因此处理器可以直接返回 `int`, `std::string`, `std::tuple`
 * 等任何 nlohmann/json 能序列化的类型.  `std::string` 与 `std::string_view`
 * 参数允许携带任意字节 (含嵌入 NUL 与非法 UTF-8 序列), 不再要求 UTF-8;
 * 其他类型仍按 nlohmann/json 默认序列化校验.
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
            const auto args = detail::cbor_decode(args_cbor);
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
 * 实参按顺序打包成 CBOR array 发送; 响应 CBOR 反序列化为 @p Ret.  参数与返回
 * 允许任意字节 (含嵌入 NUL), 例如图像, protobuf 等不透明载荷.
 */
template <typename Ret, typename... Args>
auto call(
    const std::string& instance_name,
    const std::string& handler_name,
    Args... args
) -> Ret {
    auto args_cbor_value = ::nlohmann::json::array();
    auto push_one = [&args_cbor_value](auto&& arg) {
        args_cbor_value.push_back(detail::encode_arg(std::forward<decltype(arg)>(arg)));
    };
    (push_one(std::move(args)), ...);
    const auto args_cbor = detail::cbor_encode(args_cbor_value);

    auto response = detail::call_raw(instance_name, handler_name, args_cbor);

    if constexpr (std::is_void_v<Ret>) {
        (void)response;
    } else {
        return detail::decode_return<Ret>(detail::cbor_decode(std::move(response)));
    }
}

}  // namespace urpc2_rbk
