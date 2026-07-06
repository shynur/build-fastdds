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

std::string usage(const char* const program)
{
    return std::string{"usage: "} + program + " <self-name> <peer-name>... [--rounds=N]";
}

unsigned seed_for(const std::string& text)
{
    return static_cast<unsigned>(std::hash<std::string>{}(text));
}

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
        auto round_peers = peers;
        std::shuffle(round_peers.begin(), round_peers.end(), rng);
        std::cout << "ROUND " << self_name << " round=" << round << '\n';

        for (const auto& peer_name : round_peers) {
            ++call_index;
            ok = call_peer(rpc, self_name, peer_name, round, call_index) && ok;
            std::this_thread::sleep_for(std::chrono::milliseconds{gap(rng)});
        }
    }

    std::cout << (ok ? "DONE " : "FAILED ") << self_name << '\n';
    std::this_thread::sleep_for(std::chrono::seconds{20});
    return ok ? 0 : 1;
}

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
