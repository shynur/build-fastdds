// CalculatorServer.cpp - RPC 服务端
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/qos/DomainParticipantQos.hpp>
#include <fastdds/dds/domain/qos/ReplierQos.hpp>
#include <fastdds/dds/rpc/interfaces/RpcServer.hpp>

#include "CalculatorServerImpl.hpp"
#include "types/calculatorServer.hpp"

class Server {
public:

    explicit Server(const std::string &service_name) : service_name_(service_name) {}

    ~Server() {
        this->server_.reset();
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

        const std::shared_ptr<::calculator_example::CalculatorServer_IServerImplementation> impl = std::make_shared<CalculatorServerImpl>();
        const auto qos = ::eprosima::fastdds::dds::ReplierQos{};
        // thread_pool_size = 0 表示使用单线程处理请求
        this->server_ = ::calculator_example::create_CalculatorServer(*this->participant_, this->service_name_.c_str(), qos, 0, impl);
        if (!this->server_) {
            throw std::runtime_error{"Server initialization failed"};
        }

        std::cout << "[Server] initialized, service='" << this->service_name_ << "'" << std::endl;
    }

    // 阻塞运行, 直到 stop() 被调用.
    void run() {
        std::cout << "[Server] running" << std::endl;
        this->server_->run();
        std::cout << "[Server] run() returned" << std::endl;
    }

    void stop() {
        std::cout << "[Server] stopping" << std::endl;
        this->server_->stop();
    }

private:

    std::shared_ptr<::eprosima::fastdds::dds::rpc::RpcServer> server_ = nullptr;
    ::eprosima::fastdds::dds::DomainParticipant *participant_ = nullptr;
    std::string service_name_;
};

// 信号处理只做一件事: 置位停止标志 (async-signal-safe).  真正的 stop()/join()
// 由主线程执行, 避免在信号上下文里调用非异步信号安全的清理逻辑.
std::atomic<bool> g_stop_requested{false};

void signal_handler(int signum) {
    static_cast<void>(signum);
    g_stop_requested.store(true);
}

int main() {
    const auto server = std::make_shared<Server>("CalculatorService");
    server->init();

    // run() 阻塞, 放到独立线程中执行, 主线程负责等待停止信号.
    auto thread = std::thread{[server]() { server->run(); }};

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "[Server] press Ctrl+C to stop." << std::endl;

    while (!g_stop_requested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    server->stop();
    thread.join();
    return 0;
}
