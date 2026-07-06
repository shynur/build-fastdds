#ifndef URPC2_HPP
#define URPC2_HPP

#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace urpc2 {

/**
 * @brief 应用程序直接使用的最小 RPC-over-DDS 端点。
 *
 * 每个 Urpc2 对象拥有一个 Fast DDS RPC 服务。构造函数名称会用作 DDS RPC
 * 服务名，因此它在 DDS 域内必须唯一。处理器名称的作用域是拥有它的 Urpc2
 * 实例：两个不同实例都可以注册名为 "add" 的处理器，调用方通过向 call()
 * 传入接收方实例名称来消除歧义。
 *
 * 框架将请求体和响应体视为不透明字符串。第一版预期约定是 JSON 文本，但
 * Urpc2 不会解析或校验该载荷。
 */
class Urpc2 {
  public:
    /**
     * @brief 已注册 RPC 处理器的函数签名。
     *
     * @param args 调用方提供的不透明请求载荷。
     * @return 返回给调用方的不透明响应载荷。
     *
     * @note 处理器异常会通过生成的 Fast DDS RPC 层作为远程异常传播。
     */
    using Handler = std::function<std::string(std::string)>;

    /**
     * @brief 使用唯一实例名称创建 RPC 端点。
     *
     * @param name 此端点在 DDS 域内的唯一名称。
     *
     * @throws std::invalid_argument 如果 @p name 为空。
     * @throws std::runtime_error 如果 Fast DDS participant 或服务器创建失败。
     */
    explicit Urpc2(std::string name);

    /**
     * @brief 停止所拥有的 RPC 服务器并释放 Fast DDS 资源。
     */
    ~Urpc2();

    Urpc2(const Urpc2&) = delete;
    Urpc2& operator=(const Urpc2&) = delete;
    Urpc2(Urpc2&&) = delete;
    Urpc2& operator=(Urpc2&&) = delete;

    /**
     * @brief 返回此端点的唯一实例名称。
     */
    const std::string& name() const noexcept;

    /**
     * @brief 在此端点上注册或替换具名处理器。
     *
     * @param handler_name 调用方用于选择处理器的名称。
     * @param handler 接收请求字符串并返回响应字符串的可调用对象。
     *
     * @throws std::invalid_argument 如果 @p handler_name 为空，或 @p handler
     *         不可调用。
     */
    void register_handler(std::string handler_name, Handler handler);

    /**
     * @brief 调用另一个 Urpc2 端点上的处理器。
     *
     * 此重载使用 default_call_timeout()。
     *
     * @param receiver_name 接收端点的唯一名称。
     * @param handler_name 注册在接收端点上的处理器名称。
     * @param args 不透明请求载荷。
     * @return 接收方处理器返回的不透明响应载荷。
     *
     * @throws std::invalid_argument 如果 @p receiver_name 或 @p handler_name
     *         为空。
     * @throws std::runtime_error 如果调用超时或客户端创建失败。
     * @throws eprosima::fastdds::dds::rpc::RpcException 表示远程 Fast DDS RPC
     *         错误。
     */
    std::string call(
            const std::string& receiver_name,
            const std::string& handler_name,
            const std::string& args);

    /**
     * @brief 使用自定义超时时间调用另一个 Urpc2 端点上的处理器。
     *
     * @param receiver_name 接收端点的唯一名称。
     * @param handler_name 注册在接收端点上的处理器名称。
     * @param args 不透明请求载荷。
     * @param timeout 请求发送后等待回复的最长时间。
     * @return 接收方处理器返回的不透明响应载荷。
     *
     * @throws std::invalid_argument 如果 @p receiver_name 或 @p handler_name
     *         为空。
     * @throws std::runtime_error 如果调用超时或客户端创建失败。
     * @throws eprosima::fastdds::dds::rpc::RpcException 表示远程 Fast DDS RPC
     *         错误。
     */
    std::string call(
            const std::string& receiver_name,
            const std::string& handler_name,
            const std::string& args,
            std::chrono::milliseconds timeout);

    /**
     * @brief 未显式指定超时时间的 call() 所使用的默认回复等待时间。
     */
    static constexpr std::chrono::milliseconds default_call_timeout() noexcept
    {
        return std::chrono::milliseconds{5000};
    }

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace urpc2

#endif  // URPC2_HPP
