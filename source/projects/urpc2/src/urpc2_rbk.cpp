#include "urpc2_rbk.hpp"

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

#include <unistd.h>  // getpid()

#include <urpc2.hpp>  // 同时引入全局的 using namespace std::literals (供 "..."s 使用)

#include "urpc2_log.hpp"

namespace urpc2_rbk { namespace detail { namespace {

/*
 * rbk 隐藏了 urpc2::Urpc2::call() 所要求的 timeout, 对外只暴露无 timeout 的
 * call().  这里用一个宽松的默认值: urpc2 的调用路径本身已经为发现预留了数秒,
 * 因此该 timeout 只覆盖握手之后等待回复的时间.
 */
constexpr auto default_call_timeout = std::chrono::milliseconds{30000};

/*
 * 进程级实例注册表.
 *
 * serve() 按名字懒创建并缓存 urpc2::Urpc2 实例, 同名实例复用同一对象.
 *
 * call() 需要一个 urpc2::Urpc2 对象作为"载体"来触发其成员函数 call(); 该成员
 * 函数内部使用 (并缓存) 载体自己的 client-side participant 与 client, 与载体
 * 的 server-side participant 无关.  因此任意一个实例都能充当载体: 优先复用某个
 * 已 serve 的实例以免多起一个空转 server, 纯调用方进程 (从不 serve) 才懒创建
 * 一个专用 client 实例.  该 client 的名字带上 PID, 以免与其它进程的 DDS 服务名
 * 冲突.  注意载体应保持稳定: 换载体调用同一 receiver 会在新载体上再建一套
 * client 缓存.
 */
struct Registry {
    std::mutex mutex;
    std::map<std::string, std::unique_ptr<::urpc2::Urpc2>> instances;
    std::unique_ptr<::urpc2::Urpc2> client;
};

auto registry() -> Registry& {
    /*
     * 故意泄漏, 永不析构.
     *
     * 若用普通的函数内 static, 它会在首个 Urpc2 之前完成构造, 而 Fast DDS 的
     * 工厂单例要到首个 Urpc2 构造时才懒创建 (更晚).  进程退出时 static 按构造
     * 逆序析构, 于是工厂先于本注册表被销毁; 随后 ~Registry 析构缓存的 Urpc2 ->
     * ~Impl -> delete_participant 会去访问已销毁的工厂 -> UB.  这些实例本就存活
     * 至进程结束, 因此改为永不析构的堆对象, 交由 OS 在退出时统一回收.
     */
    static Registry *const the_registry = new Registry{};
    return *the_registry;
}

// 返回已缓存的实例, 不存在则懒创建.  调用方须持有 registry().mutex.
//
// Urpc2 的构造较慢 (建 DDS participant/server, 且 sleep 约 1s), 却仍刻意放在锁
// 内: 这样每个名字至多构造一个实例, 避免竞态下瞬时出现两个同名 DDS server.
// 代价是并发的首次建实例会被串行化, 属可接受的一次性开销.
auto get_or_create_instance_locked(const std::string& name) -> ::urpc2::Urpc2& {
    auto& reg = registry();
    auto iter = reg.instances.find(name);
    if (iter == reg.instances.end()) {
        iter = reg.instances.emplace(name, std::make_unique<::urpc2::Urpc2>(name)).first;
    }
    return *iter->second;
}

}  // namespace

void serve_handler(
    const std::string& instance_name,
    const std::string& handler_name,
    ::urpc2::Urpc2::Handler handler
) {
    /*
     * 在锁内取得 (或创建) 实例指针; 实例一旦缓存便不会被移除, 所以释放锁后指针
     * 仍然有效.  真正的注册交给实例自己的 register_handler() (它有独立的锁),
     * 无需在注册期间继续持有注册表锁.
     */
    /*
     * 提前自查可调用性.  下面注册进去的是恒为真的 lambda, 底层 register_handler()
     * 的同名检查再也不会触发, 而不可调用的 handler 若一路放过去, 要等到真有请求
     * 打过来才在 std::function 里炸成 bad_function_call.  故在此保留该检查, 维持
     * "注册即报错" 的行为.
     */
    if (!handler) {
        URPC2_THROW(std::invalid_argument, "Handler must be callable");
    }

    ::urpc2::Urpc2* instance = nullptr;
    {
        const auto lock = std::lock_guard<std::mutex>{registry().mutex};
        instance = &get_or_create_instance_locked(instance_name);
    }

    /*
     * 包一层错误处理: 处理器体内抛出的异常在这里落一条 [ERROR] 日志, 然后原样
     * 重抛.
     *
     * 日志是纯增量的, 不改变错误语义: 重抛后异常仍会走到生成的 server 代码, 由它
     * 翻译成 REMOTE_EX_* 回给 client (调用方那侧照旧收到 urpc2::RemoteError).  若在
     * 此吞掉异常, 就得凭空编一个返回值, client 会把失败当成功.
     *
     * 之所以要在服务端留这条日志: 回给 client 的远端异常只带一个错误码, 处理器自
     * 己的异常类型和 what() 到不了对端, 排查时服务端日志是唯一线索.  catch (...)
     * 的分支覆盖非 std::exception 的抛出物 (例如生成代码用的 RpcException), 它们
     * 同样会静默变成一个错误码.
     */
    instance->register_handler(
        handler_name,
        [instance_name, handler_name, handler = std::move(handler)](std::string args) -> std::string {
            try {
                return handler(std::move(args));
            }
            catch (const std::exception& e) {
                URPC2_LOG_ERROR(
                    ::urpc2_detail::exception_class_name(e).c_str(),
                    "Handler \""s
                    + ::urpc2_detail::entity(instance_name + '.' + handler_name)
                    + "\" failed: " + e.what()
                );
                throw;
            }
            catch (...) {
                URPC2_LOG_ERROR(
                    "unknown exception",
                    "Handler \""s
                    + ::urpc2_detail::entity(instance_name + '.' + handler_name)
                    + "\" failed"
                );
                throw;
            }
        }
    );
}

auto call_raw(
    const std::string& instance_name,
    const std::string& handler_name,
    const std::string& args_json
) -> std::string {
    ::urpc2::Urpc2* vehicle = nullptr;
    {
        auto& reg = registry();
        const auto lock = std::lock_guard<std::mutex>{reg.mutex};
        if (!reg.instances.empty()) {
            vehicle = reg.instances.begin()->second.get();
        } else {
            if (!reg.client) {
                reg.client = std::make_unique<::urpc2::Urpc2>(
                    "urpc2_rbk_client_" + std::to_string(::getpid()));
            }
            vehicle = reg.client.get();
        }
    }
    return vehicle->call(instance_name, handler_name, args_json, default_call_timeout);
}

}}  // namespace urpc2_rbk::detail
