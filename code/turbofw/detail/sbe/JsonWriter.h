// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <format>
#include <string_view>
#include <type_traits>
#include <vector>

namespace turbofw::detail::sbe {

/// Streaming JSON writer: knows object/array nesting and inserts the separators, so a caller only emits
/// keys and values in document order. String keys and values are escaped.
template <typename OutputIt>
class JsonWriter {
private:
    struct Frame {
        bool isArray;
        bool first{true};
    };

    OutputIt out_;
    std::vector<Frame> stack_;

    void writeEscaped(std::string_view value) {
        for (char const ch : value) {
            switch (ch) {
            case '"':
                *out_++ = '\\';
                *out_++ = '"';
                break;
            case '\\':
                *out_++ = '\\';
                *out_++ = '\\';
                break;
            case '\b':
                *out_++ = '\\';
                *out_++ = 'b';
                break;
            case '\f':
                *out_++ = '\\';
                *out_++ = 'f';
                break;
            case '\n':
                *out_++ = '\\';
                *out_++ = 'n';
                break;
            case '\r':
                *out_++ = '\\';
                *out_++ = 'r';
                break;
            case '\t':
                *out_++ = '\\';
                *out_++ = 't';
                break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    std::format_to(out_, "\\u{:04x}", static_cast<unsigned int>(static_cast<unsigned char>(ch)));
                } else {
                    *out_++ = ch;
                }
            }
        }
    }

    /// Separator between the members of the current object: a key follows
    void memberSeparator() {
        if (stack_.empty()) { // not inside a container: a key is a misuse, leave the stream untouched
            return;
        }
        auto& frame = stack_.back();
        if (!frame.first) {
            *out_++ = ',';
        }
        frame.first = false;
    }

    /// Separator before an element of the current array: object members are separated by `key()`
    void elementSeparator() {
        if (stack_.empty() || !stack_.back().isArray) {
            return;
        }
        auto& frame = stack_.back();
        if (!frame.first) {
            *out_++ = ',';
        }
        frame.first = false;
    }

public:
    explicit JsonWriter(OutputIt it) : out_{it} {}

    void objectBegin() {
        elementSeparator();
        *out_++ = '{';
        stack_.push_back(Frame{.isArray = false});
    }
    void objectEnd() {
        *out_++ = '}';
        stack_.pop_back();
    }
    void arrayBegin() {
        elementSeparator();
        *out_++ = '[';
        stack_.push_back(Frame{.isArray = true});
    }
    void arrayEnd() {
        *out_++ = ']';
        stack_.pop_back();
    }
    void key(std::string_view value) {
        memberSeparator();
        *out_++ = '"';
        writeEscaped(value);
        *out_++ = '"';
        *out_++ = ':';
    }
    void valueStr(std::string_view value) {
        elementSeparator();
        *out_++ = '"';
        writeEscaped(value);
        *out_++ = '"';
    }
    template <typename V>
        requires std::is_integral_v<V> || std::is_floating_point_v<V>
    void valueNumber(V value) {
        elementSeparator();
        if constexpr (std::is_floating_point_v<V>) {
            std::format_to(out_, "{}", value);
        } else if constexpr (std::is_signed_v<V>) {
            std::format_to(out_, "{}", static_cast<std::int64_t>(value));
        } else {
            std::format_to(out_, "{}", static_cast<std::uint64_t>(value));
        }
    }
    void valueNull() {
        elementSeparator();
        std::format_to(out_, "null");
    }
};

template <typename OutputIt>
JsonWriter(OutputIt) -> JsonWriter<OutputIt>;

} // namespace turbofw::detail::sbe
