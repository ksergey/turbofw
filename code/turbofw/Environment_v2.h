// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <expected>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <turboq/MPSCMessageQueue.h>
#include <turboq/MulticastMessageQueue.h>
#include <turboq/SPSCMessageQueue.h>

#include "config/ByteSize.h"

namespace turbofw::v2 {

class Service;

/// Queue roles of every statically configured service
inline constexpr std::string_view kDataQueueRole = "data"; ///< the service publishes here (Multicast)
inline constexpr std::string_view kCmdQueueRole = "cmd";   ///< the service receives requests here (MPSC)

/// A queue definition of the "queues" section, built from json at access time
struct QueueEntry {
    enum class Type {
        Multicast,
        MPSC,
        SPSC,
    };

    // clang-format off
    using CreationOptionsVariant = std::variant<
        turboq::MulticastMessageQueue::CreationOptions,
        turboq::MPSCMessageQueue::CreationOptions,
        turboq::SPSCMessageQueue::CreationOptions
    >;
    // clang-format on

    std::string name; ///< ${VAR} expanded
    Type type;
    CreationOptionsVariant creationOptions;
};

/// fmt::format helper for QueueEntry::Type
[[nodiscard]] constexpr auto format_as(QueueEntry::Type type) noexcept -> std::string_view {
    switch (type) {
    case QueueEntry::Type::Multicast: return "multicast";
    case QueueEntry::Type::MPSC: return "mpsc";
    case QueueEntry::Type::SPSC: return "spsc";
    }
    return "unknown";
}

/// A statically configured service of the "services" section, built from json at access time
struct ServiceEntry {
    std::string name;                          ///< ${VAR} expanded
    std::uint16_t serviceId;                   ///< assigned by configuration
    std::map<std::string, std::string> queues; ///< role -> expanded queue name
};

/// Extra variables for ${VAR} expansion, applied before the process environment
using Vars = std::map<std::string, std::string, std::less<>>;

/// Process environment for a system of services: a json config, expanded at access time.
///
/// Unlike turbofw::Environment (v1), which fully expands and validates the config at load, this
/// one keeps the config as json and reads it on every call: "${VAR}" entries are expanded and
/// values are built into structs at the moment of the call, so variable changes (vars or the
/// process environment) are picked up without a reload.
///
/// Validation happens only at creation: the config must be a json object with a "scope" (non-empty
/// string) section; "queues" (array of entries with a "name" and a known "type") and "services"
/// (array of entries with "name", "service-id" and a "queues" object) are optional: absent = empty.
/// Cross-references are not checked: a service may name a queue the "queues" section does not
/// define, an error surfaces when that queue is accessed.
///
/// The expanded scope is available to the rest of the config as "${SCOPE}"; the expansion
/// sources are (in order): the vars passed to load, "${SCOPE}", the process environment.
///
/// Access errors (an unknown variable, a bad queue name or creation options after the
/// expansion, a bad "service-id") throw std::runtime_error.
///
/// \see Service (Service_v2.h): the per-service view able to create queues
class Environment {
public:
    /// Load environment from a json string in memory
    /// \see the class docblock for what is validated
    [[nodiscard]] static auto loadFromJson(std::string_view content, Vars vars = {}) noexcept
        -> std::expected<Environment, std::string> {
        try {
            // ignore_comments: allow "//" comments in config files
            return Environment{nlohmann::json::parse(content, nullptr, true, true), std::move(vars)};
        } catch (std::exception const& e) {
            return std::unexpected{e.what()};
        }
    }

    /// Load environment from a json config file
    /// \see the class docblock for what is validated
    [[nodiscard]] static auto loadFromFile(std::filesystem::path const& configPath, Vars vars = {}) noexcept
        -> std::expected<Environment, std::string> {
        try {
            auto stream = std::ifstream{configPath};
            if (!stream) {
                throw std::runtime_error{fmt::format("can't open config file \"{}\"", configPath.string())};
            }
            // ignore_comments: allow "//" comments in config files
            return Environment{nlohmann::json::parse(stream, nullptr, true, true), std::move(vars)};
        } catch (std::exception const& e) {
            return std::unexpected{e.what()};
        }
    }

    /// Build from an already parsed json. Throws std::runtime_error when the shape is invalid
    /// \see the class docblock for what is validated
    explicit Environment(nlohmann::json json, Vars vars = {}) : json_(std::move(json)), vars_(std::move(vars)) {
        validateShape(json_);
    }

    /// Config scope: the name of the system/deployment all services of one system belong to;
    /// expanded (and charset-validated) on every call
    [[nodiscard]] auto scope() const -> std::string {
        auto const value = this->expandString(rawScope(), true);
        if (!isScopeName(value)) {
            throw std::runtime_error{
                fmt::format("\"scope\" \"{}\" must not be empty and may contain [A-Za-z0-9@._-] only", value)};
        }
        return value;
    }

    /// Queues configured for the scope: names, types and creation options, read from json and
    /// expanded on every call. Throws std::runtime_error on an access error (see the class docblock)
    [[nodiscard]] auto queues() const -> std::vector<QueueEntry> {
        auto result = std::vector<QueueEntry>{};
        result.reserve(json_.at("queues").size());
        for (auto const& entry : json_.at("queues")) {
            auto const name = this->expandString(entry.at("name").get_ref<std::string const&>());
            validateQueueName(name, entry.at("name").get_ref<std::string const&>());
            result.push_back(QueueEntry{
                .name = name,
                .type = queueTypeOf(entry.at("type").get_ref<std::string const&>()),
                .creationOptions = queueCreationOptions(entry),
            });
        }
        return result;
    }

    /// Services statically configured in the environment, read from json and expanded on every
    /// call. Throws std::runtime_error on an access error (see the class docblock)
    [[nodiscard]] auto services() const -> std::vector<ServiceEntry> {
        auto result = std::vector<ServiceEntry>{};
        result.reserve(json_.at("services").size());
        for (auto const& entry : json_.at("services")) {
            result.push_back(makeServiceEntry(entry));
        }
        return result;
    }

    /// Find a statically configured service by (expanded) name, nullopt when absent.
    /// Throws std::runtime_error on an access error in the matched or earlier entries
    [[nodiscard]] auto findService(std::string_view name) const -> std::optional<ServiceEntry> {
        for (auto const& entry : json_.at("services")) {
            auto const serviceEntry = this->makeServiceEntry(entry);
            if (serviceEntry.name == name) {
                return serviceEntry;
            }
        }
        return std::nullopt;
    }

    /// Expanded "memory-source": where the queue files live; nullopt = the turboq default
    /// (tmpfs scan). Throws std::runtime_error on an access error
    [[nodiscard]] auto memorySourceMount() const -> std::optional<std::filesystem::path> {
        auto const& json = json_;
        if (!json.contains("memory-source")) {
            return std::nullopt;
        }
        auto const mount = this->expandString(json.at("memory-source").get_ref<std::string const&>());
        if (mount.empty()) {
            throw std::runtime_error{"\"memory-source\" must not be empty"};
        }
        return std::filesystem::path{mount};
    }

    /// Create a Service bound to the statically configured service `name`.
    /// Requires Service_v2.h (the return type is incomplete here)
    /// \see Service
    [[nodiscard]] auto service(std::string_view name) const -> Service;

private:
    nlohmann::json json_;
    Vars vars_;

    /// Check the config shape: what the class docblock promises. Throws std::runtime_error
    static void validateShape(nlohmann::json const& json) {
        auto const require = [&](char const* section, auto const& isValid, char const* what) {
            if (!json.contains(section) || !isValid(json.at(section))) {
                throw std::runtime_error{fmt::format("environment config: \"{}\" {}", section, what)};
            }
        };

        if (!json.is_object()) {
            throw std::runtime_error{"environment config must be a json object"};
        }
        require(
            "scope",
            [](nlohmann::json const& value) {
                return value.is_string();
            },
            "must be a string");
        if (json.at("scope").get_ref<std::string const&>().empty()) {
            throw std::runtime_error{"environment config: \"scope\" must not be empty"};
        }

        // "queues" and "services" are optional: absent means empty (like v1)
        auto const optionalArray = [&](char const* section, auto const& isValid, char const* what) {
            if (json.contains(section) && !isValid(json.at(section))) {
                throw std::runtime_error{fmt::format("environment config: \"{}\" {}", section, what)};
            }
        };
        optionalArray(
            "queues",
            [](nlohmann::json const& value) {
                return value.is_array() && std::ranges::all_of(value, isQueueEntryShape);
            },
            "must be an array of {\"name\": string, \"type\": \"multicast\"|\"mpsc\"|\"spsc\", ...} entries");

        optionalArray(
            "services",
            [](nlohmann::json const& value) {
                return value.is_array() && std::ranges::all_of(value, isServiceEntryShape);
            },
            "must be an array of {\"name\": string, \"service-id\": number, \"queues\": {role: string}} entries");

        if (json.contains("memory-source") && !json.at("memory-source").is_string()) {
            throw std::runtime_error{"environment config: \"memory-source\" must be a string"};
        }
    }

    /// {"name": string, "type": one of multicast/mpsc/spsc}
    static auto isQueueEntryShape(nlohmann::json const& entry) -> bool {
        return entry.is_object() && entry.contains("name") && entry.at("name").is_string() && entry.contains("type") &&
               entry.at("type").is_string() &&
               std::ranges::contains(
                   std::array{"multicast", "mpsc", "spsc"}, entry.at("type").get_ref<std::string const&>());
    }

    /// {"name": string, "service-id": number, "queues": {role: string}}
    static auto isServiceEntryShape(nlohmann::json const& entry) -> bool {
        return entry.is_object() && entry.contains("name") && entry.at("name").is_string() &&
               entry.contains("service-id") && entry.at("service-id").is_number() && entry.contains("queues") &&
               entry.at("queues").is_object() &&
               std::ranges::all_of(entry.at("queues"), [](nlohmann::json const& value) {
                   return value.is_string();
               });
    }

    /// Build one ServiceEntry from json, expanding everything. Throws std::runtime_error
    [[nodiscard]] auto makeServiceEntry(nlohmann::json const& entry) const -> ServiceEntry {
        auto const name = entry.at("name").get_ref<std::string const&>();
        auto const serviceId = entry.at("service-id").get<std::int64_t>();
        if (serviceId < 0 || serviceId > 0xFFFF) {
            throw std::runtime_error{
                fmt::format("service \"{}\": \"service-id\" {} is out of [0, 65535]", name, serviceId)};
        }

        auto serviceEntry = ServiceEntry{
            .name = this->expandString(name),
            .serviceId = static_cast<std::uint16_t>(serviceId),
            .queues{},
        };
        for (auto const& [role, queueName] : entry.at("queues").items()) {
            auto const expanded = this->expandString(queueName.get_ref<std::string const&>());
            validateQueueName(expanded, queueName.get_ref<std::string const&>());
            serviceEntry.queues.emplace(role, expanded);
        }
        return serviceEntry;
    }

    /// The raw "scope" value
    [[nodiscard]] auto rawScope() const -> std::string_view {
        return json_.at("scope").get_ref<std::string const&>();
    }

    /// Expand "${VAR}" in input: vars, then "${SCOPE}" (skipped while expanding the scope
    /// itself, so the scope cannot reference itself), then the process environment.
    /// Throws std::runtime_error on bad syntax or an unknown variable
    [[nodiscard]] auto expandString(std::string_view input, bool inScope = false) const -> std::string {
        auto result = std::string{};
        result.reserve(input.size());
        for (std::size_t pos = 0; pos < input.size();) {
            auto const begin = input.find("${", pos);
            if (begin == std::string_view::npos) {
                result.append(input.substr(pos));
                break;
            }
            result.append(input.substr(pos, begin - pos));
            auto const end = input.find('}', begin);
            if (end == std::string_view::npos) {
                throw std::runtime_error{fmt::format("unterminated variable in \"{}\"", input)};
            }
            auto const varName = input.substr(begin + 2, end - (begin + 2));
            if (varName.empty()) {
                throw std::runtime_error{fmt::format("empty variable in \"{}\"", input)};
            }
            if (auto const found = vars_.find(varName); found != vars_.end()) {
                result.append(found->second);
            } else if (!inScope && varName == "SCOPE") {
                result.append(this->expandString(rawScope(), true));
            } else if (auto const value = ::getenv(std::string{varName}.c_str()); value != nullptr) {
                result.append(value);
            } else {
                throw std::runtime_error{fmt::format("unknown variable \"{}\" in \"{}\"", varName, input)};
            }
            pos = end + 1;
        }
        return result;
    }

    /// Queue type by its config name
    [[nodiscard]] static auto queueTypeOf(std::string_view typeName) -> QueueEntry::Type {
        if (typeName == "multicast") {
            return QueueEntry::Type::Multicast;
        }
        if (typeName == "mpsc") {
            return QueueEntry::Type::MPSC;
        }
        return QueueEntry::Type::SPSC;
    }

    /// Creation options of one "queues" entry. Throws std::runtime_error on bad or missing
    /// options: "capacity" (multicast/spsc), "length" and "slot-size" (mpsc), byte sizes as
    /// literals ("64MiB") or bare numbers
    [[nodiscard]] static auto queueCreationOptions(nlohmann::json const& entry) -> QueueEntry::CreationOptionsVariant {
        auto const typeName = entry.at("type").get_ref<std::string const&>();
        auto const capacity = [&]() -> std::optional<std::size_t> {
            if (!entry.contains("capacity")) {
                return std::nullopt;
            }
            return byteSizeOf(entry.at("capacity"), "capacity");
        }();
        auto const length = [&]() -> std::optional<std::uint64_t> {
            if (!entry.contains("length")) {
                return std::nullopt;
            }
            auto const& value = entry.at("length");
            if (!value.is_number_unsigned()) {
                throw std::runtime_error{fmt::format("queue \"{}\": \"length\" must be a number", typeName)};
            }
            return value.get<std::uint64_t>();
        }();
        auto const slotSize = [&]() -> std::optional<std::size_t> {
            if (!entry.contains("slot-size")) {
                return std::nullopt;
            }
            return byteSizeOf(entry.at("slot-size"), "slot-size");
        }();

        if (typeName == "multicast" || typeName == "spsc") {
            if (!capacity.has_value()) {
                throw std::runtime_error{fmt::format("queue \"{}\": \"capacity\" is required", typeName)};
            }
            if (*capacity == 0) {
                throw std::runtime_error{fmt::format("queue \"{}\": \"capacity\" must be greater than zero", typeName)};
            }
            if (length.has_value() || slotSize.has_value()) {
                throw std::runtime_error{fmt::format(
                    "queue \"{}\": \"length\" and \"slot-size\" are only valid for \"mpsc\" queues", typeName)};
            }
            if (typeName == "multicast") {
                return turboq::MulticastMessageQueue::CreationOptions{.capacityHint = *capacity};
            }
            return turboq::SPSCMessageQueue::CreationOptions{.capacityHint = *capacity};
        }

        // mpsc
        if (!length.has_value() || !slotSize.has_value()) {
            throw std::runtime_error{fmt::format("queue \"{}\": \"length\" and \"slot-size\" are required", typeName)};
        }
        if (*length == 0) {
            throw std::runtime_error{fmt::format("queue \"{}\": \"length\" must be greater than zero", typeName)};
        }
        if (*slotSize == 0) {
            throw std::runtime_error{fmt::format("queue \"{}\": \"slot-size\" must be greater than zero", typeName)};
        }
        if (capacity.has_value()) {
            throw std::runtime_error{
                fmt::format("queue \"{}\": \"capacity\" is only valid for \"spsc\"/\"multicast\" queues", typeName)};
        }
        return turboq::MPSCMessageQueue::CreationOptions{.slotSizeHint = *slotSize, .lengthHint = *length};
    }

    /// Byte size from a json value: a string literal ("64MiB") or a bare number (bytes).
    /// Throws std::runtime_error
    [[nodiscard]] static auto byteSizeOf(nlohmann::json const& value, std::string_view field) -> std::size_t {
        if (value.is_number_unsigned()) {
            return value.get<std::size_t>();
        }
        auto const result = config::ByteSize::fromString(value.get_ref<std::string const&>());
        if (!result) {
            throw std::runtime_error{fmt::format("\"{}\": {}", field, result.error())};
        }
        return result->bytes;
    }

    /// Queue name rules (the expanded value): not empty, [A-Za-z0-9@._-/] only, no leading or
    /// trailing '/', no "//", no "." or ".." parts (it is joined with the memory source mount)
    static void validateQueueName(std::string_view name, std::string_view source) {
        auto const valid = [&] {
            if (name.empty() || name.front() == '/' || name.back() == '/') {
                return false;
            }
            if (!std::ranges::all_of(name, [](char c) {
                    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '@' ||
                           c == '.' || c == '_' || c == '-' || c == '/';
                })) {
                return false;
            }
            for (std::size_t pos = 0; pos <= name.size();) {
                auto const slash = name.find('/', pos);
                auto const part = name.substr(pos, (slash == std::string_view::npos ? name.size() : slash) - pos);
                if (part.empty() || part == "." || part == "..") {
                    return false;
                }
                if (slash == std::string_view::npos) {
                    break;
                }
                pos = slash + 1;
            }
            return true;
        }();
        if (!valid) {
            throw std::runtime_error{fmt::format(
                "queue name \"{}\" (\"{}\") is not valid: must not be empty, may contain [A-Za-z0-9@._-/] only "
                "and must not escape the memory source mount",
                name, source)};
        }
    }

    /// Scope name rules: not empty, [A-Za-z0-9@._-] only
    static auto isScopeName(std::string_view name) noexcept -> bool {
        return !name.empty() && std::ranges::all_of(name, [](char c) {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '@' || c == '.' ||
                   c == '_' || c == '-';
        });
    }
};

} // namespace turbofw::v2
