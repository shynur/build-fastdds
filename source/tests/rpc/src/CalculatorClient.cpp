#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/qos/DomainParticipantQos.hpp>
#include <fastdds/dds/domain/qos/RequesterQos.hpp>
#include <fastdds/dds/rpc/exceptions.hpp>
#include <fastdds/dds/rpc/interfaces/RpcFuture.hpp>

#include "types/calculator.hpp"
#include "types/calculatorClient.hpp"

using namespace std::literals;


class Client {
    std::shared_ptr<::calculator_example::Calculator> client_ = nullptr;
    ::eprosima::fastdds::dds::DomainParticipant *participant_ = nullptr;
    std::string service_name_;
  public:
    explicit Client(const std::string& service_name): service_name_(service_name) {}
    ~Client() {
        this->client_.reset();
        if (this->participant_ != nullptr) {
            this->participant_->delete_contained_entities();
            ::eprosima::fastdds::dds::DomainParticipantFactory::get_shared_instance()->delete_participant(this->participant_);
        }
    }
    void init() {
        const auto factory = ::eprosima::fastdds::dds::DomainParticipantFactory::get_shared_instance();
        if (!factory) {
            throw std::runtime_error{"Failed to get participant factory instance"};
        }

        const auto participant_qos = ::eprosima::fastdds::dds::DomainParticipantQos{};
        this->participant_ = factory->create_participant(0, participant_qos);
        if (this->participant_ == nullptr) {
            throw std::runtime_error{"Failed to create participant"};
        }

        const auto qos = ::eprosima::fastdds::dds::RequesterQos{};
        this->client_ = ::calculator_example::create_CalculatorClient(*this->participant_, this->service_name_.c_str(), qos);
        if (!this->client_) {
            throw std::runtime_error{"Failed to create client"};
        }

        std::cout << "[Client] initialized, service='" << this->service_name_ << "'" << std::endl;
    }

    // 演示 out 参数: 取回 32 位整数的表示范围.
    void call_representation_limits() {
        auto future = this->client_->representation_limits();
        if (future.wait_for(5s) != std::future_status::ready) {
            std::cerr << "[Client] representation_limits timed out" << std::endl;
            return;
        }
        try {
            const auto limits = future.get();
            std::cout << "[Client] representation_limits => min=" << limits.min_value
                      << ", max=" << limits.max_value
                      << std::endl;
        }
        catch (const ::eprosima::fastdds::dds::rpc::RpcException& e) {
            std::cerr << "[Client] representation_limits RPC exception: " << e.what() << std::endl;
        }
    }

    // 演示 in 参数 + 返回值 (可能抛出 OverflowException).
    void call_addition(const std::int32_t x, const std::int32_t y) {
        auto future = this->client_->addition(x, y);
        if (future.wait_for(5s) != std::future_status::ready) {
            std::cerr << "[Client] addition timed out" << std::endl;
            return;
        }
        try {
            const std::int32_t result = future.get();
            std::cout << "[Client] addition => " << x << " + " << y << " = " << result << std::endl;
        }
        catch (const ::eprosima::fastdds::dds::rpc::RpcException& e) {
            std::cout << "[Client] addition(" << x << ", " << y << ") raised exception: " << e.what() << std::endl;
        }
    }
};

int main() {
    auto client = Client{"CalculatorService"};
    client.init();

    // 等待与服务端完成端点匹配.
    std::this_thread::sleep_for(2s);

    // 依次演示: out 参数 / 正常运算 / 触发溢出异常.
    client.call_representation_limits();
    client.call_addition(5, 3);
    client.call_addition(std::numeric_limits<std::int32_t>::max(), 1);

    std::cout << "[Client] done." << std::endl;
}
