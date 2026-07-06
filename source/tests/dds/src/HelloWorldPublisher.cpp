#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>

#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/DataWriterListener.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>

#include "types/HelloWorldPubSubTypes.hpp"


using namespace std::literals;

class HelloWorldPublisher {
  public:
    HelloWorldPublisher() = default;
    ~HelloWorldPublisher() {
        if (this->writer_ != nullptr) {
            this->publisher_->delete_datawriter(this->writer_);
        }
        if (this->publisher_ != nullptr) {
            this->participant_->delete_publisher(this->publisher_);
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
        pqos.name("HelloWorld_pub_participant");
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

        this->publisher_ = this->participant_->create_publisher(::eprosima::fastdds::dds::PUBLISHER_QOS_DEFAULT);
        if (this->publisher_ == nullptr) {
            return false;
        }

        this->writer_ = this->publisher_->create_datawriter(
            this->topic_, ::eprosima::fastdds::dds::DATAWRITER_QOS_DEFAULT, &this->listener_
        );
        if (this->writer_ == nullptr) {
            return false;
        }
        std::cout << "[Publisher] created, waiting for subscribers..." << std::endl;
        return true;
    }
    void run(const std::uint32_t samples) {
        while (this->listener_.matched_ == 0) {
            std::this_thread::sleep_for(100ms);
        }
        std::cout << "[Publisher] " << this->listener_.matched_ << " subscriber(s) matched. Start publishing." << std::endl;

        for (std::uint32_t i = 1; i <= samples && HelloWorldPublisher::running_; ++i) {
            this->hello_.index(i);
            this->hello_.message("HelloWorld from publisher");
            this->writer_->write(&this->hello_);
            std::cout << "[Publisher] SENT sample: index=" << this->hello_.index()
                      << " message='" << this->hello_.message() << "'"
                      << std::endl;
            std::this_thread::sleep_for(500ms);
        }
        std::this_thread::sleep_for(500ms);  // 给 subscribers 一点时间收尾
    }

    class PubListener : public ::eprosima::fastdds::dds::DataWriterListener {
      public:
        std::atomic<int> matched_{0};

        void on_publication_matched(::eprosima::fastdds::dds::DataWriter* writer, const ::eprosima::fastdds::dds::PublicationMatchedStatus& info) override {
            (void)writer;
            if (info.current_count_change == 1) {
                this->matched_ = info.current_count;
                std::cout << "[Publisher] a subscriber matched (total=" << this->matched_ << ")." << std::endl;
            }
            else if (info.current_count_change == -1) {
                this->matched_ = info.current_count;
                std::cout << "[Publisher] a subscriber unmatched (total=" << this->matched_ << ")." << std::endl;
            }
        }
    };

    static std::atomic<bool> running_;

  private:
    HelloWorld hello_;
    ::eprosima::fastdds::dds::DomainParticipant *participant_ = nullptr;
    ::eprosima::fastdds::dds::Publisher *publisher_ = nullptr;
    ::eprosima::fastdds::dds::Topic *topic_ = nullptr;
    ::eprosima::fastdds::dds::DataWriter *writer_ = nullptr;
    ::eprosima::fastdds::dds::TypeSupport type_{new HelloWorldPubSubType};
    PubListener listener_;
};

std::atomic<bool> HelloWorldPublisher::running_{true};

int main(int argc, char **argv) {
    std::uint32_t samples = 10;
    if (argc > 1) {
        samples = static_cast<std::uint32_t>(std::atoi(argv[1]));
    }

    std::signal(SIGINT, [](int) { HelloWorldPublisher::running_ = false; });

    auto pub = HelloWorldPublisher{};
    if (!pub.init()) {
        std::cerr << "[Publisher] init failed" << std::endl;
        return 1;
    }
    pub.run(samples);
    std::cout << "[Publisher] done." << std::endl;
}
