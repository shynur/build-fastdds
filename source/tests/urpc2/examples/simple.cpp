#include <urpc2.hpp>

#include <cassert>
#include <iostream>
#include <string>

int main()
{
    auto alice = ::urpc2::Urpc2{"alice"};
    auto bob = ::urpc2::Urpc2{"bob"};

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

    const auto sub_result = bob.call("alice", "sub", "[5,3]");
    const auto add_result = alice.call("bob", "add", "[5,3]");

    assert(sub_result == "2");
    assert(add_result == "8");

    std::cout << "bob -> alice.sub([5,3]) = " << sub_result << '\n';
    std::cout << "alice -> bob.add([5,3]) = " << add_result << '\n';
}
