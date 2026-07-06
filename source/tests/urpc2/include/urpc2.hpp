#ifndef URPC2_HPP
#define URPC2_HPP

#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace urpc2 {

/**
 * @brief Minimal RPC-over-DDS endpoint used directly by applications.
 *
 * Each Urpc2 object owns one Fast DDS RPC service. The constructor name is used
 * as the DDS RPC service name, so it must be unique in the DDS domain. Handler
 * names are scoped by the owning Urpc2 instance: two different instances may
 * both register an "add" handler, and callers disambiguate them by passing the
 * receiver instance name to call().
 *
 * The framework treats request and response bodies as opaque strings. The
 * intended first-version convention is JSON text, but Urpc2 does not parse or
 * validate that payload.
 */
class Urpc2 {
  public:
    /**
     * @brief Function signature for a registered RPC handler.
     *
     * @param args Opaque request payload provided by the caller.
     * @return Opaque response payload returned to the caller.
     *
     * @note Handler exceptions are propagated through the generated Fast DDS RPC
     *       layer as remote exceptions.
     */
    using Handler = std::function<std::string(std::string)>;

    /**
     * @brief Create an RPC endpoint with a unique instance name.
     *
     * @param name Unique name of this endpoint in the DDS domain.
     *
     * @throws std::invalid_argument if @p name is empty.
     * @throws std::runtime_error if Fast DDS participant or server creation
     *         fails.
     */
    explicit Urpc2(std::string name);

    /**
     * @brief Stop the owned RPC server and release Fast DDS resources.
     */
    ~Urpc2();

    Urpc2(const Urpc2&) = delete;
    Urpc2& operator=(const Urpc2&) = delete;
    Urpc2(Urpc2&&) = delete;
    Urpc2& operator=(Urpc2&&) = delete;

    /**
     * @brief Return this endpoint's unique instance name.
     */
    const std::string& name() const noexcept;

    /**
     * @brief Register or replace a named handler on this endpoint.
     *
     * @param handler_name Name used by callers to select the handler.
     * @param handler Callable that receives the request string and returns the
     *        response string.
     *
     * @throws std::invalid_argument if @p handler_name is empty or @p handler is
     *         not callable.
     */
    void register_handler(std::string handler_name, Handler handler);

    /**
     * @brief Call a handler on another Urpc2 endpoint.
     *
     * This overload uses default_call_timeout().
     *
     * @param receiver_name Unique name of the receiver endpoint.
     * @param handler_name Handler name registered on the receiver endpoint.
     * @param args Opaque request payload.
     * @return Opaque response payload returned by the receiver handler.
     *
     * @throws std::invalid_argument if @p receiver_name or @p handler_name is
     *         empty.
     * @throws std::runtime_error if the call times out or client creation fails.
     * @throws eprosima::fastdds::dds::rpc::RpcException for remote Fast DDS RPC
     *         errors.
     */
    std::string call(
            const std::string& receiver_name,
            const std::string& handler_name,
            const std::string& args);

    /**
     * @brief Call a handler on another Urpc2 endpoint with a custom timeout.
     *
     * @param receiver_name Unique name of the receiver endpoint.
     * @param handler_name Handler name registered on the receiver endpoint.
     * @param args Opaque request payload.
     * @param timeout Maximum time to wait for the reply after the request is
     *        sent.
     * @return Opaque response payload returned by the receiver handler.
     *
     * @throws std::invalid_argument if @p receiver_name or @p handler_name is
     *         empty.
     * @throws std::runtime_error if the call times out or client creation fails.
     * @throws eprosima::fastdds::dds::rpc::RpcException for remote Fast DDS RPC
     *         errors.
     */
    std::string call(
            const std::string& receiver_name,
            const std::string& handler_name,
            const std::string& args,
            std::chrono::milliseconds timeout);

    /**
     * @brief Default reply wait used by call() without an explicit timeout.
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
