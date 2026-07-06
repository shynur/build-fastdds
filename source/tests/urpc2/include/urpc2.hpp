#ifndef URPC2_HPP
#define URPC2_HPP

#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace urpc2 {

class Urpc2 {
  public:
    using Handler = std::function<std::string(std::string)>;

    explicit Urpc2(std::string name);
    ~Urpc2();

    Urpc2(const Urpc2&) = delete;
    Urpc2& operator=(const Urpc2&) = delete;
    Urpc2(Urpc2&&) = delete;
    Urpc2& operator=(Urpc2&&) = delete;

    const std::string& name() const noexcept;

    void register_handler(std::string handler_name, Handler handler);

    std::string call(
            const std::string& receiver_name,
            const std::string& handler_name,
            const std::string& args);

    std::string call(
            const std::string& receiver_name,
            const std::string& handler_name,
            const std::string& args,
            std::chrono::milliseconds timeout);

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
