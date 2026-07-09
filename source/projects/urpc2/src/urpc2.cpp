#include "urpc2.hpp"

#include <future>
#include <map>
#include <memory>
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

#include "types/processor.hpp"
#include "types/processorClient.hpp"
#include "types/processorServer.hpp"


static auto create_participant() -> ::eprosima::fastdds::dds::DomainParticipant * {
    const auto factory = ::eprosima::fastdds::dds::DomainParticipantFactory::get_shared_instance();
    if (!factory) {
        throw std::runtime_error{"Failed to get Fast DDS participant factory"};
    }

    auto *const participant = factory->create_participant(0, ::eprosima::fastdds::dds::DomainParticipantQos{});
    if (participant == nullptr) {
        throw std::runtime_error{"Failed to create Fast DDS participant"};
    }
    return participant;
}

/*
 * 销毁 participant 以及它仍然拥有的所有实体.
 *
 * 生成的 RPC 客户端和服务器通常会清理自己的 requester, replier 和 service
 * 对象.  这里仍然会在把 participant 交还给工厂前调用 delete_contained_entities(),
 * 作为防御性的清理边界.
 */
static void delete_participant(::eprosima::fastdds::dds::DomainParticipant *const participant) noexcept {
    if (participant == nullptr) {
        return;
    }

    participant->delete_contained_entities();
    const auto factory = ::eprosima::fastdds::dds::DomainParticipantFactory::get_shared_instance();
    if (factory) {
        factory->delete_participant(participant);
    }
}

namespace {
    struct ParticipantDeleter {
        void operator()(::eprosima::fastdds::dds::DomainParticipant *const p) const {
            delete_participant(p);
        }
    };
    using Participant = std::unique_ptr<::eprosima::fastdds::dds::DomainParticipant, ParticipantDeleter>;
}

class urpc2::Urpc2::Impl {
    class Router final: public gen::ProcessorServer_IServerImplementation {
        Impl& owner_;
      public:
        explicit Router(Impl& owner): owner_{owner} {}
        std::string router(
            const ::eprosima::fastdds::dds::rpc::RpcRequest&,
            const std::string& handler_name, const std::string& args
        ) override {
            return this->owner_.dispatch(handler_name, args);
        }
    };

    const std::string name_;
    mutable std::mutex handlers_mutex_;
    std::map<std::string, std::shared_ptr<Handler>> handlers_ = {
        {
            "hi",
            std::make_shared<Handler>(
                [this](const std::string num) {
                    return '"' + "Hi, here's ["s + this->name_ + "].  Got "s + num + "~" + '"';
                }
            )
        }
    };

    const Participant participant_;
    std::shared_ptr<::eprosima::fastdds::dds::rpc::RpcServer> server_;
    std::thread server_thread_;
  public:
    explicit Impl(const std::string& name)
    : name_{name},
      participant_{create_participant()},
      server_{
          [this] {
              const auto router = std::make_shared<Router>(*this);
              const auto server = gen::create_ProcessorServer(
                  *this->participant_,
                  this->name_.c_str(),
                  ::eprosima::fastdds::dds::ReplierQos{},
                  0,
                  router
              );
              if (!server) {
                  throw std::runtime_error{"Failed to create Urpc2 server"};
              }
              return server;
          }()
      },
      server_thread_{
          [this] { this->server_->run(); }
      } {
        std::this_thread::sleep_for(1s);
    }

    /*
     * 在成员自动析构前手动关闭, 这是正确性要求, 不是风格问题.
     *
     * server_thread_ 声明在 server_ 之后, 逆序析构时会先被销毁; 若此时
     * run() 仍在阻塞运行, 销毁一个 joinable 的线程会触发 std::terminate().
     * 所以必须先 stop() 打断运行循环, 再 join() 等线程退出.
     *
     * 随后先销毁 server (释放它从 participant 借来的 DDS 实体), 最后手动
     * 删除裸指针 participant_. 顺序反了会先删掉 participant, 而 server 仍
     * 持有其实体, 造成悬空访问.
     */
    ~Impl() {
        if (this->server_) {
            this->server_->stop();
        }
        if (this->server_thread_.joinable()) {
            this->server_thread_.join();
        }

        this->server_.reset();
    }

    auto name() const noexcept -> const std::string& { return this->name_; }

    /*
     * 重新注册同名处理器会替换旧的可调用对象.  互斥量只保护注册表变更和
     * 查找; 处理器执行发生在 dispatch() 的临界区之外.
     */
    void register_handler(const std::string& handler_name, Handler handler) {
        if (!handler) {
            throw std::invalid_argument{"Handler must be callable"};
        }
        {
            const auto lock = std::lock_guard<std::mutex>{this->handlers_mutex_};
            this->handlers_[handler_name] = std::make_shared<Handler>(std::move(handler));
        }
    }

    auto call(
        const std::string& receiver_name,
        const std::string& handler_name,
        const std::string& args,
        const std::chrono::duration<double> timeout
    ) -> std::string {
        namespace rpc = ::eprosima::fastdds::dds::rpc;

        // --- 建 client: 工厂/participant/client 任何本地失败统一成 LocalError ---
        auto participant = Participant{};
        std::shared_ptr<gen::Processor> client;
        try {
            participant = Participant{create_participant()};
            client = gen::create_ProcessorClient(
                *participant,
                receiver_name.c_str(),
                ::eprosima::fastdds::dds::RequesterQos{}
            );
        }
        catch (const rpc::RpcException& e) {
            throw urpc2::LocalError{
                "Failed to set up Urpc2 client for instance \""s + receiver_name + "\" (" + e.what() + ')'};
        }
        catch (const std::exception& e) {
            throw urpc2::LocalError{
                "Failed to set up Urpc2 client for instance \""s + receiver_name + "\" (" + e.what() + ')'};
        }
        if (!client) {
            throw urpc2::LocalError{
                "Failed to create Urpc2 client for instance \""s + receiver_name + '"'};
        }

        std::this_thread::sleep_for(5s);  // 给 requester 和 replier 留出彼此发现的时间.

        auto future = client->router(handler_name, args);
        if (future.wait_for(timeout) != std::future_status::ready) {
            throw urpc2::Timeout{
                "Urpc2 call to \""s + receiver_name + "\"/\"" + handler_name + "\" timed out"};
        }

        // future.get() 会 rethrow 存进 promise 的 Fast DDS RpcException 子类. 在此
        // 边界把它们统一翻译成 urpc2 自己的、以 std::exception 为根的异常, 既不把
        // DDS 类型泄漏给调用方, 也避免非 std::exception 的 RpcException 逃逸导致
        // std::terminate. catch 顺序须由派生到基类 (Remote 专用码在 RpcRemoteException 前).
        try {
            return future.get();
        }
        catch (const rpc::RemoteUnknownOperationError& e) {
            throw urpc2::UnknownOperation{
                "No such handler \""s + handler_name + "\" on instance \"" + receiver_name + "\" (" + e.what() + ')'};
        }
        catch (const rpc::RpcBrokenPipeException& e) {
            // client 侧的 broken pipe 只源于「发请求时 requester 未匹配」, 即目标未被发现.
            throw urpc2::ServerNotFound{
                "No Urpc2 server discovered for instance \""s + receiver_name + "\" (" + e.what() + ')'};
        }
        catch (const rpc::RpcTimeoutException& e) {
            throw urpc2::Timeout{
                "Urpc2 call to \""s + receiver_name + "\"/\"" + handler_name + "\" timed out (" + e.what() + ')'};
        }
        catch (const rpc::RpcRemoteException& e) {
            // 其余远端错误 (invalid argument / unsupported / out of resources / unknown exception).
            throw urpc2::RemoteError{
                "Remote error from instance \""s + receiver_name + "\" (" + e.what() + ')'};
        }
        catch (const rpc::RpcException& e) {
            // 兜底: 任何未预料的 RpcException, 降级为根 Error, 保留消息, 绝不外泄或 terminate.
            throw urpc2::Error{
                "Urpc2 RPC error on instance \""s + receiver_name + "\" (" + e.what() + ')'};
        }
    }

    auto dispatch(const std::string& handler_name, const std::string& args) const -> std::string {
        const std::shared_ptr<Handler> handler = [&] {
            const auto lock = std::lock_guard<std::mutex>{this->handlers_mutex_};
            const auto iter = this->handlers_.find(handler_name);
            if (iter == this->handlers_.end()) {
                const auto message = "No such Urpc2 handler: "s + handler_name;
                throw ::eprosima::fastdds::dds::rpc::RemoteUnknownOperationError{message.c_str()};
            }
            return iter->second;
        }();
        return (*handler)(args);
    }
};

urpc2::Urpc2::Urpc2(const std::string& name): impl_{std::make_unique<Impl>(name)} {}
urpc2::Urpc2::~Urpc2() = default;
auto urpc2::Urpc2::name() const noexcept -> const std::string& {
    return this->impl_->name();
}
void urpc2::Urpc2::register_handler(const std::string& handler_name, Handler handler) {
    this->impl_->register_handler(handler_name, std::move(handler));
}
auto urpc2::Urpc2::call(
    const std::string& receiver_name,
    const std::string& handler_name,
    const std::string& args,
    const std::chrono::duration<double> timeout
) -> std::string {
    return this->impl_->call(receiver_name, handler_name, args, timeout);
}
