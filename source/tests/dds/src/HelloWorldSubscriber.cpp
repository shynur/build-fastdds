#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/DataReaderListener.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>

#include "types/HelloWorldPubSubTypes.hpp"


using namespace std::literals;

class HelloWorldSubscriber {
  public:
    explicit HelloWorldSubscriber(const std::string& name): name_(name), listener_(name) {}
    ~HelloWorldSubscriber() {
        if (this->reader_ != nullptr) {
            this->subscriber_->delete_datareader(this->reader_);
        }
        if (this->subscriber_ != nullptr) {
            this->participant_->delete_subscriber(this->subscriber_);
        }
        if (this->topic_ != nullptr) {
            this->participant_->delete_topic(this->topic_);
        }
        if (this->participant_ != nullptr) {
            ::eprosima::fastdds::dds::DomainParticipantFactory::get_instance()->delete_participant(this->participant_);
        }
    }
    bool init() {
        auto pqos = ::eprosima::fastdds::dds::DomainParticipantQos{};
        pqos.name(this->name_ + "_participant");
        this->participant_ = ::eprosima::fastdds::dds::DomainParticipantFactory::get_instance()->create_participant(0, pqos);
        if (this->participant_ == nullptr) {
            return false;
        }

        this->type_.register_type(this->participant_);

        this->topic_ = this->participant_->create_topic(
            "HelloWorldTopic", this->type_.get_type_name(), ::eprosima::fastdds::dds::TOPIC_QOS_DEFAULT
        );
        if (this->topic_ == nullptr) {
            return false;
        }

        this->subscriber_ = this->participant_->create_subscriber(::eprosima::fastdds::dds::SUBSCRIBER_QOS_DEFAULT);
        if (this->subscriber_ == nullptr) {
            return false;
        }

        this->reader_ = this->subscriber_->create_datareader(
            this->topic_, ::eprosima::fastdds::dds::DATAREADER_QOS_DEFAULT, &this->listener_
        );
        if (this->reader_ == nullptr) {
            return false;
        }
        std::cout << "[" << this->name_ << "] created, waiting for data..." << std::endl;
        return true;
    }
    void run() {
        while (HelloWorldSubscriber::running_) {
            std::this_thread::sleep_for(100ms);
        }
    }

    class SubListener: public ::eprosima::fastdds::dds::DataReaderListener {
      public:
        explicit SubListener(const std::string& name): name_(name) {}
        void on_subscription_matched(
            ::eprosima::fastdds::dds::DataReader *reader,
            const ::eprosima::fastdds::dds::SubscriptionMatchedStatus& info
        ) override {
            static_cast<void>(reader);
            if (info.current_count_change == 1) {
                std::cout << "[" << this->name_ << "] a publisher matched (total=" << info.current_count << ")." << std::endl;
            }
            else if (info.current_count_change == -1) {
                std::cout << "[" << this->name_ << "] a publisher unmatched (total=" << info.current_count << ")." << std::endl;
            }
        }
        void on_data_available(::eprosima::fastdds::dds::DataReader *reader) override {
            auto info = ::eprosima::fastdds::dds::SampleInfo{};
            while (reader->take_next_sample(&this->sample_, &info) == ::eprosima::fastdds::dds::RETCODE_OK) {
                if (info.valid_data) {
                    ++this->received_;
                    std::cout << "[" << this->name_ << "] RECEIVED sample: index=" << this->sample_.index()
                              << " message='" << this->sample_.message()
                              << "' (total received=" << this->received_ << ")"
                              << std::endl;
                }
            }
        }

        std::string name_;
        HelloWorld sample_;
        std::atomic<std::uint32_t> received_{0};
    };

    static std::atomic<bool> running_;

  private:
    std::string name_;
    ::eprosima::fastdds::dds::DomainParticipant *participant_ = nullptr;
    ::eprosima::fastdds::dds::Subscriber *subscriber_ = nullptr;
    ::eprosima::fastdds::dds::Topic *topic_ = nullptr;
    ::eprosima::fastdds::dds::DataReader *reader_ = nullptr;
    ::eprosima::fastdds::dds::TypeSupport type_{new HelloWorldPubSubType};
    SubListener listener_;
};

std::atomic<bool> HelloWorldSubscriber::running_{true};

int main(int argc, char **argv) {
    auto name = std::string{"Subscriber"};
    if (argc > 1) {
        name = argv[1];
    }

    std::signal(SIGINT, [](int) { HelloWorldSubscriber::running_ = false; });

    auto sub = HelloWorldSubscriber{name};
    if (!sub.init()) {
        std::cerr << "[" << name << "] init failed" << std::endl;
        return 1;
    }
    sub.run();
    std::cout << "[" << name << "] done." << std::endl;
}
