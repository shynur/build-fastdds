#include <urpc2.hpp>

#include <cassert>
#include <iostream>
#include <string>

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
     * 处理器接收并返回不透明字符串.  此示例使用形似 JSON 的文本, 但避免为
     * 第一版框架增加 JSON 依赖.
     */
    alice.register_handler(
            "sub",
            [](std::string json_array) {
                if (json_array == "[5,3]") {
                    return std::string{"2"};
                }
                return std::string{"null"};
            });

    bob.register_handler(
            "add",
            [](std::string json_array) {
                if (json_array == "[5,3]") {
                    return std::string{"8"};
                }
                return std::string{"null"};
            });

    /*
     * 调用时先给出接收端点名称, 再给出接收端点本地的处理器名称.  由于端点
     * 名称唯一, 同一个处理器名称可以同时存在于两个端点上而不会产生歧义.
     */
    const auto sub_result = bob.call("alice", "sub", "[5,3]");
    const auto add_result = alice.call("bob", "add", "[5,3]");

    assert(sub_result == "2");
    assert(add_result == "8");

    std::cout << "bob -> alice.sub([5,3]) = " << sub_result << '\n';
    std::cout << "alice -> bob.add([5,3]) = " << add_result << '\n';
}
