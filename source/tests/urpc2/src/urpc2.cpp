#include "urpc2.hpp"

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

#include "types/processor.hpp"
#include "types/processorClient.hpp"
#include "types/processorServer.hpp"


namespace urpc2 { namespace {

auto create_participant() -> ::eprosima::fastdds::dds::DomainParticipant * {
    const auto *const factory = ::eprosima::fastdds::dds::DomainParticipantFactory::get_shared_instance();
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
void delete_participant(::eprosima::fastdds::dds::DomainParticipant *const participant) noexcept {
    if (participant == nullptr) {
        return;
    }

    participant->delete_contained_entities();
    const auto *const factory = ::eprosima::fastdds::dds::DomainParticipantFactory::get_shared_instance();
    if (factory) {
        factory->delete_participant(participant);
    }
}

/*
 * 单个短生命周期客户端 participant 的 RAII 包装.
 *
 * 当前调用路径会为每次 RPC 创建一个临时 participant 和生成的 ProcessorClient.
 */
class TemporaryParticipant {
  ::eprosima::fastdds::dds::DomainParticipant *const participant_ = nullptr;
  public:
    TemporaryParticipant(): participant_{create_participant()} {}
    ~TemporaryParticipant() {
        delete_participant(this->participant_);
    }

    TemporaryParticipant(const TemporaryParticipant&) = delete;

    auto get() const -> ::eprosima::fastdds::dds::DomainParticipant& {
        return *this->participant_;
    }
};

}}  // namespace urpc2






class urpc2::Urpc2::Impl {
    class Router final: public gen::ProcessorServer_IServerImplementation {
        Impl& owner_;
      public:
        explicit Router(Impl& owner): owner_{owner} {}
        std::string router(
            const ::eprosima::fastdds::dds::rpc::RpcRequest&,
            const std::string& handler_name,
            const std::string& args
        ) override {
            return this->owner_.dispatch(handler_name, args);
        }
    };

    std::string name_;
    ::eprosima::fastdds::dds::DomainParticipant *const participant_ = create_participant();

    std::shared_ptr<::eprosima::fastdds::dds::rpc::RpcServer> server_;
    std::thread server_thread_;

    std::mutex handlers_mutex_;
    std::map<std::string, Handler> handlers_;
  public:
    explicit Impl(const std::string& name)
    : name_{name},
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
     * 删除 participant 前停止服务器.
     *
     * 生成服务器的析构也会调用 stop(), 但这里显式执行可让关闭顺序更清楚:
     * 停止运行循环, 等待线程结束, 释放生成的服务器对象, 然后删除
     * participant.
     */
    ~Impl() {
        if (this->server_) {
            this->server_->stop();
        }
        if (this->server_thread_.joinable()) {
            this->server_thread_.join();
        }

        this->server_.reset();
        delete_participant(this->participant_);
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
            this->handlers_[handler_name] = std::move(handler);
        }
    }

    auto call(
        const std::string& receiver_name,
        const std::string& handler_name,
        const std::string& args,
        const std::chrono::milliseconds timeout
    ) -> std::string {
        TemporaryParticipant participant;
        const auto client = gen::create_ProcessorClient(
            participant.get(),
            receiver_name.c_str(),
            ::eprosima::fastdds::dds::RequesterQos{}
        );
        if (!client) {
            throw std::runtime_error{"Failed to create Urpc2 client"};
        }

        std::this_thread::sleep_for(5s);  // 给 requester 和 replier 留出彼此发现的时间.

        auto future = client->router(handler_name, args);
        if (future.wait_for(timeout) != std::future_status::ready) {
            throw std::runtime_error{"Urpc2 call timed out"};
        }
        return future.get();
    }

    /*
     * 持有注册表互斥量时复制处理器, 并在释放互斥量后调用.  这样一个处理器就
     * 可以注册或替换其他处理器, 而不会让分发路径死锁.
     */
    auto dispatch(const std::string& handler_name, const std::string& args) -> std::string {
        Handler handler;
        {
            const auto lock = std::lock_guard<std::mutex>{this->handlers_mutex_};
            const auto iter = this->handlers_.find(handler_name);
            if (iter == this->handlers_.end()) {
                const auto message = "No such Urpc2 handler: "s + handler_name;
                throw ::eprosima::fastdds::dds::rpc::RemoteUnknownOperationError{message.c_str()};
            }
            handler = iter->second;
        }
        return handler(args);
    }
};

urpc2::Urpc2::Urpc2(std::string name): impl_{std::make_unique<Impl>(std::move(name))} {}
urpc2::Urpc2::~Urpc2() = default;  // TODO: 既然 default, 那不声明 析构函数 也行吧?
auto urpc2::Urpc2::name() const noexcept -> const std::string& {
    return this->impl_->name();
}
void urpc2::Urpc2::register_handler(const std::string handler_name, Handler handler) {
    this->impl_->register_handler(handler_name, std::move(handler));
}
auto urpc2::Urpc2::call(
    const std::string& receiver_name,
    const std::string& handler_name,
    const std::string& args,
    const std::chrono::milliseconds timeout
) -> std::string {
    return this->impl_->call(receiver_name, handler_name, args, timeout);
}
