#include <urpc2_rbk.hpp>

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <tuple>

int main(int argc, char **argv) {
    const auto instance_name = argv[1];

    urpc2_rbk::serve(
        instance_name, "add",
        std::function{[](int a, double b) {
            return a+b;
        }}
    );

    urpc2_rbk::serve(
        instance_name, "swap",
        std::function{[](int a, std::string b) {
            return std::tuple{b, a};
        }}
    );

    urpc2_rbk::serve(
        instance_name, "print",
        std::function{[&] {
            std::cout << "print: here's "s + instance_name + '\n' << std::flush;
        }}
    );

    // 一个总是抛异常的 handler: 供验证「远端执行期抛错」-> urpc2::RemoteError.
    urpc2_rbk::serve(
        instance_name, "boom",
        std::function{[] { throw std::runtime_error{"boom: intentional failure"}; }}
    );

    // ------------------------------------

    // 1. 打印提供的操作
    std::cout <<
        "实例 \""s + instance_name + "\" 提供以下操作:\n"
        "  add     <int> <double>   -> double\n"
        "  swap    <int> <string>   -> [string, int]\n"
        "  print                    -> (无返回值)\n"
        "  hi      <int>            -> string   (每个实例预置的测试 handler)\n"
        "  boom                     -> (远端 handler 抛异常, 测 RemoteError)\n"
        "  unknown                  -> (调不存在的 handler, 测 UnknownOperation)\n"
        "\n"
        "以 EOF (Ctrl-D) 结束.\n";

    // 2. loop:
    //    每个可调用 handler 的签名在编译期已知 (add/swap/print 是上面 serve 的,
    //    hi 是每个实例预置的), 因此按 handler name 分派到对应的类型化
    //    call<Ret, Args...>: 参数按声明类型读取, 返回值也被反序列化成对应类型.
    std::string handler_name;
    while (true) {
        //    1. 提示读取 handler name
        std::cout << "\nhandler name> ";
        if (!std::getline(std::cin, handler_name)) break;

        //    3. 提示读取 replier instance name
        //       (先读应答方, 再按 handler 决定要读几个参数)
        std::cout << "replier instance> ";
        std::string replier;
        if (!std::getline(std::cin, replier)) break;

        try {
            //    2. 提示读取 参数, 并 4. 打印结果 (若有)
            if (handler_name == "add") {
                std::cout << "args <int> <double>> ";
                int a;
                double b;
                std::cin >> a >> b;
                std::cin.ignore();  // 丢弃行尾换行, 供下轮 getline.
                const auto result =
                    urpc2_rbk::call<double, int, double>(replier, "add", a, b);
                std::cout << "result: " << result << '\n';
            }
            else if (handler_name == "swap") {
                std::cout << "args <int> <string>> ";
                int a;
                std::string b;
                std::cin >> a >> b;
                std::cin.ignore();
                const auto result =
                    urpc2_rbk::call<std::tuple<std::string, int>, int, std::string>(
                        replier, "swap", a, b);
                std::cout << "result: [\"" << std::get<0>(result) << "\", "
                          << std::get<1>(result) << "]\n";
            }
            else if (handler_name == "print") {
                // print 返回 void: 无参数, 也没有结果需要打印.
                urpc2_rbk::call<void>(replier, "print");
            }
            else if (handler_name == "hi") {
                // hi 是每个实例预置的原始 handler: 经 rbk 层调用时, 该 int 会被
                // 打包成 JSON array 文本作为请求体, handler 原样回显; 返回一个
                // 带引号的 JSON 字符串, 反序列化成 std::string.
                std::cout << "args <int>> ";
                int num;
                std::cin >> num;
                std::cin.ignore();
                const auto result =
                    urpc2_rbk::call<std::string, int>(replier, "hi", num);
                std::cout << "result: " << result << '\n';
            }
            else if (handler_name == "boom") {
                urpc2_rbk::call<void>(replier, "boom");            // -> RemoteError
            }
            else if (handler_name == "unknown") {
                urpc2_rbk::call<void>(replier, "no_such_handler"); // -> UnknownOperation
            }
            else {
                std::cerr << "未知 handler: " << handler_name << '\n';
            }
        }
        catch (const std::exception& e) {
            // urpc2 已把底层 Fast DDS 的 RpcException 都翻译成 std::exception 的子类
            // (ServerNotFound / Timeout / UnknownOperation / RemoteError / ...), 故一个
            // catch 即可兜住; e.what() 自带足够的定位信息.
            std::cerr << "error: " << e.what() << '\n';
        }
    }
}
