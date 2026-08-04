#include "urpc2.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <unistd.h>

#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/qos/DomainParticipantQos.hpp>
#include <fastdds/dds/domain/qos/ReplierQos.hpp>
#include <fastdds/dds/domain/qos/RequesterQos.hpp>
#include <fastdds/dds/rpc/exceptions.hpp>
#include <fastdds/dds/rpc/interfaces/RpcServer.hpp>
#include <fastdds/rtps/common/Property.hpp>
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.hpp>
#include <fastdds/utils/IPFinder.hpp>

#include <nlohmann/json.hpp>

#include "types/processor.hpp"
#include "types/processorClient.hpp"
#include "types/processorServer.hpp"

namespace urpc2_detail {
    // 枚举值按严重程度递增, 供日志阈值过滤使用.
    enum class LogLevel {
        info,
        error,
    };

    struct NamedLogLevel {
        std::string_view name;
        LogLevel level;
    };

    static constexpr NamedLogLevel named_log_levels[] = {
        {"info", LogLevel::info},
        {"error", LogLevel::error},
    };

    static auto configured_log_level() noexcept -> std::optional<LogLevel> {
        static const auto configured = []() -> std::optional<LogLevel> {
            const auto *const value = std::getenv("URPC2_LOG");
            if (value == nullptr) {
                return std::nullopt;
            }

            const auto name = std::string_view{value};
            for (const auto& named_level: named_log_levels) {
                if (named_level.name == name) {
                    return named_level.level;
                }
            }
            return std::nullopt;
        }();
        return configured;
    }

    static auto should_log(const LogLevel level) noexcept -> bool {
        const auto configured = configured_log_level();
        return configured.has_value() && level >= *configured;
    }

    static constexpr auto ansi_reset = "\033[0m";
    static constexpr auto ansi_bold = "\033[1m";
    static constexpr auto ansi_dim = "\033[2m";
    static constexpr auto ansi_italic = "\033[3m";
    static constexpr auto ansi_underline = "\033[4m";
    static constexpr auto ansi_bold_red = "\033[1;31m";
    static constexpr auto ansi_bold_green = "\033[1;32m";
    static constexpr auto ansi_bold_cyan = "\033[1;36m";
    static constexpr auto ansi_green = "\033[32m";
    static constexpr auto ansi_yellow = "\033[33m";
    static constexpr auto ansi_magenta = "\033[35m";

    static auto colors_enabled() noexcept -> bool {
        static const auto enabled = [] {
            const auto *const no_color = std::getenv("NO_COLOR");
            return (no_color == nullptr || no_color[0] == '\0')
                && ::isatty(::fileno(stderr)) != 0;
        }();
        return enabled;
    }

    static auto style_code(const char *const code) noexcept -> const char * {
        return colors_enabled() ? code : "";
    }

    static auto styled(std::string text, const char *const code) -> std::string {
        if (!colors_enabled()) {
            return text;
        }

        auto result = std::string{code};
        result += text;
        result += ansi_reset;
        return result;
    }

    static auto action(std::string text) -> std::string {
        return styled(std::move(text), ansi_bold_cyan);
    }

    static auto entity(std::string text) -> std::string {
        return styled(std::move(text), ansi_underline);
    }

    static auto argument(std::string text) -> std::string {
        return styled(std::move(text), ansi_yellow);
    }

    static auto response(std::string text) -> std::string {
        return styled(std::move(text), ansi_green);
    }

    static auto value(std::string text) -> std::string {
        return styled(std::move(text), ansi_magenta);
    }

    static auto separator(std::string text) -> std::string {
        return styled(std::move(text), ansi_dim);
    }

    /*
     * 将 CBOR 二进制数据转换为 JSON 字符串用于日志显示.
     * 如果 CBOR 无法转换为合法 JSON, 返回占位符文本.
     */
    static auto cbor_to_json_for_logging(const std::vector<std::uint8_t>& cbor_data) -> std::string {
        try {
            const auto json_obj = ::nlohmann::json::from_cbor(cbor_data);
            return json_obj.dump();
        }
        catch (...) {
            return "<binary data, " + std::to_string(cbor_data.size()) + " bytes>";
        }
    }

    static auto cbor_to_json_for_logging(const std::string& cbor_str) -> std::string {
        return cbor_to_json_for_logging(
            std::vector<std::uint8_t>(cbor_str.begin(), cbor_str.end())
        );
    }

    static void format_timestamp(char (&timestamp)[32]) noexcept {
        const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        auto local_time = std::tm{};
        char date_time[20]{};
        char utc_offset[6]{};

        if (
            localtime_r(&now, &local_time) != nullptr
            && std::strftime(date_time, sizeof(date_time), "%Y-%m-%dT%H:%M:%S", &local_time) != 0
            && std::strftime(utc_offset, sizeof(utc_offset), "%z", &local_time) == 5
        ) {
            std::snprintf(
                timestamp,
                sizeof(timestamp),
                "%s%c%c%c:%c%c",
                date_time,
                utc_offset[0],
                utc_offset[1],
                utc_offset[2],
                utc_offset[3],
                utc_offset[4]
            );
        }
        else {
            std::snprintf(timestamp, sizeof(timestamp), "1970-01-01T00:00:00+00:00");
        }
    }

    static void log_info(
        const char *const file,
        const int line,
        const char *const function,
        const std::string& message
    ) noexcept {
        char timestamp[32]{};
        format_timestamp(timestamp);

        const auto *const reset = style_code(ansi_reset);
        const auto *const bold = style_code(ansi_bold);
        const auto *const dim = style_code(ansi_dim);
        const auto *const italic = style_code(ansi_italic);
        const auto *const info = style_code(ansi_bold_green);

        std::fprintf(
            stderr,
            "%s%.11s%s%s%.8s%s%s%s%s %s[INFO]%s urpc2 - "
            "%s%s: line %d:%s %s%s%s: %s%s\n",
            dim,
            timestamp,
            reset,
            bold,
            timestamp + 11,
            reset,
            dim,
            timestamp + 19,
            reset,
            info,
            reset,
            dim,
            file,
            line,
            reset,
            italic,
            function,
            reset,
            message.c_str(),
            reset
        );
        std::fflush(stderr);
    }

    static void log_exception(
        const char *const file,
        const int line,
        const char *const function,
        const char *const exception_class,
        const std::string& message
    ) noexcept {
        char timestamp[32]{};
        format_timestamp(timestamp);

        const auto *const reset = style_code(ansi_reset);
        const auto *const bold = style_code(ansi_bold);
        const auto *const dim = style_code(ansi_dim);
        const auto *const italic = style_code(ansi_italic);
        const auto *const error = style_code(ansi_bold_red);

        std::fprintf(
            stderr,
            "%s%.11s%s%s%.8s%s%s%s%s %s[ERROR]%s urpc2 - "
            "%s%s: line %d:%s %s%s%s: %s%s%s{%s%s%s}\n",
            dim,
            timestamp,
            reset,
            bold,
            timestamp + 11,
            reset,
            dim,
            timestamp + 19,
            reset,
            error,
            reset,
            dim,
            file,
            line,
            reset,
            italic,
            function,
            reset,
            error,
            exception_class,
            reset,
            error,
            message.c_str(),
            reset
        );
        std::fflush(stderr);
    }

}

#define URPC2_LOG_INFO(message) \
    do { \
        if (::urpc2_detail::should_log(::urpc2_detail::LogLevel::info)) { \
            const auto urpc2_log_message = std::string{message}; \
            ::urpc2_detail::log_info( \
                __FILE__, __LINE__, __PRETTY_FUNCTION__, urpc2_log_message); \
        } \
    } while (false)

#define URPC2_THROW(exception_class, message) \
    do { \
        auto urpc2_exception_message = std::string{message}; \
        if (::urpc2_detail::should_log(::urpc2_detail::LogLevel::error)) { \
            ::urpc2_detail::log_exception( \
                __FILE__, __LINE__, __PRETTY_FUNCTION__, #exception_class, urpc2_exception_message); \
        } \
        throw exception_class{std::move(urpc2_exception_message)}; \
    } while (false)

#define URPC2_THROW_C_STRING(exception_class, message) \
    do { \
        const auto urpc2_exception_message = std::string{message}; \
        if (::urpc2_detail::should_log(::urpc2_detail::LogLevel::error)) { \
            ::urpc2_detail::log_exception( \
                __FILE__, __LINE__, __PRETTY_FUNCTION__, #exception_class, urpc2_exception_message); \
        } \
        throw exception_class{urpc2_exception_message.c_str()}; \
    } while (false)


/*
 * 开启网段限制时, 唯一允许通信的网段.
 *
 * 车上的部署形态是固定的: 库跑在小车内的三个控制器上, 控制器之间由我们手工组成
 * 192.168.192.0/24 这一个局域网.  除此之外的网卡 (车载 4G, 调试用的 Wi-Fi,
 * docker0 之类的虚拟网桥, 以及回环) 都不该承载 urpc2 流量: 既避免把内部 RPC
 * 和发现报文播到外部网络, 也避免 DDS 在多张网卡上重复发现, 匹配到错误的
 * locator.
 */
static constexpr auto allowed_subnet_prefix = "192.168.192.";

/*
 * 车内 Ethernet MTU 为 1500.  让单个 RTPS message 落在 MTU 以内可以避免 IP 分片:
 * IP 分片后只要丢一个分片, 整个 datagram 就报废, 在突发流量下会放大丢包.
 *
 * 关键: 这个预算只能加在 writer 的 `fastdds.max_message_size` 属性上, 绝不能写进
 * transport 的 `maxMessageSize`.  两者性质完全不同 --
 *
 * - transport 的 `maxMessageSize` 是**硬门限**, 且收发两端都生效.  发送端超过它
 *   (或超过 IPv4 datagram 的 65507 上限) 就发不出去; 接收端收到超长 datagram 会
 *   截断进而解析失败.  两种情况都不打日志, 表现为 reliable reader 对着缺失的
 *   fragment 无限 NACK_FRAG, RPC 调用方只看到超时.
 * - writer 属性只参与 fragment 大小的反推, 超出它顶多让 IP 层多分一次片, 不丢包.
 *
 * 为什么一定要留余量: Fast DDS 的 `BaseWriter::calculate_max_payload_size()` 在
 * 由 message 上限反推 fragment 大小时, 只为 RTPS header, INFO_DST, INFO_TS,
 * DATA_FRAG header 和 HEARTBEAT 预留了固定开销, **完全没有为 inline QoS 预留**.
 * 而 RPC reply 的 inline QoS 里有 PID_CONTENT_FILTER_INFO, 它的长度随「writer 匹配
 * 到的不同 content filter 个数」增长 -- 每多一个 requester 就多一个 16 字节的
 * filterSignature.  于是实际发出的 message 会比 writer 自己以为的大出几十字节:
 * 只有一个 requester 时不多不少正好卡在上限, 第二个 requester 一上线就溢出.
 *
 * 所以 transport 门限保持 Fast DDS 默认的 65500, 与 MTU 目标彻底解耦: 即使 inline
 * QoS 涨到上百字节, 也只是让 datagram 略微超过 MTU 而已, 不会静默丢包.
 */
static constexpr std::uint32_t vehicle_writer_max_message_size = 1200;
static constexpr std::uint32_t vehicle_udp_socket_buffer_size = 1024 * 1024;

/*
 * 判断是否启用上述网段限制: 环境变量 RBK_IN_CAR 存在且非空字符串时启用.
 *
 * 限制只在车上才成立, 而开发机, CI runner 和测试容器都没有 192.168.192.* 网卡,
 * 无条件启用会让它们连 participant 都建不起来.  故以 RBK_IN_CAR 作为"当前跑在
 * 车内"的标志: 车上的启动环境设置它, 其余环境不设置, 于是默认退回 Fast DDS 的
 * builtin transport 行为 (所有网卡).
 */
static auto subnet_restriction_enabled() -> bool {
    const auto *const in_car = std::getenv("RBK_IN_CAR");
    URPC2_LOG_INFO(
        urpc2_detail::entity("RBK_IN_CAR") + '='
        + urpc2_detail::value(in_car == nullptr ? "" : in_car)
    );
    return in_car != nullptr && in_car[0] != '\0';
}

/*
 * 收集本机属于 allowed_subnet_prefix 网段的 IPv4 地址.
 *
 * 只做字符串前缀匹配: /24 的网段用点分十进制前缀判断即等价于掩码判断, 无需
 * 引入位运算. 不含回环 (getIPs 默认不返回), 故本机进程之间也走该网段通信.
 */
static auto find_allowed_ipv4s() -> std::vector<std::string> {
    auto interfaces = std::vector<::eprosima::fastdds::rtps::IPFinder::info_IP>{};
    ::eprosima::fastdds::rtps::IPFinder::getIPs(&interfaces, false);

    auto addresses = std::vector<std::string>{};
    for (const auto& nic: interfaces) {
        if (nic.type != ::eprosima::fastdds::rtps::IPFinder::IP4) {
            continue;
        }
        if (nic.name.rfind(allowed_subnet_prefix, 0) == 0) {
            addresses.push_back(nic.name);
        }
    }
    return addresses;
}

/*
 * 创建 participant; 若 subnet_restriction_enabled(), 则限制在 allowed_subnet_prefix
 * 网段内通信.
 *
 * 限制的做法是关掉 builtin transport, 换成一个自建的 UDPv4 transport, 并把它的
 * interface allowlist 填成本机在该网段上的地址.  allowlist 同时约束收和发,
 * 于是用户数据和 builtin 的发现报文 (含发现用的多播) 都只经过这张网卡.
 * 另外打开 ignore_non_matching_locators, 丢弃对端宣告的、本机 transport 无法
 * 匹配的 locator, 避免为网段外的地址保留无用的 sender resource.
 *
 * 启用限制而本机在该网段上没有地址时直接失败, 而不是静默退回到"所有网卡":
 * 后者会让配错网络的控制器看起来能通, 却把报文发到了不该去的地方, 排查成本
 * 远高于启动即报错.  未启用限制时用默认 QoS, 即 Fast DDS 原本的所有网卡行为.
 */
static auto create_participant() -> ::eprosima::fastdds::dds::DomainParticipant * {
    const auto factory = ::eprosima::fastdds::dds::DomainParticipantFactory::get_shared_instance();
    if (!factory) {
        URPC2_THROW(std::runtime_error, "Failed to get Fast DDS participant factory");
    }

    auto qos = ::eprosima::fastdds::dds::DomainParticipantQos{};
    if (subnet_restriction_enabled()) {
        const auto addresses = find_allowed_ipv4s();
        if (addresses.empty()) {
            URPC2_THROW(
                std::runtime_error,
                "RBK_IN_CAR is set but no local network interface is in "s
                + allowed_subnet_prefix
                + "0/24; urpc2 only communicates on that subnet in the car"
            );
        }

        const auto udp = std::make_shared<::eprosima::fastdds::rtps::UDPv4TransportDescriptor>();
        // 不动 maxMessageSize: 它是收发两端的硬门限, 调小会让略微超限的 message
        // 被静默丢弃.  MTU 目标改由 writer 的 fastdds.max_message_size 属性达成.
        udp->sendBufferSize = vehicle_udp_socket_buffer_size;
        udp->receiveBufferSize = vehicle_udp_socket_buffer_size;
        for (const auto& address: addresses) {
            udp->interface_allowlist.emplace_back(address);
        }
        qos.transport().use_builtin_transports = false;
        qos.transport().user_transports.push_back(udp);
        qos.wire_protocol().ignore_non_matching_locators = true;
    }

    auto *const participant = factory->create_participant(0, qos);
    if (participant == nullptr) {
        URPC2_THROW(std::runtime_error, "Failed to create Fast DDS participant");
    }
    return participant;
}

/*
 * 给 writer 设置分片预算, 令它按 MTU 而不是按 64 KiB 切分 sample.
 *
 * request 和 reply 两个方向都可能出现大 payload, 故 requester 和 replier 的
 * writer 都要设.
 */
static void set_writer_fragment_budget(::eprosima::fastdds::dds::DataWriterQos& writer_qos) {
    auto property = ::eprosima::fastdds::rtps::Property{};
    property.name("fastdds.max_message_size");
    property.value(std::to_string(vehicle_writer_max_message_size));
    writer_qos.properties().properties().push_back(std::move(property));
}

/*
 * Requester 默认给 reply reader 配置 KEEP_ALL.  一旦某个分片回复永久缺少一个
 * fragment, reliable reader 会一直等待该序号, 后续完整回复也无法交付给 RPC
 * processing thread.  reply reader 只保留最新样本可让后续回复淘汰这个不完整的
 * 旧序号, 从而恢复调用链.
 *
 * 深度 1 要求每个 requester 同时最多有一个有效请求; CachedClient::call_mutex
 * 在 call() 中保证这一点.  不收紧 reader resource limits: reply writer 的全局
 * 序列空间会让发给其它 requester 的回复暂时计入 unknown missing changes;
 * max_samples 太小会在 KEEP_LAST 淘汰旧样本前直接拒绝新回复.  request writer
 * 保持 RequesterQos 的默认 KEEP_ALL, 以免削弱尚未确认请求的重传能力.
 */
static auto create_requester_qos() -> ::eprosima::fastdds::dds::RequesterQos {
    auto qos = ::eprosima::fastdds::dds::RequesterQos{};
    qos.reader_qos.history().kind = ::eprosima::fastdds::dds::KEEP_LAST_HISTORY_QOS;
    qos.reader_qos.history().depth = 1;
    set_writer_fragment_budget(qos.writer_qos);
    return qos;
}

/*
 * Replier 的 reply writer 是大 payload 的主要来源 (例如地图列表), 同样需要按 MTU
 * 切分.  其余 QoS 保持 ReplierQos 的默认值.
 */
static auto create_replier_qos() -> ::eprosima::fastdds::dds::ReplierQos {
    auto qos = ::eprosima::fastdds::dds::ReplierQos{};
    set_writer_fragment_budget(qos.writer_qos);
    return qos;
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
        std::vector<std::uint8_t> router(
            const ::eprosima::fastdds::dds::rpc::RpcRequest&,
            const std::string& handler_name, const std::vector<std::uint8_t>& args
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
                [this](const std::vector<std::uint8_t> num) {
                    const auto num_str = std::string(num.begin(), num.end());
                    const auto response = '"' + "Hi, here's ["s + this->name_ + "].  Got "s + num_str + "~" + '"';
                    return std::vector<std::uint8_t>(response.begin(), response.end());
                }
            )
        }
    };

    const Participant participant_;
    std::shared_ptr<::eprosima::fastdds::dds::rpc::RpcServer> server_;
    std::thread server_thread_;

    struct CachedClient {
        explicit CachedClient(std::shared_ptr<gen::Processor> processor)
        : processor{std::move(processor)} {}

        std::mutex call_mutex;
        std::shared_ptr<gen::Processor> processor;
    };

    /*
     * Client-side 缓存.
     *
     * 出站调用不再按次创建/销毁 participant, 而是: 整个实例懒创建一个专用的
     * client-side participant (首个出站调用时才建, 纯 server 实例零开销), 并按
     * receiver name 缓存生成的 client.  每个 client 持有自己的 requester 和
     * 收发线程.  reply reader 使用 KEEP_LAST(1), 因此 CachedClient::call_mutex
     * 将同一 receiver 的调用串行化, 防止并发回复互相覆盖; 不同 receiver 使用
     * 不同缓存项, 仍可并发.  server 掉线重连由 DDS 发现自动重匹配, 缓存条目
     * 无需失效处理.
     *
     * 不复用 server 侧的 participant_: DomainParticipant::create_service() 在
     * 同名 service 已存在时会失败, 复用会让"调用与自己同名的实例"这一场景
     * 直接建不出 client.
     *
     * 声明顺序即析构依赖: clients_ 中的 client 析构时要回到 client_participant_
     * 上删除自己的 requester/service, 故 clients_ 声明在后, 先于 participant 析构.
     */
    std::mutex clients_mutex_;
    Participant client_participant_;
    std::map<std::string, std::shared_ptr<CachedClient>> clients_;

    /*
     * 返回 receiver 对应的缓存 client, 缺失则创建并缓存.
     *
     * 创建放在锁内: 串行化并发的首次调用, 保证每个 receiver 至多建一个
     * client.  任何本地失败 (工厂/participant/client) 统一翻译成 LocalError.
     * 创建失败不留残余缓存条目, 下次调用会重试.
     */
    auto get_client(const std::string& receiver_name) -> std::shared_ptr<CachedClient> {
        const auto lock = std::lock_guard<std::mutex>{this->clients_mutex_};

        const auto iter = this->clients_.find(receiver_name);
        if (iter != this->clients_.end()) {
            return iter->second;
        }

        std::shared_ptr<gen::Processor> processor;
        try {
            if (!this->client_participant_) {
                this->client_participant_ = Participant{create_participant()};
            }
            processor = gen::create_ProcessorClient(
                *this->client_participant_,
                receiver_name.c_str(),
                create_requester_qos()
            );
        }
        catch (const ::eprosima::fastdds::dds::rpc::RpcException& e) {
            URPC2_THROW(
                urpc2::LocalError,
                "Failed to set up Urpc2 client for instance \""s + receiver_name + "\" (" + e.what() + ')'
            );
        }
        catch (const std::exception& e) {
            URPC2_THROW(
                urpc2::LocalError,
                "Failed to set up Urpc2 client for instance \""s + receiver_name + "\" (" + e.what() + ')'
            );
        }
        if (!processor) {
            URPC2_THROW(
                urpc2::LocalError,
                "Failed to create Urpc2 client for instance \""s + receiver_name + '"'
            );
        }

        const auto client = std::make_shared<CachedClient>(std::move(processor));
        this->clients_.emplace(receiver_name, client);
        return client;
    }
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
                  create_replier_qos(),
                  0,
                  router
              );
              if (!server) {
                  URPC2_THROW(std::runtime_error, "Failed to create Urpc2 server");
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
            URPC2_THROW(std::invalid_argument, "Handler must be callable");
        }
        {
            const auto lock = std::lock_guard<std::mutex>{this->handlers_mutex_};
            this->handlers_[handler_name] = std::make_shared<Handler>(std::move(handler));
        }
        URPC2_LOG_INFO(
            urpc2_detail::action("Registered") + " handler \""
            + urpc2_detail::entity(this->name_ + '.' + handler_name) + '"'
        );
    }

    auto call(
        const std::string& receiver_name,
        const std::string& handler_name,
        const std::vector<std::uint8_t>& args,
        const std::chrono::duration<double> timeout
    ) -> std::vector<std::uint8_t> {
        namespace rpc = ::eprosima::fastdds::dds::rpc;

        URPC2_LOG_INFO(
            "Instance \""s + urpc2_detail::entity(this->name_) + "\" "
            + urpc2_detail::action("call") + " \""
            + urpc2_detail::entity(receiver_name + '.' + handler_name)
            + "\": args=" + urpc2_detail::argument(urpc2_detail::cbor_to_json_for_logging(args))
            + ", timeout=" + urpc2_detail::value(std::to_string(timeout.count()) + 's')
        );

        // 取 (或懒创建) 该 receiver 的缓存 client; 本地失败在 get_client() 内
        // 已统一翻译为 LocalError.
        const auto client = this->get_client(receiver_name);

        // reply reader 仅保留最新样本, 所以同一 requester 在任意时刻只允许一个
        // 有效请求.  锁覆盖发请求、等待及取结果; 不同 receiver 使用不同的锁.
        // 等锁属于本地排队, 不计入 reply timeout.
        const auto call_lock = std::lock_guard<std::mutex>{client->call_mutex};

        // 不再盲目 sleep 等发现: client->router() 内部的 send_request 会先
        // wait_for_matching —— 等 requester 与 replier 双向匹配上, 一旦匹配就立即
        // 返回 (最多阻塞 Fast DDS 内部默认的 3 秒). 于是:
        //   - 目标存在:   匹配就绪即发请求, 省去此前无条件的 5s 固定延迟;
        //   - 目标不存在: 匹配等待到点, send_request 返回失败 -> router() 的 future
        //     立即携带 RpcBrokenPipeException, 下面 get() 将其翻译为 ServerNotFound (快速失败).
        auto future = client->processor->router(handler_name, args);
        if (future.wait_for(timeout) != std::future_status::ready) {
            // 放弃这个 future 即可: client 是缓存的, 迟到的回复 (若有) 会由其
            // 收发线程投递到已被放弃的 promise 上, 随后条目被移除, 不会串扰
            // 之后的调用 (每次请求有独立的 sample identity).
            URPC2_THROW(
                urpc2::Timeout,
                "Urpc2 call to \""s + receiver_name + "\"/\"" + handler_name + "\" timed out"
            );
        }

        // future.get() 会 rethrow 存进 promise 的 Fast DDS RpcException 子类. 在此
        // 边界把它们统一翻译成 urpc2 自己的、以 std::exception 为根的异常, 既不把
        // DDS 类型泄漏给调用方, 也避免非 std::exception 的 RpcException 逃逸导致
        // std::terminate. catch 顺序须由派生到基类 (Remote 专用码在 RpcRemoteException 前).
        try {
            auto response = future.get();
            URPC2_LOG_INFO(
                "Instance \""s + urpc2_detail::entity(this->name_) + "\" "
                + urpc2_detail::action("received response from") + " \""
                + urpc2_detail::entity(receiver_name + '.' + handler_name) + "\": "
                + urpc2_detail::argument(urpc2_detail::cbor_to_json_for_logging(args))
                + ' ' + urpc2_detail::separator("->") + ' '
                + urpc2_detail::response(urpc2_detail::cbor_to_json_for_logging(response))
            );
            return response;
        }
        catch (const rpc::RemoteUnknownOperationError& e) {
            URPC2_THROW(
                urpc2::UnknownOperation,
                "No such handler \""s + handler_name + "\" on instance \"" + receiver_name + "\" (" + e.what() + ')'
            );
        }
        catch (const rpc::RpcBrokenPipeException& e) {
            // client 侧的 broken pipe 只源于「发请求时 requester 未匹配」, 即目标未被发现.
            URPC2_THROW(
                urpc2::ServerNotFound,
                "No Urpc2 server discovered for instance \""s + receiver_name + "\" (" + e.what() + ')'
            );
        }
        catch (const rpc::RpcTimeoutException& e) {
            URPC2_THROW(
                urpc2::Timeout,
                "Urpc2 call to \""s + receiver_name + "\"/\"" + handler_name + "\" timed out (" + e.what() + ')'
            );
        }
        catch (const rpc::RpcRemoteException& e) {
            // 其余远端错误 (invalid argument / unsupported / out of resources / unknown exception).
            URPC2_THROW(
                urpc2::RemoteError,
                "Remote error from instance \""s + receiver_name + "\" (" + e.what() + ')'
            );
        }
        catch (const rpc::RpcException& e) {
            // 兜底: 任何未预料的 RpcException, 降级为根 Error, 保留消息, 绝不外泄或 terminate.
            URPC2_THROW(
                urpc2::Error,
                "Urpc2 RPC error on instance \""s + receiver_name + "\" (" + e.what() + ')'
            );
        }
    }

    auto dispatch(const std::string& handler_name, const std::vector<std::uint8_t>& args) const -> std::vector<std::uint8_t> {
        URPC2_LOG_INFO(
            "Instance \""s + urpc2_detail::entity(this->name_ + '.' + handler_name) + "\" "
            + urpc2_detail::action("received call") + ": args="
            + urpc2_detail::argument(urpc2_detail::cbor_to_json_for_logging(args))
        );

        const std::shared_ptr<Handler> handler = [&] {
            const auto lock = std::lock_guard<std::mutex>{this->handlers_mutex_};
            const auto iter = this->handlers_.find(handler_name);
            if (iter == this->handlers_.end()) {
                const auto message = "No such Urpc2 handler: "s + handler_name;
                URPC2_THROW_C_STRING(
                    ::eprosima::fastdds::dds::rpc::RemoteUnknownOperationError,
                    message
                );
            }
            return iter->second;
        }();
        auto response = (*handler)(args);
        URPC2_LOG_INFO(
            "Instance \""s + urpc2_detail::entity(this->name_ + '.' + handler_name) + "\" "
            + urpc2_detail::action("completed call") + ": "
            + urpc2_detail::argument(urpc2_detail::cbor_to_json_for_logging(args))
            + ' ' + urpc2_detail::separator("->") + ' '
            + urpc2_detail::response(urpc2_detail::cbor_to_json_for_logging(response))
        );
        return response;
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
    const std::vector<std::uint8_t>& args,
    const std::chrono::duration<double> timeout
) -> std::vector<std::uint8_t> {
    return this->impl_->call(receiver_name, handler_name, args, timeout);
}
