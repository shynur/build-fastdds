#include <urpc2.hpp>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::string make_binary_payload()
{
    std::string s;
    s.push_back(static_cast<char>(0xFF));
    s.append("\x80binary\x00with-NUL\x00", 18);
    s.append("hello, world!\n", 14);
    s.append(std::string(200, '\x42'));
    return s;
}

auto echo_handler(std::string args) -> std::string
{
    return args;
}

}  // namespace

int main()
{
    auto server = ::urpc2::Urpc2{"binary_server"};
    auto client = ::urpc2::Urpc2{"binary_client"};

    server.register_handler("echo", echo_handler);

    const auto payload = make_binary_payload();
    const auto payload_len = payload.size();

    std::fprintf(stderr, ">> sending args_len=%zu\n", payload_len);

    const auto response = client.call(
        "binary_server",
        "echo",
        payload,
        std::chrono::milliseconds{5000}
    );

    // 1) 响应内容必须与原载荷逐字节相等.
    if (response.size() != payload_len) {
        std::fprintf(stderr, "FAIL: response.size()=%zu, expected %zu\n",
            response.size(), payload_len);
        return 1;
    }
    if (std::memcmp(response.data(), payload.data(), payload_len) != 0) {
        std::fprintf(stderr, "FAIL: response bytes differ from payload\n");
        return 1;
    }
    std::fprintf(stderr, ">> received response_len=%zu (byte-for-byte equal)\n",
        response.size());

    // 2) 验证载荷里有我们关心的特殊字节 (避免「载荷被意外截到第一个 NUL 之前」).
    bool saw_ff = false, saw_nul = false, saw_80 = false;
    for (const auto b : response) {
        if (static_cast<std::uint8_t>(b) == 0xFF) saw_ff = true;
        if (b == '\0') saw_nul = true;
        if (static_cast<std::uint8_t>(b) == 0x80) saw_80 = true;
    }
    if (!saw_ff || !saw_nul || !saw_80) {
        std::fprintf(stderr,
            "FAIL: payload did not survive round-trip (0xFF=%d NUL=%d 0x80=%d)\n",
            saw_ff, saw_nul, saw_80);
        return 1;
    }

    std::cout << "binary_passthrough OK (args_len=" << payload_len
              << ", response_len=" << response.size() << ")\n";
    return 0;
}
