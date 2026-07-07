#include "urpc2.hpp"

#include "types/processor.hpp"
#include "types/processorClient.hpp"
#include "types/processorServer.hpp"

#include <future>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/qos/DomainParticipantQos.hpp>
#include <fastdds/dds/domain/qos/ReplierQos.hpp>
#include <fastdds/dds/domain/qos/RequesterQos.hpp>
#include <fastdds/dds/rpc/exceptions.hpp>
#include <fastdds/dds/rpc/interfaces/RpcServer.hpp>

namespace urpc2 {
namespace {

/*
 * 在域 0 中创建一个普通 DomainParticipant.
 *
 * 第一版有意不把 QoS 自定义暴露到公共接口.  所有传输, 发现和 participant
 * 默认值都来自 Fast DDS, 因此该包装层保持小巧且易于检查.
 */
::eprosima::fastdds::dds::DomainParticipant* create_participant()
{
    const auto factory = ::eprosima::fastdds::dds::DomainParticipantFactory::get_shared_instance();
    if (!factory) {
        throw std::runtime_error{"Failed to get Fast DDS participant factory"};
    }

    auto* const participant = factory->create_participant(0, ::eprosima::fastdds::dds::DomainParticipantQos{});
    if (participant == nullptr) {
        throw std::runtime_error{"Failed to create Fast DDS participant"};
    }

    return participant;
}

/*
 * 销毁 participant 以及它仍然拥有的所有实体.
 *
 * 生成的 RPC 客户端和服务器通常会清理自己的 requester, replier 和 service
 * 对象.  这里仍然会在把 participant 交还给工厂前调用
 * delete_contained_entities(), 作为防御性的清理边界.
 */
void delete_participant(::eprosima::fastdds::dds::DomainParticipant* participant) noexcept
{
    if (participant == nullptr) {
        return;
    }

    participant->delete_contained_entities();
    const auto factory = ::eprosima::fastdds::dds::DomainParticipantFactory::get_shared_instance();
    if (factory) {
        factory->delete_participant(participant);
    }
}

/*
 * 单个短生命周期客户端 participant 的 RAII 包装.
 *
 * 当前调用路径会为每次 RPC 调用创建一个临时 participant 和生成的
 * ProcessorClient.  这有意牺牲效率, 但在测试框架仍在验证最简单正确性模型
 * 时, 可以避免共享生成的客户端状态.
 */
class TemporaryParticipant {
  public:
    TemporaryParticipant(): participant_{create_participant()} {}
    ~TemporaryParticipant()
    {
        delete_participant(this->participant_);
    }

    TemporaryParticipant(const TemporaryParticipant&) = delete;
    TemporaryParticipant& operator=(const TemporaryParticipant&) = delete;

    ::eprosima::fastdds::dds::DomainParticipant& get() const
    {
        return *this->participant_;
    }

  private:
    ::eprosima::fastdds::dds::DomainParticipant* participant_ = nullptr;
};

}  // namespace

/*
 * 公共 Urpc2 门面的隐藏实现.
 *
 * 公共头文件保持不依赖 Fast DDS 头文件.  此类拥有生成的 RPC 服务器, 服务器
 * 线程和处理器注册表.  调用会使用生成的 ProcessorClient 执行 IDL 操作:
 *
 *     router(in string handler_name, in string args) -> string
 */
class Urpc2::Impl {
  public:
    /*
     * 构造一个 Urpc2 端点的接收侧.
     *
     * Urpc2 名称会成为生成的 RPC 服务名.  Router 对象将生成服务器回调适配回此
     * Impl, 服务器运行循环放在后台线程上, 因此应用程序既能处理请求, 也能
     * 发起出站调用.
     */
    explicit Impl(std::string name): name_{std::move(name)}
    {
        if (this->name_.empty()) {
            throw std::invalid_argument{"Urpc2 name must not be empty"};
        }

        this->participant_ = create_participant();

        const auto router = std::make_shared<Router>(*this);
        this->server_ = create_ProcessorServer(
            *this->participant_,
            this->name_.c_str(),
            ::eprosima::fastdds::dds::ReplierQos{},
            0,
            router);
        if (!this->server_) {
            throw std::runtime_error{"Failed to create Urpc2 server"};
        }

        this->server_thread_ = std::thread{[this]() { this->server_->run(); }};
        std::this_thread::sleep_for(std::chrono::seconds{1});
    }

    /*
     * 删除 participant 前停止服务器.
     *
     * 生成服务器的析构也会调用 stop(), 但这里显式执行可让关闭顺序更清楚:
     * 停止运行循环, 等待线程结束, 释放生成的服务器对象, 然后删除
     * participant.
     */
    ~Impl()
    {
        if (this->server_) {
            this->server_->stop();
        }
        if (this->server_thread_.joinable()) {
            this->server_thread_.join();
        }

        this->server_.reset();
        delete_participant(this->participant_);
    }

    const std::string& name() const noexcept
    {
        return this->name_;
    }

    /*
     * 将处理器存入本地注册表.
     *
     * 重新注册同名处理器会替换旧的可调用对象.  互斥量只保护注册表变更和
     * 查找; 处理器执行发生在 dispatch() 的临界区之外.
     */
    void register_handler(std::string handler_name, Handler handler)
    {
        if (handler_name.empty()) {
            throw std::invalid_argument{"Handler name must not be empty"};
        }
        if (!handler) {
            throw std::invalid_argument{"Handler must be callable"};
        }

        std::lock_guard<std::mutex> lock{this->handlers_mutex_};
        this->handlers_[std::move(handler_name)] = std::move(handler);
    }

    /*
     * 执行一次同步出站 RPC.
     *
     * 会为接收方服务名创建一个临时 participant/client 对.  Fast DDS 发现是异步
     * 的, 因此当前保守实现会在发送请求前等待.  生成的 future 返回后, 调用方
     * 提供的超时时间只限制请求发送后的回复等待.
     */
    std::string call(
            const std::string& receiver_name,
            const std::string& handler_name,
            const std::string& args,
            const std::chrono::milliseconds timeout)
    {
        if (receiver_name.empty()) {
            throw std::invalid_argument{"Receiver name must not be empty"};
        }
        if (handler_name.empty()) {
            throw std::invalid_argument{"Handler name must not be empty"};
        }

        TemporaryParticipant participant;
        const auto client = create_ProcessorClient(
            participant.get(),
            receiver_name.c_str(),
            ::eprosima::fastdds::dds::RequesterQos{});
        if (!client) {
            throw std::runtime_error{"Failed to create Urpc2 client"};
        }

        /*
         * 给 requester 和 replier 端点留出彼此发现的时间.  这让第一版行为在
         * 示例和多进程测试中保持确定性, 代价是增加每次调用的延迟.
         */
        std::this_thread::sleep_for(std::chrono::seconds{5});

        auto future = client->router(handler_name, args);
        if (future.wait_for(timeout) != std::future_status::ready) {
            throw std::runtime_error{"Urpc2 call timed out"};
        }

        return future.get();
    }

    /*
     * 将传入的生成 RPC 请求路由到已注册的用户处理器.
     *
     * 持有注册表互斥量时复制处理器, 并在释放互斥量后调用.  这样一个处理器就
     * 可以注册或替换其他处理器, 而不会让分发路径死锁.
     */
    std::string dispatch(const std::string& handler_name, const std::string& args)
    {
        Handler handler;
        {
            std::lock_guard<std::mutex> lock{this->handlers_mutex_};
            const auto iter = this->handlers_.find(handler_name);
            if (iter == this->handlers_.end()) {
                const auto message = std::string{"No such Urpc2 handler: "} + handler_name;
                throw ::eprosima::fastdds::dds::rpc::RemoteUnknownOperationError{message.c_str()};
            }
            handler = iter->second;
        }

        return handler(args);
    }

  private:
    /*
     * 从生成的 ProcessorServer_IServerImplementation 到 Impl 的适配器.
     *
     * IDL 只有一个操作 router().  第一个字符串选择用户处理器, 第二个字符串
     * 作为不透明载荷透传.
     */
    class Router final : public ProcessorServer_IServerImplementation {
      public:
        explicit Router(Impl& owner): owner_{owner} {}

        std::string router(
                const ::eprosima::fastdds::dds::rpc::RpcRequest& info,
                const std::string& handler_name,
                const std::string& args) override
        {
            static_cast<void>(info);
            return this->owner_.dispatch(handler_name, args);
        }

      private:
        Impl& owner_;
    };

    std::string name_;
    ::eprosima::fastdds::dds::DomainParticipant* participant_ = nullptr;
    std::shared_ptr<::eprosima::fastdds::dds::rpc::RpcServer> server_;
    std::thread server_thread_;
    std::mutex handlers_mutex_;
    std::map<std::string, Handler> handlers_;
};

Urpc2::Urpc2(std::string name): impl_{std::make_unique<Impl>(std::move(name))} {}

Urpc2::~Urpc2() = default;

const std::string& Urpc2::name() const noexcept
{
    return this->impl_->name();
}

void Urpc2::register_handler(std::string handler_name, Handler handler)
{
    this->impl_->register_handler(std::move(handler_name), std::move(handler));
}

std::string Urpc2::call(
        const std::string& receiver_name,
        const std::string& handler_name,
        const std::string& args)
{
    return this->call(receiver_name, handler_name, args, default_call_timeout());
}

std::string Urpc2::call(
        const std::string& receiver_name,
        const std::string& handler_name,
        const std::string& args,
        const std::chrono::milliseconds timeout)
{
    return this->impl_->call(receiver_name, handler_name, args, timeout);
}

}  // namespace urpc2
