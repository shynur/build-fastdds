#include <urpc2.hpp>

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
#include <vector>

namespace {

/*
 * 打印单个网格节点进程接受的命令行.
 *
 * 进程名称就是 Urpc2 端点名称.  剩余的非选项参数是所有已知节点名称; 进程会
 * 过滤掉自己的名称.
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
 * 执行一次带有限重试的同步 RPC.
 *
 * 进入 Urpc2::call() 前会写入一行 CALL, 因此卡住的测试日志能指出正在测试的
 * 精确边.  成功响应必须回显接收方名称和原始载荷, 以证明是目标对等节点在
 * 应答, 而不是另一个不同名称的进程.
 */
bool call_peer(
        ::urpc2::Urpc2& rpc,
        const std::string& self_name,
        const std::string& peer_name,
        const int round,
        const int call_index)
{
    const auto payload = std::string{"from="} + self_name
            + ";to=" + peer_name
            + ";round=" + std::to_string(round)
            + ";call=" + std::to_string(call_index);
    const auto expected = peer_name + ":" + payload;

    for (int attempt = 1; attempt <= 4; ++attempt) {
        try {
            std::cout << "CALL " << self_name << " -> " << peer_name
                      << " round=" << round
                      << " call=" << call_index
                      << " attempt=" << attempt << '\n';
            const auto result = rpc.call(peer_name, "echo", payload, std::chrono::milliseconds{6000});
            if (result == expected) {
                std::cout << "OK " << self_name << " -> " << peer_name
                          << " round=" << round
                          << " call=" << call_index
                          << " attempt=" << attempt << '\n';
                return true;
            }

            std::cerr << "BAD " << self_name << " -> " << peer_name << ": expected '" << expected
                      << "', got '" << result << "'\n";
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
 * 第一版只需要 --rounds=N.  保持解析器小巧, 便于将示例复制到临时容器测试中.
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
 * 每个进程都会注册相同的 "echo" 处理器, 等待对等节点启动, 然后执行多轮
 * 单线程出站调用.  每轮都会打乱对等节点列表, 并在调用之间插入较小的随机
 * 间隔.  因此, 六个独立运行的进程无需在进程内启动多个客户端工作线程, 也能
 * 自然地产生交错且乱序的 RPC 流.
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

    auto rpc = ::urpc2::Urpc2{self_name};
    rpc.register_handler(
            "echo",
            [self_name](std::string args) {
                /*
                 * 添加确定性的处理器抖动, 使多进程负载下的回复不会完全按照
                 * 请求顺序返回.
                 */
                const auto delay_ms = 10 + (std::hash<std::string>{}(self_name + args) % 81);
                std::this_thread::sleep_for(std::chrono::milliseconds{static_cast<int>(delay_ms)});
                return self_name + ":" + args;
            });

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
            ok = call_peer(rpc, self_name, peer_name, round, call_index) && ok;
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
