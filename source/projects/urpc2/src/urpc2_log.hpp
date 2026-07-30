#pragma once

/*
 * urpc2 内部日志设施, 不随 target 安装, 只供 src/ 下的实现文件包含.
 *
 * 抽出成 header 而非留在 urpc2.cpp, 是为了让 urpc2 与 urpc2_rbk 两个库共用同一套
 * 日志格式 (时间戳, [INFO]/[ERROR] 级别, 文件/行/函数, 以及各字段的配色), 避免
 * 复制一份格式化代码后两边慢慢分叉.
 *
 * 这里的函数都是 static 的, 因此每个包含它的 TU 各有一份副本; colors_enabled()
 * 的缓存也随之各自独立, 但探测结果 (NO_COLOR 与 isatty) 在进程内一致, 无影响.
 */

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

#include <cxxabi.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

namespace urpc2_detail {
    static constexpr auto ansi_reset = "\033[0m";
    static constexpr auto ansi_bold = "\033[1m";
    static constexpr auto ansi_dim = "\033[2m";
    static constexpr auto ansi_italic = "\033[3m";
    static constexpr auto ansi_underline = "\033[4m";
    static constexpr auto ansi_bold_red = "\033[1;31m";
    static constexpr auto ansi_bold_green = "\033[1;32m";
    static constexpr auto ansi_bold_cyan = "\033[1;36m";
    static constexpr auto ansi_green = "\033[32m";
    static constexpr auto ansi_yellow = "\033[33m";
    static constexpr auto ansi_magenta = "\033[35m";

    static auto colors_enabled() noexcept -> bool {
        static const auto enabled = [] {
            const auto *const no_color = std::getenv("NO_COLOR");
            return (no_color == nullptr || no_color[0] == '\0')
                && ::isatty(::fileno(stderr)) != 0;
        }();
        return enabled;
    }

    static auto style_code(const char *const code) noexcept -> const char * {
        return colors_enabled() ? code : "";
    }

    static auto styled(std::string text, const char *const code) -> std::string {
        if (!colors_enabled()) {
            return text;
        }

        auto result = std::string{code};
        result += text;
        result += ansi_reset;
        return result;
    }

    static auto action(std::string text) -> std::string {
        return styled(std::move(text), ansi_bold_cyan);
    }

    static auto entity(std::string text) -> std::string {
        return styled(std::move(text), ansi_underline);
    }

    static auto argument(std::string text) -> std::string {
        return styled(std::move(text), ansi_yellow);
    }

    static auto response(std::string text) -> std::string {
        return styled(std::move(text), ansi_green);
    }

    static auto value(std::string text) -> std::string {
        return styled(std::move(text), ansi_magenta);
    }

    static auto separator(std::string text) -> std::string {
        return styled(std::move(text), ansi_dim);
    }

    /*
     * 将 CBOR 二进制数据转换为 JSON 字符串用于日志显示.
     * 如果 CBOR 无法转换为合法 JSON, 返回占位符文本.
     */
    static auto cbor_to_json_for_logging(const std::vector<std::uint8_t>& cbor_data) -> std::string {
        try {
            const auto json_obj = ::nlohmann::json::from_cbor(cbor_data);
            return json_obj.dump();
        }
        catch (...) {
            return "<binary data, " + std::to_string(cbor_data.size()) + " bytes>";
        }
    }

    static auto cbor_to_json_for_logging(const std::string& cbor_str) -> std::string {
        return cbor_to_json_for_logging(
            std::vector<std::uint8_t>(cbor_str.begin(), cbor_str.end())
        );
    }

    static void format_timestamp(char (&timestamp)[32]) noexcept {
        const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        auto local_time = std::tm{};
        char date_time[20]{};
        char utc_offset[6]{};

        if (
            localtime_r(&now, &local_time) != nullptr
            && std::strftime(date_time, sizeof(date_time), "%Y-%m-%dT%H:%M:%S", &local_time) != 0
            && std::strftime(utc_offset, sizeof(utc_offset), "%z", &local_time) == 5
        ) {
            std::snprintf(
                timestamp,
                sizeof(timestamp),
                "%s%c%c%c:%c%c",
                date_time,
                utc_offset[0],
                utc_offset[1],
                utc_offset[2],
                utc_offset[3],
                utc_offset[4]
            );
        }
        else {
            std::snprintf(timestamp, sizeof(timestamp), "1970-01-01T00:00:00+00:00");
        }
    }

    /*
     * 取异常的动态类型名, 用于日志中的 {类名} 一栏.
     *
     * URPC2_THROW 能用 #exception_class 拿到字面量, 但 catch 到的异常只有运行期
     * 类型信息, typeid().name() 在 GCC/Clang 下是 mangled 的 (如 "St13runtime_error"),
     * 故用 __cxa_demangle 还原成源码里的写法.  demangle 失败时退回 mangled 名,
     * 日志的可读性可以降级, 但不能因此丢掉这条错误.
     */
    static auto exception_class_name(const std::exception& e) -> std::string {
        auto status = 0;
        auto *const demangled = ::abi::__cxa_demangle(typeid(e).name(), nullptr, nullptr, &status);
        if (demangled == nullptr) {
            return typeid(e).name();
        }

        auto name = std::string{demangled};
        std::free(demangled);
        return name;
    }

    static void log_info(
        const char *const file,
        const int line,
        const char *const function,
        const std::string& message
    ) noexcept {
        char timestamp[32]{};
        format_timestamp(timestamp);

        const auto *const reset = style_code(ansi_reset);
        const auto *const bold = style_code(ansi_bold);
        const auto *const dim = style_code(ansi_dim);
        const auto *const italic = style_code(ansi_italic);
        const auto *const info = style_code(ansi_bold_green);

        std::fprintf(
            stderr,
            "%s%.11s%s%s%.8s%s%s%s%s %s[INFO]%s urpc2 - "
            "%s%s: line %d:%s %s%s%s: %s%s\n",
            dim,
            timestamp,
            reset,
            bold,
            timestamp + 11,
            reset,
            dim,
            timestamp + 19,
            reset,
            info,
            reset,
            dim,
            file,
            line,
            reset,
            italic,
            function,
            reset,
            message.c_str(),
            reset
        );
        std::fflush(stderr);
    }

    static void log_exception(
        const char *const file,
        const int line,
        const char *const function,
        const char *const exception_class,
        const std::string& message
    ) noexcept {
        char timestamp[32]{};
        format_timestamp(timestamp);

        const auto *const reset = style_code(ansi_reset);
        const auto *const bold = style_code(ansi_bold);
        const auto *const dim = style_code(ansi_dim);
        const auto *const italic = style_code(ansi_italic);
        const auto *const error = style_code(ansi_bold_red);

        std::fprintf(
            stderr,
            "%s%.11s%s%s%.8s%s%s%s%s %s[ERROR]%s urpc2 - "
            "%s%s: line %d:%s %s%s%s: %s%s%s{%s%s%s}\n",
            dim,
            timestamp,
            reset,
            bold,
            timestamp + 11,
            reset,
            dim,
            timestamp + 19,
            reset,
            error,
            reset,
            dim,
            file,
            line,
            reset,
            italic,
            function,
            reset,
            error,
            exception_class,
            reset,
            error,
            message.c_str(),
            reset
        );
        std::fflush(stderr);
    }

}

#define URPC2_LOG_INFO(message) \
    do { \
        const auto urpc2_log_message = std::string{message}; \
        ::urpc2_detail::log_info( \
            __FILE__, __LINE__, __PRETTY_FUNCTION__, urpc2_log_message); \
    } while (false)

#define URPC2_THROW(exception_class, message) \
    do { \
        auto urpc2_exception_message = std::string{message}; \
        ::urpc2_detail::log_exception( \
            __FILE__, __LINE__, __PRETTY_FUNCTION__, #exception_class, urpc2_exception_message); \
        throw exception_class{std::move(urpc2_exception_message)}; \
    } while (false)

#define URPC2_THROW_C_STRING(exception_class, message) \
    do { \
        const auto urpc2_exception_message = std::string{message}; \
        ::urpc2_detail::log_exception( \
            __FILE__, __LINE__, __PRETTY_FUNCTION__, #exception_class, urpc2_exception_message); \
        throw exception_class{urpc2_exception_message.c_str()}; \
    } while (false)

#define URPC2_LOG_ERROR(exception_class, message) \
    do { \
        const auto urpc2_error_message = std::string{message}; \
        ::urpc2_detail::log_exception( \
            __FILE__, __LINE__, __PRETTY_FUNCTION__, exception_class, urpc2_error_message); \
    } while (false)
