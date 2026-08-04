#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std::literals;

namespace urpc2 {
    class Urpc2;

    /**
     * @brief urpc2 所有异常的根.
     *
     * 以 std::exception (经 std::runtime_error) 为根, 因此调用方一个
     * `catch (const std::exception&)` 即可兜住 Urpc2::call() 的全部失败; 想区分
     * 失败种类再 catch 下面的具体子类.
     *
     * 刻意「不」向外暴露底层 Fast DDS 的 eprosima::fastdds::dds::rpc::RpcException:
     * 它并不继承 std::exception, 一旦逃逸到只 catch std::exception 的调用方就会
     * std::terminate.  Urpc2::call() 在边界处把这些 DDS 异常翻译成下列类型.
     */
    struct Error : std::runtime_error {
        using std::runtime_error::runtime_error;
    };

    /**
     * @brief 未发现目标 instance 的 server.
     *
     * 在发现窗口内没有任何名为 receiver 的 Urpc2 server 与调用方 requester 匹配.
     * 通常意味着目标名写错、对端尚未启动, 或网络不通.
     */
    struct ServerNotFound : Error {
        using Error::Error;
    };

    /**
     * @brief 已匹配并发出请求, 但在 timeout 内未收到回复.
     *
     * 例如对端匹配成功后处理过慢、卡死, 或请求发出后中途失联.
     */
    struct Timeout : Error {
        using Error::Error;
    };

    /**
     * @brief 目标 server 收到了请求, 但以错误应答.
     *
     * 携带远端给出的说明 (见 what()).  「无此 handler」这一常见情形另见其子类
     * UnknownOperation.
     */
    struct RemoteError : Error {
        using Error::Error;
    };

    /**
     * @brief 目标 instance 上不存在被调用的 handler.
     */
    struct UnknownOperation : RemoteError {
        using RemoteError::RemoteError;
    };

    /**
     * @brief 本地端点创建失败 (Fast DDS participant / client 建不起来).
     *
     * 属于本地资源/配置问题, 与对端是否存在无关.
     */
    struct LocalError : Error {
        using Error::Error;
    };
}

/**
 * @brief 应用程序直接使用的最小 RPC-over-DDS 端点.
 *
 * 每个 Urpc2 instance 拥有一个 Fast DDS RPC service.  构造函数名称会用作 DDS RPC
 * 服务名.  该名称不要求 unique: 多个实例可以共享同一个 name, 框架不会拒绝.  处理器
 * 名称的作用域是拥有它的 Urpc2 实例: 两个不同实例都可以注册名为 e.g. "add" 的
 * handler, 调用方通过向 call() 传入接收方实例名称来选择目标.
 *
 * 当多个实例共享同一 name 时, 一次 call() 会被投递给所有同名端点, 每个都可能会应答,
 * 而调用方以最先到达的那条响应 resolve, 其余响应被丢弃.
 * 注意: 由于同名实例各自维护独立的处理器表, 它们注册的 handler 集合可能不同.
 *
 * 框架将请求体和响应体视为不透明二进制数据 (std::vector<std::uint8_t>).  上层封装
 * (如 urpc2_rbk) 使用 CBOR 序列化, 但 Urpc2 本身不解析或校验 payload.
 */
class urpc2::Urpc2 {
    class Impl;
    std::unique_ptr<Impl> impl_;
  public:
    using Handler = std::function<std::vector<std::uint8_t>(std::vector<std::uint8_t>)>;

    /**
     * @param name (DDS domain 内的 service name) 当成目标地址的 name 理解即可
     *
     * @throws std::runtime_error 如果 Fast DDS participant or server 创建失败.
     */
    explicit Urpc2(const std::string& name);
    ~Urpc2();

    auto name() const noexcept -> const std::string&;

    /**
     * @brief 在此端点上注册或替换具名处理器.
     *
     * @throws std::invalid_argument 如果 @p handler 不可调用.
     */
    void register_handler(const std::string& handler_name, Handler handler);

    /**
     * @brief 调用具有指定 name 的 Urpc2 instance 上的 handler.
     *
     * 可由多个线程调用.  指向不同 receiver 的调用可以并发执行; 指向同一 receiver
     * 的调用会在本地串行化, 等待前一个调用完成后才发送.  @p timeout 从请求开始发送
     * 后计算, 不包含等待同一 receiver 的前序调用所花的时间.
     *
     * 所有失败都以 urpc2::Error (其根为 std::exception) 的子类抛出; 底层 Fast DDS
     * 的 RpcException 已在实现内被翻译, 不会外泄:
     *
     * @throws urpc2::ServerNotFound   未发现目标 instance 的 server.
     * @throws urpc2::Timeout          已发出请求但超时未收到回复.
     * @throws urpc2::UnknownOperation 目标 instance 上无此 handler.
     * @throws urpc2::RemoteError      目标 server 以其它错误应答.
     * @throws urpc2::LocalError       本地 participant/client 创建失败.
     */
    auto call(
        const std::string& receiver_name,
        const std::string& handler_name,
        const std::vector<std::uint8_t>& args,
        std::chrono::duration<double> timeout
    ) -> std::vector<std::uint8_t>;
};
