// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "Exception.h"
#include "ValueBinder.h"

namespace turbofw::config {

template <typename T>
struct JsonMapping;

/// Read config from json file
template <typename T>
[[nodiscard]] auto fromJsonFile(T& value, std::filesystem::path const& path) noexcept -> std::expected<void, std::string> {
    try {
        auto stream = std::ifstream{path};
        if (!stream) {
            throw Exception{"can't open config file \"{}\"", path.string()};
        }
        // ignore_comments: allow "//" comments in config files
        auto const json = nlohmann::json::parse(stream, nullptr, true, true);
        JsonMapping<T>::read(json, value);
    } catch (std::exception const& e) {
        return std::unexpected(e.what());
    }
    return {};
}

/// Read config from json content
template <typename T>
[[nodiscard]] auto fromJson(T& value, std::string_view content) noexcept -> std::expected<void, std::string> {
    try {
        // ignore_comments: allow "//" comments in config files
        auto const json = nlohmann::json::parse(content, nullptr, true, true);
        JsonMapping<T>::read(json, value);
    } catch (std::exception const& e) {
        return std::unexpected(e.what());
    }
    return {};
}

/// Serialize config to json string
template <typename T>
[[nodiscard]] auto toJson(T const& value, std::size_t indent = 2) noexcept -> std::expected<std::string, std::string> {
    try {
        nlohmann::json json;
        JsonMapping<T>::write(json, value);
        return {json.dump(indent)};
    } catch (std::exception const& e) {
        return std::unexpected(e.what());
    }
}

template <typename ValueBinderT>
struct JsonBinder {
    ValueBinderT& binder;

    void from(nlohmann::json const& json) const {
        using ValueT = std::remove_pointer_t<decltype(binder.valuePtr())>;

        if (auto const found = json.find(binder.name()); found != json.end()) {
            JsonMapping<ValueT>::read(found.value(), *binder.valuePtr());
        } else {
            binder.setDefaultValue();
        }

        binder.validate();
    }

    void to(nlohmann::json& json) {
        using ValueT = std::remove_pointer_t<decltype(binder.valuePtr())>;

        // Validate before write
        binder.validate();

        nlohmann::json entry;
        JsonMapping<ValueT>::write(entry, *binder.valuePtr());
        json[binder.name()] = entry;
    }
};

namespace detail {

/// Input adapter, collects known keys and checks the rest after reading
class JsonInput {
private:
    nlohmann::json const& json_;
    std::vector<std::string_view> knownKeys_;

public:
    explicit JsonInput(nlohmann::json const& json) noexcept : json_{json} {}

    /// operator& for serialize(...)
    template <typename ValueBinderT>
    auto operator&(ValueBinderT const& binder) -> JsonInput& {
        knownKeys_.push_back(binder.name());
        JsonBinder{binder}.from(json_);
        return *this;
    }

    /// Check that all keys were consumed by binders, throws on unknown key
    void finish() const {
        for (auto const& [key, value] : json_.items()) {
            if (std::find(knownKeys_.begin(), knownKeys_.end(), key) == knownKeys_.end()) {
                throw Exception{"unknown key \"{}\"", key};
            }
        }
    }
};

/// Output adapter, operator& for serialize(...)
class JsonOutput {
private:
    nlohmann::json& json_;

public:
    explicit JsonOutput(nlohmann::json& json) noexcept : json_{json} {}

    template <typename ValueBinderT>
    auto operator&(ValueBinderT const& binder) -> JsonOutput& {
        JsonBinder{binder}.to(json_);
        return *this;
    }
};

} // namespace detail

template <typename T>
concept SerializeMember = requires(T& a, detail::JsonInput& input, detail::JsonOutput& output) {
    { a.serialize(input) };
    { a.serialize(output) };
};

template <typename T>
concept SerializeFreeFunction = requires(T& a, detail::JsonInput& input, detail::JsonOutput& output) {
    { serialize(a, input) };
    { serialize(a, output) };
};

/// Mapping for T::serialize(DTO&)
template <typename T>
    requires SerializeMember<T>
struct JsonMapping<T> {
    static void read(nlohmann::json const& json, T& value) {
        detail::JsonInput input{json};
        value.serialize(input);
        input.finish();
    }
    static void write(nlohmann::json& json, T const& value) {
        detail::JsonOutput output{json};
        const_cast<T&>(value).serialize(output);
    }
};

/// Mapping for serialize(T&, DTO&)
template <typename T>
    requires SerializeFreeFunction<T>
struct JsonMapping<T> {
    static void read(nlohmann::json const& json, T& value) {
        detail::JsonInput input{json};
        serialize(value, input);
        input.finish();
    }
    static void write(nlohmann::json& json, T const& value) {
        detail::JsonOutput output{json};
        serialize(const_cast<T&>(value), output);
    }
};

/// Mapping for arithmetic types (int, float, etc...)
template <typename T>
    requires std::is_arithmetic_v<T>
struct JsonMapping<T> {
    static void read(nlohmann::json const& json, T& value) {
        value = json.get<T>();
    }
    static void write(nlohmann::json& json, T const& value) {
        json = value;
    }
};

/// Mapping for string
template <>
struct JsonMapping<std::string> {
    static void read(nlohmann::json const& json, std::string& value) {
        value = json.get<std::string>();
    }
    static void write(nlohmann::json& json, std::string const& value) {
        json = value;
    }
};

/// Mapping for filesystem path
template <>
struct JsonMapping<std::filesystem::path> {
    static void read(nlohmann::json const& json, std::filesystem::path& value) {
        value = json.get<std::string>();
    }
    static void write(nlohmann::json& json, std::filesystem::path const& value) {
        json = value.string();
    }
};

/// Mapping for std::vector
template <typename T, typename Allocator>
struct JsonMapping<std::vector<T, Allocator>> {
    static void read(nlohmann::json const& json, std::vector<T, Allocator>& value) {
        value.clear();
        if (json.is_array()) {
            value.reserve(json.size());
            for (auto const& item : json) {
                JsonMapping<T>::read(item, value.emplace_back());
            }
        } else {
            JsonMapping<T>::read(json, value.emplace_back());
        }
    }
    static void write(nlohmann::json& json, std::vector<T, Allocator> const& value) {
        for (auto const& entry : value) {
            nlohmann::json item;
            JsonMapping<T>::write(item, entry);
            json.push_back(item);
        }
    }
};

/// Mapping for std::optional
template <typename T>
struct JsonMapping<std::optional<T>> {
    static void read(nlohmann::json const& json, std::optional<T>& value) {
        if (json.is_null()) {
            value = std::nullopt;
        } else {
            JsonMapping<T>::read(json, value.emplace());
        }
    }
    static void write(nlohmann::json& json, std::optional<T> const& value) {
        if (!value.has_value()) {
            json = nullptr;
        } else {
            JsonMapping<T>::write(json, *value);
        }
    }
};

/// Mapping for std::chrono::duration
template <typename Rep, typename Period>
struct JsonMapping<std::chrono::duration<Rep, Period>> {
    static void read(nlohmann::json const& json, std::chrono::duration<Rep, Period>& value) {
        Rep rep;
        JsonMapping<Rep>::read(json, rep);
        value = std::chrono::duration<Rep, Period>{rep};
    }
    static void write(nlohmann::json& json, std::chrono::duration<Rep, Period> const& value) {
        JsonMapping<Rep>::write(json, value.count());
    }
};

} // namespace turbofw::config
