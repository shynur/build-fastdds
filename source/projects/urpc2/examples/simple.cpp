#include <urpc2.hpp>

#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <vector>

int main()
{
    /*
     * 冒烟测试将两个端点放在同一个进程中, 这样无需 Docker 或进程编排也能
     * 阅读公共 API.  每个端点仍然拥有一个独立的 DDS RPC 服务, 服务名由
     * Urpc2 构造函数参数指定.
     */
    auto alice = ::urpc2::Urpc2{"alice"};
    auto bob = ::urpc2::Urpc2{"bob"};

    /*
     * 处理器接收并返回二进制数据 (std::vector<std::uint8_t>).  此示例使用形似
     * JSON 的文本, 但在 string 和 vector 之间转换以匹配新的 API.
     */
    alice.register_handler(
            "sub",
            [](std::vector<std::uint8_t> data) -> std::vector<std::uint8_t> {
                const auto json_array = std::string(data.begin(), data.end());
                std::string result;
                if (json_array == "[5,3]") {
                    result = "2";
                } else {
                    result = "null";
                }
                return std::vector<std::uint8_t>(result.begin(), result.end());
            });

    bob.register_handler(
            "add",
            [](std::vector<std::uint8_t> data) -> std::vector<std::uint8_t> {
                const auto json_array = std::string(data.begin(), data.end());
                std::string result;
                if (json_array == "[5,3]") {
                    result = "8";
                } else {
                    result = "null";
                }
                return std::vector<std::uint8_t>(result.begin(), result.end());
            });

    /*
     * 调用时先给出接收端点名称, 再给出接收端点本地的处理器名称.  由于端点
     * 名称唯一, 同一个处理器名称可以同时存在于两个端点上而不会产生歧义.
     */
    const auto args = std::string{"[5,3]"};
    const auto args_vec = std::vector<std::uint8_t>(args.begin(), args.end());

    const auto sub_result_vec = bob.call("alice", "sub", args_vec, std::chrono::milliseconds{5000});
    const auto add_result_vec = alice.call("bob", "add", args_vec, std::chrono::milliseconds{5000});

    const auto sub_result = std::string(sub_result_vec.begin(), sub_result_vec.end());
    const auto add_result = std::string(add_result_vec.begin(), add_result_vec.end());

    assert(sub_result == "2");
    assert(add_result == "8");

    std::cout << "bob -> alice.sub([5,3]) = " << sub_result << '\n';
    std::cout << "alice -> bob.add([5,3]) = " << add_result << '\n';
}
