#include "urpc2.hpp"

#include <cstdlib>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/qos/DomainParticipantQos.hpp>
#include <fastdds/dds/domain/qos/ReplierQos.hpp>
#include <fastdds/dds/domain/qos/RequesterQos.hpp>
#include <fastdds/dds/rpc/exceptions.hpp>
#include <fastdds/dds/rpc/interfaces/RpcServer.hpp>
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.hpp>
#include <fastdds/utils/IPFinder.hpp>

#include "types/processor.hpp"
#include "types/processorClient.hpp"
#include "types/processorServer.hpp"


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
 * 判断是否启用上述网段限制: 环境变量 RBK_IN_CAR 存在且非空字符串时启用.
 *
 * 限制只在车上才成立, 而开发机, CI runner 和测试容器都没有 192.168.192.* 网卡,
 * 无条件启用会让它们连 participant 都建不起来.  故以 RBK_IN_CAR 作为"当前跑在
 * 车内"的标志: 车上的启动环境设置它, 其余环境不设置, 于是默认退回 Fast DDS 的
 * builtin transport 行为 (所有网卡).
 */
static auto subnet_restriction_enabled() -> bool {
    const auto *const in_car = std::getenv("RBK_IN_CAR");
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
        throw std::runtime_error{"Failed to get Fast DDS participant factory"};
    }

    auto qos = ::eprosima::fastdds::dds::DomainParticipantQos{};
    if (subnet_restriction_enabled()) {
        const auto addresses = find_allowed_ipv4s();
        if (addresses.empty()) {
            throw std::runtime_error{
                "RBK_IN_CAR is set but no local network interface is in "s
                + allowed_subnet_prefix
                + "0/24; urpc2 only communicates on that subnet in the car"};
        }

        const auto udp = std::make_shared<::eprosima::fastdds::rtps::UDPv4TransportDescriptor>();
        for (const auto& address: addresses) {
            udp->interface_allowlist.emplace_back(address);
        }
        qos.transport().use_builtin_transports = false;
        qos.transport().user_transports.push_back(udp);
        qos.wire_protocol().ignore_non_matching_locators = true;
    }

    auto *const participant = factory->create_participant(0, qos);
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
        std::string router(
            const ::eprosima::fastdds::dds::rpc::RpcRequest&,
            const std::string& handler_name, const std::string& args
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
                [this](const std::string num) {
                    return '"' + "Hi, here's ["s + this->name_ + "].  Got "s + num + "~" + '"';
                }
            )
        }
    };

    const Participant participant_;
    std::shared_ptr<::eprosima::fastdds::dds::rpc::RpcServer> server_;
    std::thread server_thread_;

    /*
     * Client-side 缓存.
     *
     * 出站调用不再按次创建/销毁 participant, 而是: 整个实例懒创建一个专用的
     * client-side participant (首个出站调用时才建, 纯 server 实例零开销), 并按
     * receiver name 缓存生成的 client.  每个 client 持有自己的 requester 和
     * 收发线程, 天然支持并发挂起多个请求, 因此同一 receiver 的所有调用共享
     * 一个 client 即可.  server 掉线重连由 DDS 发现自动重匹配, 缓存条目无需
     * 失效处理.
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
    std::map<std::string, std::shared_ptr<gen::Processor>> clients_;

    /*
     * 返回 receiver 对应的缓存 client, 缺失则创建并缓存.
     *
     * 创建放在锁内: 串行化并发的首次调用, 保证每个 receiver 至多建一个
     * client.  任何本地失败 (工厂/participant/client) 统一翻译成 LocalError.
     * 创建失败不留残余缓存条目, 下次调用会重试.
     */
    auto get_client(const std::string& receiver_name) -> std::shared_ptr<gen::Processor> {
        const auto lock = std::lock_guard<std::mutex>{this->clients_mutex_};

        const auto iter = this->clients_.find(receiver_name);
        if (iter != this->clients_.end()) {
            return iter->second;
        }

        std::shared_ptr<gen::Processor> client;
        try {
            if (!this->client_participant_) {
                this->client_participant_ = Participant{create_participant()};
            }
            client = gen::create_ProcessorClient(
                *this->client_participant_,
                receiver_name.c_str(),
                ::eprosima::fastdds::dds::RequesterQos{}
            );
        }
        catch (const ::eprosima::fastdds::dds::rpc::RpcException& e) {
            throw urpc2::LocalError{
                "Failed to set up Urpc2 client for instance \""s + receiver_name + "\" (" + e.what() + ')'};
        }
        catch (const std::exception& e) {
            throw urpc2::LocalError{
                "Failed to set up Urpc2 client for instance \""s + receiver_name + "\" (" + e.what() + ')'};
        }
        if (!client) {
            throw urpc2::LocalError{
                "Failed to create Urpc2 client for instance \""s + receiver_name + '"'};
        }

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
            throw std::invalid_argument{"Handler must be callable"};
        }
        {
            const auto lock = std::lock_guard<std::mutex>{this->handlers_mutex_};
            this->handlers_[handler_name] = std::make_shared<Handler>(std::move(handler));
        }
    }

    auto call(
        const std::string& receiver_name,
        const std::string& handler_name,
        const std::string& args,
        const std::chrono::duration<double> timeout
    ) -> std::string {
        namespace rpc = ::eprosima::fastdds::dds::rpc;

        // 取 (或懒创建) 该 receiver 的缓存 client; 本地失败在 get_client() 内
        // 已统一翻译为 LocalError.
        const auto client = this->get_client(receiver_name);

        // 不再盲目 sleep 等发现: client->router() 内部的 send_request 会先
        // wait_for_matching —— 等 requester 与 replier 双向匹配上, 一旦匹配就立即
        // 返回 (最多阻塞 Fast DDS 内部默认的 3 秒). 于是:
        //   - 目标存在:   匹配就绪即发请求, 省去此前无条件的 5s 固定延迟;
        //   - 目标不存在: 匹配等待到点, send_request 返回失败 -> router() 的 future
        //     立即携带 RpcBrokenPipeException, 下面 get() 将其翻译为 ServerNotFound (快速失败).
        auto future = client->router(handler_name, args);
        if (future.wait_for(timeout) != std::future_status::ready) {
            // 放弃这个 future 即可: client 是缓存的, 迟到的回复 (若有) 会由其
            // 收发线程投递到已被放弃的 promise 上, 随后条目被移除, 不会串扰
            // 之后的调用 (每次请求有独立的 sample identity).
            throw urpc2::Timeout{
                "Urpc2 call to \""s + receiver_name + "\"/\"" + handler_name + "\" timed out"};
        }

        // future.get() 会 rethrow 存进 promise 的 Fast DDS RpcException 子类. 在此
        // 边界把它们统一翻译成 urpc2 自己的、以 std::exception 为根的异常, 既不把
        // DDS 类型泄漏给调用方, 也避免非 std::exception 的 RpcException 逃逸导致
        // std::terminate. catch 顺序须由派生到基类 (Remote 专用码在 RpcRemoteException 前).
        try {
            return future.get();
        }
        catch (const rpc::RemoteUnknownOperationError& e) {
            throw urpc2::UnknownOperation{
                "No such handler \""s + handler_name + "\" on instance \"" + receiver_name + "\" (" + e.what() + ')'};
        }
        catch (const rpc::RpcBrokenPipeException& e) {
            // client 侧的 broken pipe 只源于「发请求时 requester 未匹配」, 即目标未被发现.
            throw urpc2::ServerNotFound{
                "No Urpc2 server discovered for instance \""s + receiver_name + "\" (" + e.what() + ')'};
        }
        catch (const rpc::RpcTimeoutException& e) {
            throw urpc2::Timeout{
                "Urpc2 call to \""s + receiver_name + "\"/\"" + handler_name + "\" timed out (" + e.what() + ')'};
        }
        catch (const rpc::RpcRemoteException& e) {
            // 其余远端错误 (invalid argument / unsupported / out of resources / unknown exception).
            throw urpc2::RemoteError{
                "Remote error from instance \""s + receiver_name + "\" (" + e.what() + ')'};
        }
        catch (const rpc::RpcException& e) {
            // 兜底: 任何未预料的 RpcException, 降级为根 Error, 保留消息, 绝不外泄或 terminate.
            throw urpc2::Error{
                "Urpc2 RPC error on instance \""s + receiver_name + "\" (" + e.what() + ')'};
        }
    }

    auto dispatch(const std::string& handler_name, const std::string& args) const -> std::string {
        const std::shared_ptr<Handler> handler = [&] {
            const auto lock = std::lock_guard<std::mutex>{this->handlers_mutex_};
            const auto iter = this->handlers_.find(handler_name);
            if (iter == this->handlers_.end()) {
                const auto message = "No such Urpc2 handler: "s + handler_name;
                throw ::eprosima::fastdds::dds::rpc::RemoteUnknownOperationError{message.c_str()};
            }
            return iter->second;
        }();
        return (*handler)(args);
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
    const std::string& args,
    const std::chrono::duration<double> timeout
) -> std::string {
    return this->impl_->call(receiver_name, handler_name, args, timeout);
}
