// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <format>
#include <type_traits>
#include <vector>

namespace turbofw::detail::sbe {

template <typename OutputIt>
class JsonWriter {
private:
    OutputIt out_;
    std::vector<bool> stack_;

    template <typename VecRef>
    static auto exchange(VecRef ref, bool value) noexcept -> bool {
        auto const result = static_cast<bool>(ref);
        ref = value;
        return result;
    }

public:
    explicit JsonWriter(OutputIt it) : out_{it} {}

    void objectBegin() {
        *out_++ = '{';
        stack_.emplace_back(true);
    }
    void objectEnd() {
        *out_++ = '}';
        stack_.pop_back();
    }
    void arrayBegin() {
        *out_++ = '[';
        stack_.emplace_back(true);
    }
    void arrayEnd() {
        *out_++ = ']';
        stack_.pop_back();
    }
    void key(std::string_view value) {
        if (!exchange(stack_.back(), false)) {
            *out_++ = ',';
        }
        std::format_to(out_, "\"{}\":", value);
    }
    void valueStr(std::string_view value) {
        // TODO: fix me (escape)
        *out_++ = '"';
        std::format_to(out_, "{}", value);
        *out_++ = '"';
    }
    template <typename V>
        requires std::is_integral_v<V> || std::is_floating_point_v<V>
    void valueNumber(V value) {
        if constexpr (std::is_floating_point_v<V>) {
            std::format_to(out_, "{}", value);
        } else if constexpr (std::is_signed_v<V>) {
            std::format_to(out_, "{}", static_cast<std::int64_t>(value));
        } else {
            std::format_to(out_, "{}", static_cast<std::uint64_t>(value));
        }
    }
    void valueNull() {
        std::format_to(out_, "null");
    }
};

template <typename OutputIt>
JsonWriter(OutputIt) -> JsonWriter<OutputIt>;

} // namespace turbofw::detail::sbe
