#pragma once

#include <cstdint>
#include <limits>

#include <fastdds/dds/rpc/interfaces/RpcRequest.hpp>

#include "types/calculator.hpp"
#include "types/calculatorServer.hpp"
#include "types/calculatorServerImpl.hpp"

// 继承生成的默认实现 (其默认对每个操作抛 RemoteUnsupportedError), 覆盖我们支持的操作.
struct CalculatorServerImpl: ::calculator_example::CalculatorServerImplementation {
    ::calculator_example::detail::Calculator_representation_limits_Out representation_limits(
        const ::eprosima::fastdds::dds::rpc::RpcRequest& info
    ) override {
        (void)info;
        auto limits = ::calculator_example::detail::Calculator_representation_limits_Out{};
        limits.min_value = std::numeric_limits<std::int32_t>::min();
        limits.max_value = std::numeric_limits<std::int32_t>::max();
        return limits;
    }
    std::int32_t addition(
        const ::eprosima::fastdds::dds::rpc::RpcRequest& info,
        const std::int32_t value1, const std::int32_t value2
    ) override {
        (void)info;
        const std::int32_t result = value1 + value2;
        const bool negative_1 = value1 < 0;
        const bool negative_2 = value2 < 0;
        const bool negative_result = result < 0;
        // 两个同号数相加, 结果却异号 => 溢出
        if ((negative_1 == negative_2) && (negative_result != negative_1)) {
            throw ::calculator_example::OverflowException{};
        }
        return result;
    }
};
