// urpc2_rbk_unit.cpp
//
// Header-only unit tests for source/projects/urpc2/include/urpc2_rbk.hpp
// "Plan A" (CBOR) helpers.  No Fast DDS dependency: we exercise only
// detail::{cbor_encode, cbor_decode, encode_arg, decode_arg,
// encode_return, decode_return, invoke_from_cbor} plus the encode_arg /
// decode_arg round-tripping that the public call() / serve() rely on.
//
// Build:
//   g++ -std=c++17 -O0 -Wall -Wextra
//       -I source/projects/urpc2/include
//       source/projects/urpc2/tests/urpc2_rbk_unit.cpp
//       -o /tmp/urpc2_rbk_unit
//   /tmp/urpc2_rbk_unit
//
// Returns 0 on full pass, 1 on any failure.  No <cassert>.
// nlohmann/json 3.12 is bundled under include/.

#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <random>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>
#include <urpc2_rbk.hpp>

namespace {

struct Case {
    const char* name;
    bool passed;
    std::string detail;
};

auto report(std::vector<Case>& sink, const char* name, bool ok, std::string msg)
    -> void
{
    sink.push_back(Case{name, ok, std::move(msg)});
}

auto expect_eq_bytes(
    const std::string& got,
    const std::string& want,
    std::string& msg
) -> bool
{
    if (got.size() != want.size()) {
        std::ostringstream o;
        o << "size mismatch got=" << got.size() << " want=" << want.size();
        msg = o.str();
        return false;
    }
    if (std::memcmp(got.data(), want.data(), want.size()) != 0) {
        msg = "byte-by-byte mismatch";
        return false;
    }
    msg = "size=" + std::to_string(got.size()) + " bytes match";
    return true;
}

// Build std::string that contains a lone 0xA4 continuation byte (illegal
// at byte-0) followed by printable ASCII, to prove that no UTF-8 check
// is performed on the binary path.
auto make_isolated_continuation() -> std::string {
    std::string s;
    s.push_back(static_cast<char>(0xA4));     // isolated 10xxxxxx byte
    s += "PNG_HEAD";
    s.push_back(static_cast<char>(0xA4));     // another isolated continuation
    return s;
}

// Build std::string with embedded NULs.
auto make_embedded_nul() -> std::string {
    std::string s = "AAA";
    s.push_back('\0');
    s += "BBB";
    s.push_back('\0');
    s += std::string(16, 'X');
    return s;
}

// Build std::string starting with 0xFF (illegal UTF-8 lead byte).
auto make_ff_lead() -> std::string {
    std::string s;
    s.push_back(static_cast<char>(0xFF));
    s.push_back(static_cast<char>(0xFE));
    s += "signature";
    return s;
}

// Random byte generator (deterministic seed for reproducibility).
inline auto random_bytes(std::size_t n) -> std::string
{
    auto rng = std::mt19937{0xC0FFEEu};
    auto dist = std::uniform_int_distribution<int>{0, 255};
    auto out = std::string(n, '\0');
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = static_cast<char>(dist(rng));
    }
    return out;
}

// Generic round-trip helper: std::string -> encode_arg -> cbor -> decode_arg.
inline auto roundtrip_string(const std::string& original, std::string& msg)
    -> bool
{
    const auto j = nlohmann::json::array(
        {urpc2_rbk::detail::encode_arg(original)});
    const auto wire = urpc2_rbk::detail::cbor_encode(j);
    const auto back = urpc2_rbk::detail::cbor_decode(wire);
    const auto decoded =
        urpc2_rbk::detail::decode_arg<std::string>(back.at(0));
    return expect_eq_bytes(decoded, original, msg);
}

inline void record(std::vector<Case>& sink, const char* name,
                   bool ok, std::string m, int& failures) {
    report(sink, name, ok, std::move(m));
    if (!ok) ++failures;
}
}  // namespace

int main()
{
    auto cases = std::vector<Case>{};
    int failures = 0;

    // -----------------------------------------------------------------
    // Case 1: std::string with isolated 0xA4 continuation byte round-trips
    //         through encode_arg(cbor) -> cbor_decode(decode_arg).
    // -----------------------------------------------------------------
    {
        const std::string original = make_isolated_continuation();
        const auto j_arr = nlohmann::json::array({urpc2_rbk::detail::encode_arg(original)});
        const auto wire = urpc2_rbk::detail::cbor_encode(j_arr);
        const auto back = urpc2_rbk::detail::cbor_decode(wire);
        const auto decoded = urpc2_rbk::detail::decode_arg<std::string>(
            back.at(0)
        );
        std::string msg;
        const bool ok = expect_eq_bytes(decoded, original, msg);
        report(cases, "std::string with isolated 0xA4 round-trips",
               ok, msg + " (input has bytes >0x7F; if \"UTF-8 validated\", this would throw)");
        if (!ok) ++failures;
    }

    // -----------------------------------------------------------------
    // Case 2: std::string with embedded NULs round-trips (the exact case
    //         that json::parse on a JSON string used to drop on the floor).
    // -----------------------------------------------------------------
    {
        const std::string original = make_embedded_nul();
        const auto j_arr = nlohmann::json::array({urpc2_rbk::detail::encode_arg(original)});
        const auto wire = urpc2_rbk::detail::cbor_encode(j_arr);
        const auto back = urpc2_rbk::detail::cbor_decode(wire);
        const auto decoded = urpc2_rbk::detail::decode_arg<std::string>(
            back.at(0)
        );
        // Truncation-by-NUL is the bug we're guarding against: the decoded
        // string MUST be the same size and MUST have NULs at the same
        // offsets as the original.
        std::string msg;
        bool ok = expect_eq_bytes(decoded, original, msg);
        if (ok) {
            msg += "; NUL offsets preserved (3, 7)";
        }
        report(cases, "std::string with embedded NULs round-trips full length",
               ok, msg);
        if (!ok) ++failures;
    }

    // -----------------------------------------------------------------
    // Case 3: std::string starting with 0xFF / 0xFE (illegal UTF-8 lead
    //         bytes) round-trips without being sanitized.
    // -----------------------------------------------------------------
    {
        const std::string original = make_ff_lead();
        const auto j_arr = nlohmann::json::array({urpc2_rbk::detail::encode_arg(original)});
        const auto wire = urpc2_rbk::detail::cbor_encode(j_arr);
        const auto back = urpc2_rbk::detail::cbor_decode(wire);
        const auto decoded = urpc2_rbk::detail::decode_arg<std::string>(
            back.at(0)
        );
        std::string msg;
        const bool ok = expect_eq_bytes(decoded, original, msg);
        report(cases, "std::string with 0xFF/0xFE lead bytes round-trips",
               ok, msg);
        if (!ok) ++failures;
    }

    // -----------------------------------------------------------------
    // Case 4: mixed-type wire (int + binary string + vector<int> + double).
    //         Confirms that ONLY std::string takes the bytes path; other
    //         types still flow through nlohmann's native CBOR encoding.
    // -----------------------------------------------------------------
    {
        const std::string blob("\x00\xFF\x00middle\x00", 11);
        const int  i_arg = -42;
        const std::vector<int> vec_arg{1, 2, 3, 4};
        const double d_arg = 3.14159;

        const auto j_arr = nlohmann::json::array({
            urpc2_rbk::detail::encode_arg(i_arg),
            urpc2_rbk::detail::encode_arg(std::move(const_cast<std::string&>(blob))),
            urpc2_rbk::detail::encode_arg(vec_arg),
            urpc2_rbk::detail::encode_arg(d_arg),
        });
        const auto wire = urpc2_rbk::detail::cbor_encode(j_arr);
        const auto back = urpc2_rbk::detail::cbor_decode(wire);

        const auto i_back = urpc2_rbk::detail::decode_arg<int>(back.at(0));
        const auto s_back = urpc2_rbk::detail::decode_arg<std::string>(back.at(1));
        const auto v_back = urpc2_rbk::detail::decode_arg<std::vector<int>>(back.at(2));
        const auto d_back = urpc2_rbk::detail::decode_arg<double>(back.at(3));

        bool ok = (i_back == i_arg)
               && (v_back == vec_arg)
               && (d_back == d_arg);
        std::string msg;
        if (ok) {
            std::ostringstream o;
            o << "int=" << i_back << " str.size=" << s_back.size()
              << " vec.size=" << v_back.size() << " double=" << d_back;
            msg = o.str();
        } else {
            msg = "type route mismatch (the int/vector/double slots "
                  "must NOT have been promoted to bytes)";
        }
        // Plus the byte-string must still be byte-equal even though it's
        // sitting between two scalar slots.
        if (ok && !expect_eq_bytes(s_back, blob, msg)) {
            ok = false;
        }
        report(cases, "mixed int+string+vector+double wire round-trips",
               ok, msg);
        if (!ok) ++failures;
    }

    // -----------------------------------------------------------------
    // Case 5: decode_arg<std::string> against a non-binary (text) token.
    //         Per the header's "fallback" branch, j.get<std::string>()
    //         is called and must NOT throw 302 for the std::string slot.
    // -----------------------------------------------------------------
    {
        const auto j_text = nlohmann::json("hello");  // major type 3 (text)
        bool threw = false;
        std::string decoded;
        std::string msg;
        try {
            decoded = urpc2_rbk::detail::decode_arg<std::string>(j_text);
        } catch (const nlohmann::json::type_error&) {
            threw = true;
        } catch (...) {
            threw = true;
        }
        if (!threw && decoded == "hello") {
            msg = "text token fell through to j.get<std::string>(\"hello\") OK";
        } else if (threw) {
            msg = "incorrectly threw on text token (regression of fallback)";
            ++failures;
        } else {
            msg = "decoded mismatch: \"" + decoded + "\" vs \"hello\"";
            ++failures;
        }
        report(cases, "decode_arg<std::string> accepts text token (no 302)",
               !threw && decoded == "hello", msg);
    }

    // -----------------------------------------------------------------
    // Case 6: symmetric encode_return / decode_return for std::string
    //         with binary content.  Mirrors the handler-side return path.
    // -----------------------------------------------------------------
    {
        const std::string original = make_embedded_nul();
        const auto j_ret = urpc2_rbk::detail::encode_return(original);
        const bool is_bin = j_ret.is_binary();
        const auto wire = urpc2_rbk::detail::cbor_encode(j_ret);
        const auto back = urpc2_rbk::detail::cbor_decode(wire);
        const auto decoded = urpc2_rbk::detail::decode_return<std::string>(back);
        std::string msg;
        bool ok = is_bin && expect_eq_bytes(decoded, original, msg);
        report(cases, "encode_return / decode_return preserves NULs",
               ok, std::string("is_binary=") + (is_bin ? "true" : "false")
                   + ", " + msg);
        if (!ok) ++failures;
    }

    // -----------------------------------------------------------------
    // Case 7: end-to-end with invoke_from_cbor -- exercises the path
    //         that serve() will use.  Takes std::function<void(std::string)>
    //         so that R is void and we can ignore the return contract;
    //         also covers a std::function<std::string(std::string)> variant.
    // -----------------------------------------------------------------
    {
        // R=std::string, A=std::string -- a pure binary pass-through.
        std::function<std::string(std::string)> pass_through =
            [](std::string s) -> std::string { return s; };

        const std::string blob = make_ff_lead();
        const auto j_arr_in = nlohmann::json::array({
            urpc2_rbk::detail::encode_arg(blob),
        });
        const auto wire_in = urpc2_rbk::detail::cbor_encode(j_arr_in);
        const auto decoded_in = urpc2_rbk::detail::cbor_decode(wire_in);
        const auto out_wire = urpc2_rbk::detail::invoke_from_cbor(
            pass_through,
            decoded_in,
            std::index_sequence<0>{}
        );
        // out_wire is cbor-encoded.  Decode and pull out the return value.
        const auto j_out = urpc2_rbk::detail::cbor_decode(out_wire);
        const auto decoded_out =
            urpc2_rbk::detail::decode_return<std::string>(j_out);

        std::string msg;
        const bool ok = expect_eq_bytes(decoded_out, blob, msg);
        report(cases, "invoke_from_cbor<string,string> preserves bytes",
               ok, msg);
        if (!ok) ++failures;
    }

    // -----------------------------------------------------------------
    // Case 8: regression guard -- decode_arg<int> against a text token
    //         MUST throw type_error 302.  This is the contract that
    //         protects "non-string types don't go through bytes path";
    //         if a future refactor accidentally makes every decode a
    //         bytes fallback, the std::string tests above might still
    //         pass for the wrong reason.
    // -----------------------------------------------------------------
    {
        const auto j_text = nlohmann::json("hello");
        bool got_type_error = false;
        std::string msg = "did not throw type_error on string->int";
        try {
            (void)urpc2_rbk::detail::decode_arg<int>(j_text);
        } catch (const nlohmann::json::type_error& e) {
            got_type_error = true;
            msg = std::string("threw type_error as expected: ") + e.what();
        } catch (...) {
            msg = "threw wrong exception type (not type_error)";
        }
        report(cases, "regression: decode_arg<int> throws on text token",
               got_type_error, msg);
        if (!got_type_error) ++failures;
    }

    // -----------------------------------------------------------------
    // Section A: legal UTF-8 (rounds-trip via binary token)
    // -----------------------------------------------------------------
    {
        const std::string orig = "hello world";
        std::string m; const bool ok = roundtrip_string(orig, m);
        record(cases, "A1 ascii 'hello world'",
               ok, m + " (no high bytes, but still goes via bytes path)", failures);
    }
    {
        const std::string orig =
            std::string{"\xE4\xB8\xAD" "\xE6\x96\x87", 6};
        std::string m; const bool ok = roundtrip_string(orig, m);
        record(cases, "A2 2-byte UTF-8 '中文'", ok,
               m + " (size=6, all 16-bit codepoints)", failures);
    }
    {
        const std::string orig =
            std::string{"\xE6\x97\xA5" "\xE6\x9C\xAC" "\xE8\xAA\x9E", 9};
        std::string m; const bool ok = roundtrip_string(orig, m);
        record(cases, "A3 3-byte UTF-8 '日本語'", ok,
               m + " (size=9)", failures);
    }
    {
        const std::string orig =
            std::string{"\xF0\x9F\x9A\x80", 4};
        std::string m; const bool ok = roundtrip_string(orig, m);
        record(cases, "A4 4-byte UTF-8 emoji '🚀'", ok,
               m + " (size=4)", failures);
    }
    {
        const std::string orig =
            std::string{"a" "\xE4\xB8\xAD" "b" "\xF0\x9F\x9A\x80" "c", 8};
        std::string m; const bool ok = roundtrip_string(orig, m);
        record(cases, "A5 mixed ASCII+UTF-8+emoji 'a中b🚀c'", ok,
               m + " (size=8)", failures);
    }
    {
        const std::string orig =
            std::string{"\xC0\x80", 2};
        std::string m; const bool ok = roundtrip_string(orig, m);
        record(cases, "A6 overlong 2-byte 0xC0 0x80 (spec-illegal)",
               ok, m + " (forbidden by UTF-8, but bytes path transparent)", failures);
    }
    {
        const std::string orig =
            std::string{"\xE0\x80\x80", 3};
        std::string m; const bool ok = roundtrip_string(orig, m);
        record(cases, "A7 overlong 3-byte 0xE0 0x80 0x80", ok,
               m + " (spec-illegal, must pass through)", failures);
    }
    {
        const std::string orig =
            std::string{"\xED\xA0\x80", 3};
        std::string m; const bool ok = roundtrip_string(orig, m);
        record(cases, "A8 UTF-16 surrogate half 0xED 0xA0 0x80", ok,
               m + " (spec-illegal surrogate, must pass through)", failures);
    }

    // -----------------------------------------------------------------
    // Section B: illegal UTF-8 lead/continuation bytes
    // -----------------------------------------------------------------
    {
        const std::string orig =
            std::string{"\x80\xBF\xC0\xFE", 4};
        std::string m; const bool ok = roundtrip_string(orig, m);
        record(cases, "B9 isolated continuation bytes 0x80/0xBF + 0xC0/0xFE",
               ok, m + " (all four are leading-context-illegal)", failures);
    }
    {
        const std::string orig = std::string{"\xC0\xC1", 2};
        std::string m; const bool ok = roundtrip_string(orig, m);
        record(cases, "B10 illegal lead bytes 0xC0 0xC1", ok,
               m + " (0xC0/0xC1 can never start a UTF-8 sequence)", failures);
    }
    {
        const std::string orig =
            std::string{"\xF5\xF6\xF7\xF8\xF9\xFA\xFB\xFC\xFD\xFE\xFF", 11};
        std::string m; const bool ok = roundtrip_string(orig, m);
        record(cases, "B11 0xF5..0xFF byte run (above 4-byte limit)", ok,
               m + " (size=11)", failures);
    }
    {
        const std::string orig = std::string(5, '\xFF');
        std::string m; const bool ok = roundtrip_string(orig, m);
        record(cases, "B12 five 0xFF bytes", ok,
               m + " (size=5)", failures);
    }

    // -----------------------------------------------------------------
    // Section C: byte-length boundary cases
    // -----------------------------------------------------------------
    {
        const std::string orig;
        std::string m; const bool ok = roundtrip_string(orig, m);
        record(cases, "C13 empty string ''", ok, m, failures);
    }
    {
        const std::string orig(1, '\x00');
        std::string m; const bool ok = roundtrip_string(orig, m);
        record(cases, "C14 single 0x00 byte", ok,
               m + " (NUL-only, classic json::string truncation test)", failures);
    }
    {
        const std::string orig(1, '\x7F');
        std::string m; const bool ok = roundtrip_string(orig, m);
        record(cases, "C15 single 0x7F byte (DEL)", ok,
               m + " (highest legal ASCII)", failures);
    }
    {
        const std::string orig(1, '\x80');
        std::string m; const bool ok = roundtrip_string(orig, m);
        record(cases, "C16 single 0x80 byte (illegal lead)", ok,
               m + " (lone continuation byte)", failures);
    }
    {
        const std::string orig(1, '\xFF');
        std::string m; const bool ok = roundtrip_string(orig, m);
        record(cases, "C17 single 0xFF byte", ok,
               m + " (illegal lead)", failures);
    }
    {
        const std::string orig(2, 'X');
        std::string m; const bool ok = roundtrip_string(orig, m);
        record(cases, "C18 2-byte payload", ok,
               m + " (smallest non-empty)", failures);
    }
    {
        const std::string orig(1023, 'A');
        std::string m; const bool ok = roundtrip_string(orig, m);
        record(cases, "C19 1023-byte payload (CBOR 1-byte length boundary)",
               ok, m + " (256+N+1; CBOR length within 1-byte encoding)", failures);
    }
    {
        const std::string orig(1024, 'B');
        std::string m; const bool ok = roundtrip_string(orig, m);
        record(cases, "C20 1024-byte payload (CBOR switches to 2-byte length)",
               ok, m + " (forces CBOR length prefix change)", failures);
    }
    {
        const std::string orig(65536, 'C');
        std::string m; const bool ok = roundtrip_string(orig, m);
        record(cases, "C21 65536-byte payload (CBOR 4-byte length)", ok,
               m + " (forces 32-bit length encoding)", failures);
    }

    // -----------------------------------------------------------------
    // Section D: multi-byte payload with embedded NULs at fixed cadence
    // -----------------------------------------------------------------
    {
        std::string orig = random_bytes(1024);
        for (std::size_t i = 0; i < orig.size(); i += 128) {
            orig[i] = '\0';
        }
        std::string m; const bool ok = roundtrip_string(orig, m);
        record(cases, "D22 1024-byte random, NUL at offset 128/256/...",
               ok, m + " (8 NULs total — none truncate, NUL count preserved)",
               failures);
    }

    // -----------------------------------------------------------------
    // Section E: invoke_from_cbor boundary cases
    // -----------------------------------------------------------------
    {
        // R=void, A=std::string (one), inject two strings in array slot 0/1.
        std::function<void(std::string, std::string)> sink =
            [](std::string /*a*/, std::string /*b*/) { /* discard */ };
        const std::string a = make_isolated_continuation();
        const std::string b = make_embedded_nul();
        const auto j_in = nlohmann::json::array({
            urpc2_rbk::detail::encode_arg(a),
            urpc2_rbk::detail::encode_arg(b)});
        const auto wire_in = urpc2_rbk::detail::cbor_encode(j_in);
        const auto dec_in = urpc2_rbk::detail::cbor_decode(wire_in);
        const auto out_wire = urpc2_rbk::detail::invoke_from_cbor(
            sink, dec_in, std::index_sequence<0, 1>{});
        // void returns encode to JSON null.
        const auto j_out = urpc2_rbk::detail::cbor_decode(out_wire);
        const bool ok = j_out.is_null();
        record(cases,
               "E23 invoke_from_cbor<void,string,string> with 0xA4 + NUL payload",
               ok,
               ok ? "void returned JSON null as expected; both arg bytes accepted"
                  : "void return path did not yield null",
               failures);
    }
    {
        // R = std::tuple<std::string, int>, A = std::string.
        std::function<std::tuple<std::string, int>(std::string)> make_pair =
            [counter = 0](std::string s) mutable
                -> std::tuple<std::string, int> {
                return std::make_tuple(std::move(s), ++counter);
            };
        const std::string blob = make_ff_lead();
        const auto j_in = nlohmann::json::array({
            urpc2_rbk::detail::encode_arg(blob)});
        const auto wire_in = urpc2_rbk::detail::cbor_encode(j_in);
        const auto dec_in = urpc2_rbk::detail::cbor_decode(wire_in);
        const auto out_wire = urpc2_rbk::detail::invoke_from_cbor(
            make_pair, dec_in, std::index_sequence<0>{});
        const auto j_out = urpc2_rbk::detail::cbor_decode(out_wire);
        const auto tup =
            urpc2_rbk::detail::decode_return<
                std::tuple<std::string, int>>(j_out);
        bool ok = (std::get<1>(tup) == 1);
        std::string m;
        if (ok) {
            ok = expect_eq_bytes(std::get<0>(tup), blob, m);
            if (ok) m = "tuple=<size=" + std::to_string(blob.size())
                       + " bytes, int=1>";
        } else m = "counter != 1 or tuple destructure failed";
        record(cases,
               "E24 invoke_from_cbor<tuple<string,int>,string>", ok, m, failures);
    }
    {
        // R = void, A = int, double, std::vector<int> -- make sure the
        // non-string paths inside encode_arg stay text/number/array.
        std::function<void(int, double, std::vector<int>)> sink =
            [](int, double, std::vector<int>) { /* discard */ };
        const auto j_in = nlohmann::json::array({
            urpc2_rbk::detail::encode_arg(7),
            urpc2_rbk::detail::encode_arg(2.5),
            urpc2_rbk::detail::encode_arg(std::vector<int>{10, 20, 30})});
        const auto wire_in = urpc2_rbk::detail::cbor_encode(j_in);
        const auto dec_in = urpc2_rbk::detail::cbor_decode(wire_in);
        const auto out_wire = urpc2_rbk::detail::invoke_from_cbor(
            sink, dec_in, std::index_sequence<0, 1, 2>{});
        const auto j_out = urpc2_rbk::detail::cbor_decode(out_wire);
        const bool ok = j_out.is_null();
        record(cases,
               "E25 invoke_from_cbor<void,int,double,vector<int>>",
               ok,
               ok ? "void returned null; all 3 non-string args decoded via native nlohmann path"
                  : "non-string args leaked into bytes path or void return broken",
               failures);
    }

    // -----------------------------------------------------------------
    // Section F: encode_arg accepts other types (path-routing check)
    // -----------------------------------------------------------------
    {
        // F26: const char* literal "hello".  encode_arg<const char[6]>
        // decays and the `if constexpr (is_same_v<U, std::string>)` is
        // false, so it goes through nlohmann::json(const char*) which
        // is the standard string_token.  Output: round-trip via text.
        const char* lit = "hello";
        const auto j = nlohmann::json::array({urpc2_rbk::detail::encode_arg(lit)});
        bool ok = !j.empty();
        std::string m;
        if (ok) {
            const auto& el = j[0];
            // text path: is_string() true; bytes path would be is_binary() true.
            const bool is_text = el.is_string();
            const bool is_bytes = el.is_binary();
            ok = is_text && !is_bytes;
            m = "decayed to text token (is_string=" + std::string(is_text?"true":"false")
              + ", is_binary=" + std::string(is_bytes?"true":"false") + ")";
        }
        record(cases,
               "F26 encode_arg<const char*> routes via text token, not bytes",
               ok, m, failures);
    }
    {
        // F27: char[N] array ("world").  Same path as F26; the decay should
        // NOT match the std::string branch (which checks the *exact* type
        // string, not "anything stringish").
        char buf[6] = {'w','o','r','l','d','\0'};
        const auto j = nlohmann::json::array({urpc2_rbk::detail::encode_arg(buf)});
        bool ok = !j.empty();
        std::string m;
        if (ok) {
            const bool is_text = j[0].is_string();
            const bool is_bytes = j[0].is_binary();
            ok = is_text && !is_bytes;
            m = "char[6] array: is_string=" + std::string(is_text?"true":"false")
              + ", is_binary=" + std::string(is_bytes?"true":"false")
              + " (proves 'is_same_v<string>' is strict, not stringish)";
        }
        record(cases,
               "F27 encode_arg<char[N]> routes via text token (strict type check)",
               ok, m, failures);
    }
    {
        // F28: std::string_view -- nlohmann 3.12.x supports it (C++17, line
        // 20083 #include <string_view>) so this compiles.  Plan-A widened
        // dispatch so string_view is treated as a bytes-source just like
        // std::string: encodes to a CBOR bytes token, decodes symmetrically.
        // This locks in the contract: string_view IS binary-safe in plan A.
#if defined(__cpp_lib_string_view)
        std::string_view sv = "abc";
        const auto j = nlohmann::json::array({urpc2_rbk::detail::encode_arg(sv)});
        bool ok = !j.empty();
        std::string m;
        if (ok) {
            const bool is_text = j[0].is_string();
            const bool is_bytes = j[0].is_binary();
            ok = is_bytes && !is_text;
            m = "string_view compiled (C++17 enabled); routes to bytes token "
              + std::string("is_string=") + (is_text?"true":"false")
              + ", is_binary=" + std::string(is_bytes?"true":"false")
              + " (bytes path -- binary-safe in plan A)";
        } else m = "string_view did not produce a json element";
        record(cases,
               "F28 encode_arg<string_view> takes bytes path (binary-safe)",
               ok, m, failures);

        // F28b: std::string_view with illegal UTF-8 (0xA4) round-trips.
        // Locks in the F1 fix: string_view not only takes the bytes path,
        // it also actually carries binary data through end-to-end.
        {
            std::string_view blob("\xA4\xA4""X""\xA4", 5);
            const auto j = nlohmann::json::array({urpc2_rbk::detail::encode_arg(blob)});
            const auto wire = urpc2_rbk::detail::cbor_encode(j);
            const auto back = urpc2_rbk::detail::cbor_decode(wire);
            const auto dec = urpc2_rbk::detail::decode_arg<std::string>(back.at(0));
            std::string m; bool ok = expect_eq_bytes(dec, std::string(blob), m);
            record(cases,
                   "F28b string_view with 0xA4 round-trips byte-for-byte",
                   ok, m, failures);
        }
#else
        record(cases,
               "F28 encode_arg<string_view> skipped",
               true, "string_view unavailable on this compiler", failures);
#endif
    }


    // =================================================================
    // ROUND-2 ADDITIONS: mixed-type routing, binary/string_view
    // contract guards, large/edge inputs, dispatch boundaries.
    // =================================================================

    // -----------------------------------------------------------------
    // Section A: mixed multi-arg path-routing (encode side only)
    // -----------------------------------------------------------------
    {
        // A1: (std::string, std::string_view, int)
        // Plan A (post commit 6b8b79b) widens to string_view: BOTH
        // std::string and std::string_view now take the bytes path.
        std::string bin_blob; bin_blob.push_back(static_cast<char>(0xA4));
        bin_blob += "x";
        std::string_view text_view = "hello";
        const int i_val = -7;
        const auto j = nlohmann::json::array({
            urpc2_rbk::detail::encode_arg(bin_blob),
            urpc2_rbk::detail::encode_arg(text_view),
            urpc2_rbk::detail::encode_arg(i_val)});
        const auto& e0 = j[0]; const auto& e1 = j[1]; const auto& e2 = j[2];
        const bool ok =
            e0.is_binary()
            && e1.is_binary() && !e1.is_string()
            && e2.is_number_integer();
        std::ostringstream m;
        m << "slot0=" << (e0.is_binary()?"bytes":"other")
          << " slot1=" << (e1.is_binary()?"bytes":"other")
          << " slot2=" << (e2.is_number_integer()?"int":"other");
        record(cases,
               "A1 mixed (string,string_view,int) paths: bytes/bytes/int",
               ok, m.str(), failures);
    }
    {
        // A2: (std::vector<int>, std::string_view, double)
        std::vector<int> vec{1,2,3,4,5};
        std::string_view sv = "view-data";
        const double d = 2.71828;
        const auto j = nlohmann::json::array({
            urpc2_rbk::detail::encode_arg(vec),
            urpc2_rbk::detail::encode_arg(sv),
            urpc2_rbk::detail::encode_arg(d)});
        const bool ok =
            j[0].is_array()
            && j[1].is_binary() && !j[1].is_string()
            && j[2].is_number_float();
        std::ostringstream m;
        m << "vec.is_array=" << j[0].is_array()
          << " sv.bytes=" << j[1].is_binary()
          << " d.float=" << j[2].is_number_float();
        record(cases,
               "A2 mixed (vector<int>,string_view,double): array/bytes/float",
               ok, m.str(), failures);
    }
    {
        // A3: empty std::string_view -> empty binary token (widened plan A).
        std::string_view sv_empty;
        const auto j = nlohmann::json::array({
            urpc2_rbk::detail::encode_arg(sv_empty)});
        const auto& bin = j[0].get_ref<const nlohmann::json::binary_t&>();
        const bool ok = j[0].is_binary() && bin.empty();
        record(cases,
               "A3 empty string_view -> empty BINARY token",
               ok,
               std::string("is_binary=") + (j[0].is_binary()?"true":"false")
               + ", binary_size=" + std::to_string(bin.size()),
               failures);
    }

    // -----------------------------------------------------------------
    // Section B: invoke_from_cbor end-to-end strengthening.
    // NB: current header does NOT special-case std::string_view in
    // decode_arg, so handlers must take std::string (or void) for the
    // bytes path to work end-to-end.
    // -----------------------------------------------------------------
    {
        // B1: void(string, string) with two binary-tainted payloads.
        std::string captured_a, captured_b;
        std::function<void(std::string, std::string)> sink =
            [&captured_a, &captured_b](std::string x, std::string y) {
                captured_a = std::move(x);
                captured_b = std::move(y);
            };
        std::string pav;
        pav.push_back(static_cast<char>(0xA4));
        pav += "mid";
        pav.push_back(static_cast<char>(0xA4));
        pav += "end";
        std::string pbv(2, static_cast<char>(0xFF));
        pbv += "sig";
        pbv.push_back(static_cast<char>(0xFE));
        const auto j_in = nlohmann::json::array({
            urpc2_rbk::detail::encode_arg(pav),
            urpc2_rbk::detail::encode_arg(pbv)});
        const auto wire_in = urpc2_rbk::detail::cbor_encode(j_in);
        const auto dec_in = urpc2_rbk::detail::cbor_decode(wire_in);
        const auto out_wire = urpc2_rbk::detail::invoke_from_cbor(
            sink, dec_in, std::index_sequence<0, 1>{});
        const auto j_out = urpc2_rbk::detail::cbor_decode(out_wire);
        std::string m;
        bool ok = j_out.is_null();
        if (ok) ok = expect_eq_bytes(captured_a, pav, m);
        if (ok) ok = expect_eq_bytes(captured_b, pbv, m);
        if (ok) m = "void captured a.size=" + std::to_string(captured_a.size())
                 + " b.size=" + std::to_string(captured_b.size())
                 + "; bytes_equal=true (both)";
        record(cases,
               "B1 invoke_from_cbor<void,string,string>: 0xA4 + 0xFF byte-equal",
               ok, m, failures);
    }
    {
        // B2: string(string) end-to-end -- encoding-side std::string, but
        // the result is also std::string so it's symmetric.  We deliberately
        // inject a payload with embedded NUL through encode_arg<string> and
        // verify the handler receives it byte-for-byte.
        std::function<std::string(std::string)> passthrough =
            [](std::string s) { return s; };
        const std::string bin = make_embedded_nul();
        const auto j_in = nlohmann::json::array({
            urpc2_rbk::detail::encode_arg(bin)});
        const auto wire_in = urpc2_rbk::detail::cbor_encode(j_in);
        const auto dec_in = urpc2_rbk::detail::cbor_decode(wire_in);
        const auto out_wire = urpc2_rbk::detail::invoke_from_cbor(
            passthrough, dec_in, std::index_sequence<0>{});
        const auto j_out = urpc2_rbk::detail::cbor_decode(out_wire);
        const std::string back = urpc2_rbk::detail::decode_return<std::string>(j_out);
        std::string m;
        const bool ok = expect_eq_bytes(back, bin, m);
        record(cases,
               "B2 invoke_from_cbor<string,string> w/ embedded NULs",
               ok, m, failures);
    }
    {
        // B3: void(int, string, double, ...) -- mixed types, no string_view.
        int captured_i = 0;
        double captured_d = 0;
        std::string captured_s;
        std::function<void(int, std::string, double)> sink =
            [&](int i, std::string s, double d) {
                captured_i = i; captured_d = d; captured_s = std::move(s);
            };
        std::string s_blob = make_ff_lead();
        const auto j_in = nlohmann::json::array({
            urpc2_rbk::detail::encode_arg(99),
            urpc2_rbk::detail::encode_arg(s_blob),
            urpc2_rbk::detail::encode_arg(1.5)});
        const auto wire_in = urpc2_rbk::detail::cbor_encode(j_in);
        const auto dec_in = urpc2_rbk::detail::cbor_decode(wire_in);
        const auto out_wire = urpc2_rbk::detail::invoke_from_cbor(
            sink, dec_in, std::index_sequence<0, 1, 2>{});
        const auto j_out = urpc2_rbk::detail::cbor_decode(out_wire);
        bool ok = j_out.is_null() && captured_i == 99 && captured_d == 1.5;
        std::string m;
        if (ok) ok = expect_eq_bytes(captured_s, s_blob, m);
        record(cases,
               "B3 invoke_from_cbor<void,int,string,double>: bytes round-trip",
               ok ? true : (captured_s.size() == s_blob.size()),
               "i=" + std::to_string(captured_i) + " d=" + std::to_string(captured_d)
               + " string.size=" + std::to_string(captured_s.size())
               + " (orig=" + std::to_string(s_blob.size()) + ")",
               failures);
    }
    {
        // B4: explicit no-throw guarantee: call<void, std::string>(...)
        // with a 0xA4-laden blob must NOT throw type_error 316 at any hop.
        std::string bin;
        bin.push_back(static_cast<char>(0xA4));
        bin += "x";
        bin.push_back(static_cast<char>(0xA4));
        bool threw = false;
        std::string m;
        try {
            const auto j_arg = urpc2_rbk::detail::encode_arg(bin);
            const auto j_arr = nlohmann::json::array({j_arg});
            const auto wire = urpc2_rbk::detail::cbor_encode(j_arr);
            const auto back = urpc2_rbk::detail::cbor_decode(wire);
            const auto decoded =
                urpc2_rbk::detail::decode_arg<std::string>(back.at(0));
            const bool eq_ok = expect_eq_bytes(decoded, bin, m);
            m = "no throw, bytes_equal=" + std::string(eq_ok?"true":"false");
        } catch (const nlohmann::json::exception& e) {
            threw = true; m = std::string("UNEXPECTED THROW: ") + e.what();
        } catch (const std::exception& e) {
            threw = true; m = std::string("UNEXPECTED STD THROW: ") + e.what();
        }
        record(cases,
               "B4 string arg with 0xA4: no type_error.316",
               !threw, m, failures);
    }

    // -----------------------------------------------------------------
    // Section C: boundary stress.
    // -----------------------------------------------------------------
    {
        // C1: 100 KB std::string with 0xA4 + NUL sprinkled.
        std::string big = random_bytes(100 * 1024);
        for (std::size_t i = 0; i < big.size(); i += 257)
            big[i] = static_cast<char>(0xA4);
        for (std::size_t i = 17; i < big.size(); i += 401)
            big[i] = '\0';
        std::function<std::string(std::string)> id = [](std::string s){
            return s;
        };
        const auto j_in = nlohmann::json::array({
            urpc2_rbk::detail::encode_arg(big)});
        const auto wire = urpc2_rbk::detail::cbor_encode(j_in);
        const auto dec_in = urpc2_rbk::detail::cbor_decode(wire);
        const auto out_wire = urpc2_rbk::detail::invoke_from_cbor(
            id, dec_in, std::index_sequence<0>{});
        const auto j_out = urpc2_rbk::detail::cbor_decode(out_wire);
        const std::string round = urpc2_rbk::detail::decode_return<std::string>(j_out);
        std::string m;
        const bool ok = expect_eq_bytes(round, big, m);
        record(cases,
               "C1 invoke_from_cbor<string,string> 100 KB w/ 0xA4 + NUL",
               ok, m + " size=102400", failures);
    }
    {
        // C2: 1 MB payload (no invalid bytes -- CBOR length encoding switch)
        const std::size_t N = 1024 * 1024;
        std::string big(N, 'Z');
        std::function<std::string(std::string)> id = [](std::string s){
            return s;
        };
        const auto j_in = nlohmann::json::array({
            urpc2_rbk::detail::encode_arg(big)});
        const auto wire = urpc2_rbk::detail::cbor_encode(j_in);
        const auto dec_in = urpc2_rbk::detail::cbor_decode(wire);
        const auto out_wire = urpc2_rbk::detail::invoke_from_cbor(
            id, dec_in, std::index_sequence<0>{});
        const auto j_out = urpc2_rbk::detail::cbor_decode(out_wire);
        const std::string round = urpc2_rbk::detail::decode_return<std::string>(j_out);
        std::string m;
        const bool ok = expect_eq_bytes(round, big, m);
        record(cases,
               "C2 invoke_from_cbor<string,string> 1 MB",
               ok, "size=" + std::to_string(N), failures);
    }
    {
        // C3: 100-NUL std::string
        std::string all_nul(100, '\0');
        std::function<std::string(std::string)> id = [](std::string s){
            return s;
        };
        const auto j_in = nlohmann::json::array({
            urpc2_rbk::detail::encode_arg(all_nul)});
        const auto wire = urpc2_rbk::detail::cbor_encode(j_in);
        const auto dec_in = urpc2_rbk::detail::cbor_decode(wire);
        const auto out_wire = urpc2_rbk::detail::invoke_from_cbor(
            id, dec_in, std::index_sequence<0>{});
        const auto j_out = urpc2_rbk::detail::cbor_decode(out_wire);
        const std::string round = urpc2_rbk::detail::decode_return<std::string>(j_out);
        std::string m;
        const bool ok = expect_eq_bytes(round, all_nul, m);
        record(cases,
               "C3 100-byte all-NUL std::string round-trip",
               ok, "100 NULs preserved", failures);
    }
    {
        // C4: 100-byte all-0xFF std::string
        std::string all_ff(100, static_cast<char>(0xFF));
        std::function<std::string(std::string)> id = [](std::string s){
            return s;
        };
        const auto j_in = nlohmann::json::array({
            urpc2_rbk::detail::encode_arg(all_ff)});
        const auto wire = urpc2_rbk::detail::cbor_encode(j_in);
        const auto dec_in = urpc2_rbk::detail::cbor_decode(wire);
        const auto out_wire = urpc2_rbk::detail::invoke_from_cbor(
            id, dec_in, std::index_sequence<0>{});
        const auto j_out = urpc2_rbk::detail::cbor_decode(out_wire);
        const std::string round = urpc2_rbk::detail::decode_return<std::string>(j_out);
        std::string m;
        const bool ok = expect_eq_bytes(round, all_ff, m);
        record(cases,
               "C4 100-byte all-0xFF std::string round-trip",
               ok, "100 0xFF bytes preserved", failures);
    }
    {
        // C5: 100 contiguous continuation bytes 0x80-0xBF
        std::string runs;
        for (int i = 0; i < 100; ++i)
            runs.push_back(static_cast<char>(0x80 + (i & 0x3F)));
        std::function<std::string(std::string)> id = [](std::string s){
            return s;
        };
        const auto j_in = nlohmann::json::array({
            urpc2_rbk::detail::encode_arg(runs)});
        const auto wire = urpc2_rbk::detail::cbor_encode(j_in);
        const auto dec_in = urpc2_rbk::detail::cbor_decode(wire);
        const auto out_wire = urpc2_rbk::detail::invoke_from_cbor(
            id, dec_in, std::index_sequence<0>{});
        const auto j_out = urpc2_rbk::detail::cbor_decode(out_wire);
        const std::string round = urpc2_rbk::detail::decode_return<std::string>(j_out);
        std::string m;
        const bool ok = expect_eq_bytes(round, runs, m);
        record(cases,
               "C5 100 continuation bytes 0x80..0xBF round-trip",
               ok, "size=" + std::to_string(runs.size()), failures);
    }

    // -----------------------------------------------------------------
    // Section D: C++ type dispatch edges.
    // -----------------------------------------------------------------
    {
        // D1: const std::string& lvalue
        const std::string orig = make_embedded_nul();
        const auto j = nlohmann::json::array({
            urpc2_rbk::detail::encode_arg(orig)});
        record(cases,
               R"(D1 encode_arg<const std::string&> (lvalue) -> bytes)",
               j[0].is_binary(),
               std::string("is_binary=") + (j[0].is_binary()?"true":"false")
               + ", is_string=" + (j[0].is_string()?"true":"false"),
               failures);
    }
    {
        // D2: std::string&& rvalue
        const auto j = nlohmann::json::array({
            urpc2_rbk::detail::encode_arg(make_ff_lead())});
        record(cases,
               R"(D2 encode_arg<std::string&&> (rvalue) -> bytes)",
               j[0].is_binary(),
               std::string("is_binary=") + (j[0].is_binary()?"true":"false"),
               failures);
    }
    {
        // D3: string_view with embedded NUL -- widended plan A preserves it
        std::string blob(32, 'X');
        blob[8] = '\0';
        std::string_view sv{blob};
        const auto j_in = nlohmann::json::array({
            urpc2_rbk::detail::encode_arg(sv)});
        const auto wire = urpc2_rbk::detail::cbor_encode(j_in);
        const auto back = urpc2_rbk::detail::cbor_decode(wire);
        const auto dec = urpc2_rbk::detail::decode_arg<std::string>(back.at(0));
        std::string m;
        const bool ok = expect_eq_bytes(dec, blob, m);
        record(cases,
               "D3 string_view w/ embedded NUL: bytes path, length=32 preserved",
               ok,
               std::string("decoded.size=") + std::to_string(dec.size()),
               failures);
    }
    {
        // D4: std::pmr::string -- is NOT std::string (allocator is different),
        // no special case in the header, falls through to nlohmann::json
        // which produces a TEXT token.  nlohmann 3.12 keeps embedded NULs
        // in TEXT tokens (no NUL truncation), but DOES validate UTF-8 on
        // text tokens; an input with an invalid UTF-8 byte would still
        // throw type_error.316 here.  Bytes path is the only fully
        // binary-safe channel; this test pins pmr::string to TEXT.
        std::pmr::string pmr_blob;
        pmr_blob.assign("AAA", 3); pmr_blob.push_back('\0');
        pmr_blob.append("BBB", 3);
        const auto j = nlohmann::json::array({
            urpc2_rbk::detail::encode_arg(pmr_blob)});
        const bool text = j[0].is_string() && !j[0].is_binary();
        const std::size_t got = j[0].is_string()
            ? j[0].get_ref<const std::string&>().size()
            : j[0].get_ref<const nlohmann::json::binary_t&>().size();
        record(cases,
               "D4 encode_arg<std::pmr::string> routes via text token (not bytes)",
               text && got == 7,
               std::string("is_text=") + (text?"true":"false")
               + ", decoded_size=" + std::to_string(got)
               + " (original=7; text path preserves NULs but would reject invalid UTF-8)",
               failures);
    }
    {
        // D5: const wchar_t* -- not std::string, not string_view, falls
        // through to nlohmann::json which produces a TEXT token.
        const wchar_t* w = L"hello";
        const auto j = nlohmann::json::array({
            urpc2_rbk::detail::encode_arg(w)});
        record(cases,
               "D5 encode_arg<const wchar_t*> -> text (NOT bytes)",
               j[0].is_string() && !j[0].is_binary(),
               std::string("is_string=") + (j[0].is_string()?"true":"false")
               + ", is_binary=" + (j[0].is_binary()?"true":"false"),
               failures);
    }

    // -----------------------------------------------------------------
    // Section E: regression guards (nested type contracts).
    // -----------------------------------------------------------------
    {
        // E1: int under a type alias does NOT use bytes path
        using MyInt = int;
        MyInt x = 42;
        const auto j = nlohmann::json::array({
            urpc2_rbk::detail::encode_arg(x)});
        record(cases,
               "E1 encode_arg<int> via type alias -> number, not bytes",
               j[0].is_number_integer() && !j[0].is_binary(),
               std::string("is_number_integer=")
               + (j[0].is_number_integer()?"true":"false")
               + ", is_binary=" + (j[0].is_binary()?"true":"false"),
               failures);
    }
    {
        // E2: vector<string> -- OUTER is array, INNER strings are TEXT
        // (because encode_arg<vector<string>> falls through, and nlohmann's
        // adapter for string inside containers is text-token, not bytes).
        std::vector<std::string> vs;
        vs.push_back(make_ff_lead());
        vs.push_back(make_embedded_nul());
        const auto j_outer = nlohmann::json::array({
            urpc2_rbk::detail::encode_arg(vs)});
        const auto wire = urpc2_rbk::detail::cbor_encode(j_outer);
        const auto back = urpc2_rbk::detail::cbor_decode(wire);
        const auto& outer = back.at(0);
        const bool ok = outer.is_array()
                      && outer.size() == 2
                      && outer[0].is_string() && !outer[0].is_binary()
                      && outer[1].is_string() && !outer[1].is_binary();
        record(cases,
               "E2 encode_arg<vector<string>> -> array of TEXT tokens",
               ok,
               std::string("outer.is_array=") + (outer.is_array()?"true":"false")
               + " (inner strings are TEXT)",
               failures);
    }
    {
        // E3: pair<string,int> -- outer falls through to text+number array.
        std::pair<std::string, int> p{make_ff_lead(), 7};
        const auto j_outer = nlohmann::json::array({
            urpc2_rbk::detail::encode_arg(p)});
        const auto wire = urpc2_rbk::detail::cbor_encode(j_outer);
        const auto back = urpc2_rbk::detail::cbor_decode(wire);
        const auto& outer = back.at(0);
        const bool ok = outer.is_array()
                      && outer.size() == 2
                      && outer[0].is_string() && !outer[0].is_binary()
                      && outer[1].is_number_integer();
        record(cases,
               "E3 encode_arg<pair<string,int>> -> [text,int]",
               ok,
               std::string("inner[0].is_string=")
               + (outer[0].is_string()?"true":"false")
               + ", inner[0].is_binary=" + (outer[0].is_binary()?"true":"false"),
               failures);
    }
    {
        // E4: map<string,int> -- outer is json object, keys come back as
        // std::string (text).  Document the contract: map KEYS cannot be
        // binary.
        std::map<std::string, int> m;
        m["k"] = 1;
        m["kk"] = 2;
        const auto j_outer = nlohmann::json::array({
            urpc2_rbk::detail::encode_arg(m)});
        const auto wire = urpc2_rbk::detail::cbor_encode(j_outer);
        const auto back = urpc2_rbk::detail::cbor_decode(wire);
        const auto& outer = back.at(0);
        const bool ok = outer.is_object() && !outer.empty();
        record(cases,
               "E4 encode_arg<map<string,int>> -> object (text keys)",
               ok,
               std::string("outer.is_object=") + (outer.is_object()?"true":"false")
               + ", keys come back as std::string",
               failures);
    }

    // -----------------------------------------------------------------
    // Section F: decode_arg<T>/decode_return<T> where T is std::string_view.
    //   Locks in the symmetric side of F1: after encode_arg<string_view>
    //   emits a binary token, the receive side must not throw on it.
    // -----------------------------------------------------------------
    {
        // F1: decode_arg<std::string_view> on a binary token returns the
        // raw bytes view (no copy, no UTF-8 check, no 302).
        std::string payload;
        payload.push_back(static_cast<char>(0xA4));
        payload += "X";
        payload.push_back(static_cast<char>(0xFF));
        payload.push_back('\0');
        const auto j = nlohmann::json::array({urpc2_rbk::detail::encode_arg(payload)});
        const auto wire = urpc2_rbk::detail::cbor_encode(j);
        const auto back = urpc2_rbk::detail::cbor_decode(wire);
        std::string_view got = urpc2_rbk::detail::decode_arg<std::string_view>(back.at(0));
        std::string roundtripped{got.data(), got.size()};
        std::string m;
        const bool ok = expect_eq_bytes(roundtripped, payload, m);
        record(cases,
               "F1 decode_arg<string_view> on binary token yields raw bytes",
               ok,
               m + " (view.size=" + std::to_string(got.size()) + ")",
               failures);
    }
    {
        // F2: handler signature void(string_view) end-to-end via invoke_from_cbor.
        std::string captured;
        std::function<void(std::string_view)> sink =
            [&](std::string_view sv) { captured.assign(sv.data(), sv.size()); };
        std::string payload;
        payload.push_back(static_cast<char>(0xA4));
        payload += "binary-payload";
        payload.push_back(static_cast<char>(0xA4));
        const auto j_in = nlohmann::json::array({urpc2_rbk::detail::encode_arg(payload)});
        const auto wire_in = urpc2_rbk::detail::cbor_encode(j_in);
        const auto dec_in = urpc2_rbk::detail::cbor_decode(wire_in);
        const auto out_wire = urpc2_rbk::detail::invoke_from_cbor(
            sink, dec_in, std::index_sequence<0>{});
        (void)out_wire;
        std::string m;
        const bool ok = expect_eq_bytes(captured, payload, m);
        record(cases,
               "F2 invoke_from_cbor<void,string_view>: bytes survive end-to-end",
               ok,
               m + " (captured.size=" + std::to_string(captured.size()) + ")",
               failures);
    }
    {
        // F3: handler signature string_view(string) -- receive string_view, return string_view.
        std::function<std::string_view(std::string_view)> passthrough =
            [](std::string_view sv) -> std::string_view { return sv; };
        std::string payload;
        payload.push_back(static_cast<char>(0xA4));
        payload += std::string(100, 'Y');
        payload.push_back('\0');
        const auto j_in = nlohmann::json::array({urpc2_rbk::detail::encode_arg(payload)});
        const auto wire_in = urpc2_rbk::detail::cbor_encode(j_in);
        const auto dec_in = urpc2_rbk::detail::cbor_decode(wire_in);
        const auto out_wire = urpc2_rbk::detail::invoke_from_cbor(
            passthrough, dec_in, std::index_sequence<0>{});
        const auto j_out = urpc2_rbk::detail::cbor_decode(out_wire);
        std::string_view got = urpc2_rbk::detail::decode_return<std::string_view>(j_out);
        std::string roundtripped{got.data(), got.size()};
        std::string m;
        const bool ok = expect_eq_bytes(roundtripped, payload, m);
        record(cases,
               "F3 invoke_from_cbor<string_view,string_view>: bytes round-trip",
               ok,
               m + " (view.size=" + std::to_string(got.size()) + ")",
               failures);
    }
    {
        // F4: text-token fallback -- decode_arg<string_view> on a text
        // token also works (non-rbk peer emits text). Locks in the
        // asymmetric design from the previous review.
        nlohmann::json text_j = "hello";
        std::string_view got = urpc2_rbk::detail::decode_arg<std::string_view>(text_j);
        const bool ok = (got == std::string_view{"hello"});
        record(cases,
               "F4 decode_arg<string_view> accepts text token (fallback)",
               ok,
               std::string("got.size=") + std::to_string(got.size())
               + " got=\"" + std::string{got} + "\"",
               failures);
    }

    // -----------------------------------------------------------------
    // Print final tally.
    // -----------------------------------------------------------------
    std::cout << "== urpc2_rbk header-only unit tests ==\n";
    for (const auto& c : cases) {
        std::cout << "[" << (c.passed ? "PASS" : "FAIL") << "] "
                  << c.name << "\n";
        std::cout << "        " << c.detail << "\n";
    }
    const int passed = static_cast<int>(cases.size()) - failures;
    std::cout << "\n" << passed << "/" << cases.size() << " passed\n";
    return failures == 0 ? 0 : 1;
}
