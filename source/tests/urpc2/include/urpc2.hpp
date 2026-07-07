#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>

using namespace std::literals;

namespace urpc2 {
    class Urpc2;
}

/**
 * @brief 应用程序直接使用的最小 RPC-over-DDS 端点.
 *
 * 每个 Urpc2 instance 拥有一个 Fast DDS RPC service.  构造函数名称会用作 DDS RPC
 * 服务名, 因此它在 DDS domain 内必须 unique.  处理器名称的作用域是拥有它的 Urpc2
 * 实例: 两个不同实例都可以注册名为 e.g. "add" 的 handler, 调用方通过向 call()
 * 传入接收方实例名称来消除歧义.
 *
 * 框架将请求体和响应体视为不透明字符串.  通常约定是 JSON 文本, 但 Urpc2
 * 不会解析或校验该 payload.
 */
class urpc2::Urpc2 {
    class Impl;
    std::unique_ptr<Impl> impl_;
  public:
    using Handler = std::function<std::string(std::string)>;

    /**
     * @param name 此端点在 DDS domain 内的 unique name.
     *
     * @throws std::runtime_error 如果 Fast DDS participant or server 创建失败.
     */
    explicit Urpc2(std::string name);
    ~Urpc2();

    auto name() const noexcept -> const std::string&;

    /**
     * @brief 在此端点上注册或替换具名处理器.
     *
     * @throws std::invalid_argument 如果 @p handler 不可调用.
     */
    void register_handler(std::string handler_name, Handler handler);

    /**
     * @brief 调用指定 name 的 Urpc2 instance 上的 handler.
     *
     * @throws std::runtime_error 如果 timeout 或 client 创建失败.
     * @throws eprosima::fastdds::dds::rpc::RpcException Fast DDS RPC error
     */
    auto call(
        const std::string& receiver_name,
        const std::string& handler_name,
        const std::string& args,
        std::chrono::milliseconds timeout  // TODO: 改成能接受 1s 1ms 1min 这类值的
    ) -> std::string;
};
