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
 * Create a plain DomainParticipant in domain 0.
 *
 * The first version intentionally keeps QoS customization out of the public
 * surface. All transport, discovery, and participant defaults come from Fast
 * DDS so the wrapper remains small and easy to inspect.
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
 * Destroy a participant and all entities still owned by it.
 *
 * Generated RPC clients and servers normally clean up their own requester,
 * replier, and service objects. delete_contained_entities() is still called as
 * a defensive cleanup boundary before returning the participant to the factory.
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
 * RAII wrapper for one short-lived client participant.
 *
 * The current call path creates a temporary participant and generated
 * ProcessorClient per RPC call. This is intentionally inefficient, but avoids
 * sharing generated client state while the test framework is still validating
 * the simplest correctness model.
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
 * Hidden implementation for the public Urpc2 facade.
 *
 * The public header stays independent from Fast DDS headers. This class owns
 * the generated RPC server, the server thread, and the handler registry. Calls
 * use the generated ProcessorClient for the IDL operation:
 *
 *     router(in string handler_name, in string args) -> string
 */
class Urpc2::Impl {
  public:
    /*
     * Construct the receiving side of one Urpc2 endpoint.
     *
     * The Urpc2 name becomes the generated RPC service name. The Router object
     * adapts generated server callbacks back into this Impl, and the server
     * run loop is placed on a background thread so the application can both
     * serve requests and issue outgoing calls.
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
     * Stop the server before deleting its participant.
     *
     * Generated server destruction also calls stop(), but doing it explicitly
     * here makes the shutdown order obvious: stop the run loop, join the thread,
     * release generated server objects, then delete the participant.
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
     * Store a handler in the local registry.
     *
     * Re-registering the same name replaces the old callable. The mutex only
     * protects registry mutation and lookup; handler execution happens outside
     * the critical section in dispatch().
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
     * Execute one synchronous outgoing RPC.
     *
     * A temporary participant/client pair is created for the receiver service
     * name. Fast DDS discovery is asynchronous, so the current conservative
     * implementation waits before sending the request. Once the generated
     * future is returned, the caller-provided timeout bounds only the reply
     * wait after the request has been sent.
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
         * Give the requester and replier endpoints time to discover each other.
         * This keeps the first-version behavior deterministic for examples and
         * multi-process tests, at the cost of per-call latency.
         */
        std::this_thread::sleep_for(std::chrono::seconds{5});

        auto future = client->router(handler_name, args);
        if (future.wait_for(timeout) != std::future_status::ready) {
            throw std::runtime_error{"Urpc2 call timed out"};
        }

        return future.get();
    }

    /*
     * Route an incoming generated RPC request to a registered user handler.
     *
     * The handler is copied while holding the registry mutex and invoked after
     * releasing it. This lets one handler register or replace other handlers
     * without deadlocking the dispatch path.
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
     * Adapter from generated ProcessorServer_IServerImplementation to Impl.
     *
     * The IDL has only one operation, router(). The first string selects the
     * user handler and the second string is passed through as opaque payload.
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
