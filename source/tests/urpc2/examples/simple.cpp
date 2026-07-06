#include <urpc2.hpp>

#include <cassert>
#include <iostream>
#include <string>

int main()
{
    /*
     * The smoke test keeps both endpoints in one process so the public API can
     * be read without Docker or process orchestration. Each endpoint still owns
     * an independent DDS RPC service named by the Urpc2 constructor argument.
     */
    auto alice = ::urpc2::Urpc2{"alice"};
    auto bob = ::urpc2::Urpc2{"bob"};

    /*
     * Handlers receive and return opaque strings. This example uses JSON-shaped
     * text but avoids adding a JSON dependency to the first-version framework.
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
     * Calls name the receiver endpoint first and the receiver-local handler
     * second. The same handler name could exist on both endpoints without
     * ambiguity because endpoint names are unique.
     */
    const auto sub_result = bob.call("alice", "sub", "[5,3]");
    const auto add_result = alice.call("bob", "add", "[5,3]");

    assert(sub_result == "2");
    assert(add_result == "8");

    std::cout << "bob -> alice.sub([5,3]) = " << sub_result << '\n';
    std::cout << "alice -> bob.add([5,3]) = " << add_result << '\n';
}
