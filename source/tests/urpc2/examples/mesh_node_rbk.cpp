#include <urpc2_rbk.hpp>

#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace {

/*
 * 打印单个网格节点进程接受的命令行.
 *
 * 进程名称就是 urpc2_rbk 实例名称.  剩余的非选项参数是所有已知节点名称; 进程
 * 会过滤掉自己的名称.
 */
std::string usage(const char* const program)
{
    return std::string{"usage: "} + program + " <self-name> <peer-name>... [--rounds=N]";
}

/*
 * 构建确定性的伪随机种子.
 *
 * 测试需要打乱调用顺序, 但不能让失败变得不可复现.  使用节点名称作为种子,
 * 可以让每个节点获得不同但稳定的对等节点顺序序列.
 */
unsigned seed_for(const std::string& text)
{
    return static_cast<unsigned>(std::hash<std::string>{}(text));
}

/*
 * 处理器抖动.
 *
 * 添加确定性延迟, 使多进程负载下的回复不会完全按照请求顺序返回.
 */
void jitter(const std::string& key)
{
    const auto delay_ms = 10 + (std::hash<std::string>{}(key) % 81);
    std::this_thread::sleep_for(std::chrono::milliseconds{static_cast<int>(delay_ms)});
}

/*
 * 与 mesh_node.cpp 相同的每边校验字符串, 便于人工核对两套测试的日志.
 */
std::string digest_of(
        const std::string& from,
        const std::string& to,
        const int round,
        const int call_index)
{
    return from + "->" + to
            + "#" + std::to_string(round)
            + "." + std::to_string(call_index);
}

/*
 * echo 处理器的类型化返回值.
 *
 * 特意选用一个混合类型的 tuple, 让每次网格调用都完整走一遍 urpc2_rbk 的多参
 * 数解包和多返回值打包路径: 应答方回显自己的名称, 原样返回收到的 round 与
 * call 序号 (证明整型参数未被错位或截断), 并附上按参数重算的 digest (证明字符
 * 串参数按正确顺序抵达).
 */
using EchoReply = std::tuple<std::string, int, int, std::string>;

/*
 * 执行一次带有限重试的类型化 RPC.
 *
 * 进入 urpc2_rbk::call() 前会写入一行 CALL, 因此卡住的测试日志能指出正在测试
 * 的精确边.  成功响应必须在应答方名称, round, call 序号和 digest 四处都与预期
 * 一致, 才算通过: 这样既证明是目标对等节点在应答, 也证明类型化参数与返回值在
 * 真实并发下没有被 marshalling 弄错.
 */
bool call_peer(
        const std::string& self_name,
        const std::string& peer_name,
        const int round,
        const int call_index)
{
    const auto expected_digest = digest_of(self_name, peer_name, round, call_index);

    for (int attempt = 1; attempt <= 4; ++attempt) {
        try {
            std::cout << "CALL " << self_name << " -> " << peer_name
                      << " round=" << round
                      << " call=" << call_index
                      << " attempt=" << attempt << '\n';
            const auto reply = ::urpc2_rbk::call<EchoReply, std::string, std::string, int, int>(
                    peer_name, "echo", self_name, peer_name, round, call_index);
            const auto& responder = std::get<0>(reply);
            const auto round_echo = std::get<1>(reply);
            const auto call_echo = std::get<2>(reply);
            const auto& digest = std::get<3>(reply);

            if (responder == peer_name
                    && round_echo == round
                    && call_echo == call_index
                    && digest == expected_digest) {
                std::cout << "OK " << self_name << " -> " << peer_name
                          << " round=" << round
                          << " call=" << call_index
                          << " attempt=" << attempt << '\n';
                return true;
            }

            std::cerr << "BAD " << self_name << " -> " << peer_name
                      << ": expected responder=" << peer_name
                      << " round=" << round
                      << " call=" << call_index
                      << " digest='" << expected_digest << "'"
                      << ", got responder=" << responder
                      << " round=" << round_echo
                      << " call=" << call_echo
                      << " digest='" << digest << "'\n";
        }
        catch (const std::exception& e) {
            std::cerr << "RETRY " << self_name << " -> " << peer_name
                      << " attempt=" << attempt << " error=" << e.what() << '\n';
        }
        catch (...) {
            std::cerr << "RETRY " << self_name << " -> " << peer_name
                      << " attempt=" << attempt << " error=unknown exception\n";
        }

        std::this_thread::sleep_for(std::chrono::seconds{1});
    }

    std::cerr << "FAIL " << self_name << " -> " << peer_name
              << " round=" << round
              << " call=" << call_index << '\n';
    return false;
}

/*
 * 解析网格选项.
 *
 * 只需要 --rounds=N.  保持解析器小巧, 便于将示例复制到临时容器测试中.
 */
int parse_rounds(int argc, char** argv)
{
    auto rounds = 8;
    for (int i = 2; i < argc; ++i) {
        const auto arg = std::string{argv[i]};
        const auto prefix = std::string{"--rounds="};
        if (arg.compare(0, prefix.size(), prefix) == 0) {
            rounds = std::stoi(arg.substr(prefix.size()));
        }
    }

    if (rounds < 1) {
        throw std::invalid_argument{"rounds must be positive"};
    }

    return rounds;
}

}  // namespace

/*
 * 运行一个网格节点.
 *
 * 与 mesh_node.cpp 结构一致, 只是端点改用 urpc2_rbk 的类型化 API: 每个进程都
 * 用 serve() 注册相同的 "echo" 处理器 (普通函数, 参数与返回值都是常规类型),
 * 等待对等节点启动, 然后执行多轮单线程出站调用.  每轮都会打乱对等节点列表,
 * 并在调用之间插入较小的随机间隔.  因此, 六个独立运行的进程无需在进程内启动
 * 多个客户端工作线程, 也能自然地产生交错且乱序的 RPC 流.
 */
int run(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << usage(argv[0]) << '\n';
        return 2;
    }

    const auto self_name = std::string{argv[1]};
    const auto rounds = parse_rounds(argc, argv);
    auto peers = std::vector<std::string>{};
    for (int i = 2; i < argc; ++i) {
        const auto peer_name = std::string{argv[i]};
        if (peer_name.compare(0, 2, "--") == 0) {
            continue;
        }
        if (peer_name != self_name) {
            peers.push_back(peer_name);
        }
    }

    /*
     * echo 处理器写成普通函数: 收到发起方名称, 目标名称, round 与 call 序号,
     * 返回一个混合类型 tuple.  urpc2_rbk 负责把 client 传来的 JSON array 解包成
     * 这四个参数, 再把 tuple 序列化回 JSON.
     */
    ::urpc2_rbk::serve(
            self_name,
            "echo",
            std::function{
                [self_name](std::string from, std::string to, int round, int call) -> EchoReply {
                    jitter(self_name + from + std::to_string(round) + std::to_string(call));
                    return EchoReply{self_name, round, call, digest_of(from, to, round, call)};
                }});

    std::cout << "READY " << self_name << " peers=" << peers.size()
              << " rounds=" << rounds << '\n';
    std::this_thread::sleep_for(std::chrono::seconds{3});

    auto ok = true;
    auto rng = std::mt19937{seed_for(self_name)};
    auto gap = std::uniform_int_distribution<int>{120, 420};
    auto call_index = 0;
    for (int round = 1; round <= rounds; ++round) {
        /*
         * 每轮打乱一次, 然后顺序调用对等节点.  每个进程内的序列仍然是单线程
         * 的, 而所有容器会并发运行各自的序列.
         */
        auto round_peers = peers;
        std::shuffle(round_peers.begin(), round_peers.end(), rng);
        std::cout << "ROUND " << self_name << " round=" << round << '\n';

        for (const auto& peer_name : round_peers) {
            ++call_index;
            ok = call_peer(self_name, peer_name, round, call_index) && ok;
            std::this_thread::sleep_for(std::chrono::milliseconds{gap(rng)});
        }
    }

    /*
     * 完成出站调用后继续短暂提供服务.  这可以防止较快节点在较慢对等节点的
     * 当前测试运行中仍有最后一个请求发往它时提前消失.
     */
    std::cout << (ok ? "DONE " : "FAILED ") << self_name << '\n';
    std::this_thread::sleep_for(std::chrono::seconds{20});
    return ok ? 0 : 1;
}

/*
 * 为测试进程安装日志保护.
 *
 * 单元缓冲流让进程即使被中断, Docker 日志文件也仍然有用.  terminate 处理器会
 * 在退出前记录从后台 Fast DDS 线程或析构函数逃逸的异常.
 */
int main(int argc, char** argv)
{
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    std::set_terminate(
            []() {
                const auto exception = std::current_exception();
                if (exception) {
                    try {
                        std::rethrow_exception(exception);
                    }
                    catch (const std::exception& e) {
                        std::cerr << "TERMINATE " << e.what() << '\n';
                    }
                    catch (...) {
                        std::cerr << "TERMINATE unknown exception\n";
                    }
                }
                else {
                    std::cerr << "TERMINATE without active exception\n";
                }
                std::_Exit(70);
            });

    try {
        return run(argc, argv);
    }
    catch (const std::exception& e) {
        std::cerr << "ERROR " << e.what() << '\n';
    }
    catch (...) {
        std::cerr << "ERROR unknown exception\n";
    }

    return 1;
}
