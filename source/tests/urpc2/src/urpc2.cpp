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

class Urpc2::Impl {
  public:
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

        std::this_thread::sleep_for(std::chrono::seconds{5});

        auto future = client->router(handler_name, args);
        if (future.wait_for(timeout) != std::future_status::ready) {
            throw std::runtime_error{"Urpc2 call timed out"};
        }

        return future.get();
    }

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
