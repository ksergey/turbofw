// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "SystemDefinition.h"

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <turbofw/config/ByteSize.h>

namespace turbofw {

namespace {

// Expand "${VAR}" entries in input using @c vars, then the "${SCOPE}" variable (expanded from
// the config scope), then process environment variables.
// Throws std::runtime_error on unknown variable or bad syntax
[[nodiscard]] auto expandVars(std::string_view input, SystemDefinition::Vars const& vars, std::string_view rawScope,
    bool inScope) -> std::string {
    std::string result;
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
        auto const name = input.substr(begin + 2, end - (begin + 2));
        if (name.empty()) {
            throw std::runtime_error{fmt::format("empty variable in \"{}\"", input)};
        }
        if (auto const found = vars.find(name); found != vars.end()) {
            result.append(found->second);
        } else if (name == "SCOPE") {
            if (inScope) {
                throw std::runtime_error{
                    fmt::format("\"${{SCOPE}}\" cannot be used inside the \"scope\" value \"{}\"", rawScope)};
            }
            result.append(expandVars(rawScope, vars, rawScope, true));
        } else if (auto const value = ::getenv(std::string{name}.c_str()); value != nullptr) {
            result.append(value);
        } else {
            throw std::runtime_error{fmt::format("unknown variable \"{}\" in \"{}\"", name, input)};
        }
        pos = end + 1;
    }
    return result;
}

[[nodiscard]] auto isCharset(std::string_view value, std::predicate<char> auto const& isAllowed) noexcept -> bool {
    return !value.empty() && std::ranges::all_of(value, isAllowed);
}

// A scope name: non-empty, [A-Za-z0-9@._-]
[[nodiscard]] auto isScopeName(std::string_view value) noexcept -> bool {
    return isCharset(value, [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '@' || c == '.' ||
               c == '_' || c == '-';
    });
}

// A service name: non-empty, [A-Za-z0-9@._-]
[[nodiscard]] auto isServiceName(std::string_view value) noexcept -> bool {
    return isScopeName(value);
}

// A queue name: non-empty, no leading/trailing '/', [A-Za-z0-9@._-/], and each '/'-separated part
// is a plain component: no "." / ".." (traversal), no "//"
[[nodiscard]] auto isQueueName(std::string_view name) noexcept -> bool {
    auto const valid = [&] {
        if (name.empty() || name.front() == '/' || name.back() == '/') {
            return false;
        }
        if (!isCharset(name, [](char c) {
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
    return valid;
}

// Expand a queue name and validate it; `source` is the raw value for the error message.
// Throws std::runtime_error
[[nodiscard]] auto expandQueueName(std::string_view raw, auto const& expandVarsRef) -> std::string {
    auto const name = expandVarsRef(raw);
    if (!isQueueName(name)) {
        throw std::runtime_error{
            fmt::format("queue name \"{}\" (\"{}\") is not valid: must not be empty, may contain [A-Za-z0-9@._-/] only "
                        "and must not escape the memory source mount",
                name, raw)};
    }
    return name;
}

// Parse a size literal ("64MiB", bare number of bytes); returns bytes. Throws std::runtime_error
[[nodiscard]] auto parseByteSize(nlohmann::json const& value, std::string_view queueName, std::string_view key)
    -> std::size_t {
    if (!value.is_string()) {
        throw std::runtime_error{
            fmt::format("queue \"{}\": \"{}\" must be a size literal like \"64MiB\"", queueName, key)};
    }
    auto const result = config::ByteSize::fromString(value.get_ref<std::string const&>());
    if (!result) {
        throw std::runtime_error{fmt::format("queue \"{}\": \"{}\": {}", queueName, key, result.error())};
    }
    if (result->bytes == 0) {
        throw std::runtime_error{fmt::format("queue \"{}\": \"{}\" must be greater than zero", queueName, key)};
    }
    return result->bytes;
}

// Read an optional json entry (absent = null)
[[nodiscard]] auto getOptional(nlohmann::json const& json, std::string_view key) noexcept -> nlohmann::json const* {
    auto const found = json.find(key);
    if (found == json.end() || found->is_null()) {
        return nullptr;
    }
    return &*found;
}

// Queue memory source options: "memory-source" object with "path"/"page-size"; absent = defaults
[[nodiscard]] auto parseMemorySourceOptions(nlohmann::json const& entry, std::string_view queueName,
    auto const& expandVarsRef) -> QueueDefinition::MemorySourceOptions {
    auto options = QueueDefinition::MemorySourceOptions{};
    auto const* source = getOptional(entry, "memory-source");
    if (source == nullptr) {
        return options;
    }
    if (!source->is_object()) {
        throw std::runtime_error{
            fmt::format("queue \"{}\": \"memory-source\" must be an object with \"path\"/\"page-size\"", queueName)};
    }
    if (auto const* path = getOptional(*source, "path"); path != nullptr) {
        if (!path->is_string()) {
            throw std::runtime_error{
                fmt::format("queue \"{}\": \"memory-source\" \"path\" must be a string", queueName)};
        }
        options.path = expandVarsRef(path->get_ref<std::string const&>());
        if (options.path.empty()) {
            throw std::runtime_error{
                fmt::format("queue \"{}\": \"memory-source\" \"path\" must not be empty", queueName)};
        }
    }
    if (auto const* pageSize = getOptional(*source, "page-size"); pageSize != nullptr) {
        if (!pageSize->is_number_unsigned()) {
            throw std::runtime_error{
                fmt::format("queue \"{}\": \"memory-source\" \"page-size\" must be a positive integer", queueName)};
        }
        options.pageSize = pageSize->get<std::size_t>();
        if (options.pageSize == 0) {
            throw std::runtime_error{
                fmt::format("queue \"{}\": \"memory-source\" \"page-size\" must be greater than zero", queueName)};
        }
    }
    return options;
}

// Build a queue definition from a "queues" entry. Throws std::runtime_error
[[nodiscard]] auto parseQueueDefinition(nlohmann::json const& entry, auto const& expandVarsRef) -> QueueDefinition {
    if (auto const* name = getOptional(entry, "name"); name == nullptr || !name->is_string()) {
        throw std::runtime_error{"queue entry: \"name\" must be a string"};
    }
    if (auto const* type = getOptional(entry, "type"); type == nullptr || !type->is_string()) {
        throw std::runtime_error{"queue entry: \"type\" must be a string"};
    }

    auto const name = expandQueueName(entry.at("name").get_ref<std::string const&>(), expandVarsRef);

    auto const type = entry.at("type").get_ref<std::string const&>();
    if (type == "multicast") {
        auto const* capacity = getOptional(entry, "capacity");
        if (capacity == nullptr) {
            throw std::runtime_error{
                fmt::format("queue \"{}\": \"capacity\" is required for \"multicast\" queues", name)};
        }
        if (getOptional(entry, "length") != nullptr || getOptional(entry, "slot-size") != nullptr) {
            throw std::runtime_error{
                fmt::format("queue \"{}\": \"length\" and \"slot-size\" are only valid for \"mpsc\" queues", name)};
        }
        return QueueDefinition{
            .name = name,
            .type = QueueDefinition::QueueType::Multicast,
            .creationOptions{turboq::MulticastMessageQueue::CreationOptions{
                .capacityHint = parseByteSize(*capacity, name, "capacity")}},
            .memorySourceOptions = parseMemorySourceOptions(entry, name, expandVarsRef),
        };
    } else if (type == "mpsc") {
        auto const* length = getOptional(entry, "length");
        auto const* slotSize = getOptional(entry, "slot-size");
        if (length == nullptr || slotSize == nullptr) {
            throw std::runtime_error{
                fmt::format("queue \"{}\": \"length\" and \"slot-size\" are required for \"mpsc\" queues", name)};
        }
        if (getOptional(entry, "capacity") != nullptr) {
            throw std::runtime_error{
                fmt::format("queue \"{}\": \"capacity\" is only valid for \"spsc\"/\"multicast\" queues", name)};
        }
        if (!length->is_number_unsigned()) {
            throw std::runtime_error{fmt::format("queue \"{}\": \"length\" must be a positive integer", name)};
        }
        auto const lengthHint = length->get<std::uint64_t>();
        if (lengthHint == 0) {
            throw std::runtime_error{fmt::format("queue \"{}\": \"length\" must be greater than zero", name)};
        }
        return QueueDefinition{
            .name = name,
            .type = QueueDefinition::QueueType::MPSC,
            .creationOptions{turboq::MPSCMessageQueue::CreationOptions{
                .slotSizeHint = parseByteSize(*slotSize, name, "slot-size"), .lengthHint = lengthHint}},
            .memorySourceOptions = parseMemorySourceOptions(entry, name, expandVarsRef),
        };
    } else if (type == "spsc") {
        auto const* capacity = getOptional(entry, "capacity");
        if (capacity == nullptr) {
            throw std::runtime_error{fmt::format("queue \"{}\": \"capacity\" is required for \"spsc\" queues", name)};
        }
        if (getOptional(entry, "length") != nullptr || getOptional(entry, "slot-size") != nullptr) {
            throw std::runtime_error{
                fmt::format("queue \"{}\": \"length\" and \"slot-size\" are only valid for \"mpsc\" queues", name)};
        }
        return QueueDefinition{
            .name = name,
            .type = QueueDefinition::QueueType::SPSC,
            .creationOptions{
                turboq::SPSCMessageQueue::CreationOptions{.capacityHint = parseByteSize(*capacity, name, "capacity")}},
            .memorySourceOptions = parseMemorySourceOptions(entry, name, expandVarsRef),
        };
    }
    throw std::runtime_error{fmt::format("queue \"{}\": unknown type \"{}\"", name, type)};
}

// Check the config shape. Throws std::runtime_error
void validateShape(nlohmann::json const& json) {
    if (!json.is_object()) {
        throw std::runtime_error{"environment config must be a json object"};
    }
    auto const require = [&](std::string_view section, auto const& isValid, std::string_view what) {
        if (json.contains(section) && !isValid(json.at(section))) {
            throw std::runtime_error{fmt::format("environment config: \"{}\" {}", section, what)};
        }
    };

    require(
        "scope",
        [](nlohmann::json const& value) {
            return value.is_string() && !value.get_ref<std::string const&>().empty();
        },
        "must be a non-empty string");
    require(
        "queues",
        [](nlohmann::json const& value) {
            return value.is_array();
        },
        "must be an array");
    require(
        "services",
        [](nlohmann::json const& value) {
            return value.is_array();
        },
        "must be an array");
}

} // namespace

// --------------------------------------------------------------------------------------------

auto SystemDefinition::loadFromJson(std::string_view content, Vars vars) noexcept
    -> std::expected<SystemDefinition, std::string> {
    try {
        // ignore_comments: allow "//" comments in config files
        return SystemDefinition{nlohmann::json::parse(content, nullptr, true, true), std::move(vars)};
    } catch (std::exception const& e) {
        return std::unexpected{fmt::format("can't load environment from json: {}", e.what())};
    }
}

auto SystemDefinition::loadFromFile(std::filesystem::path const& configPath, Vars vars) noexcept
    -> std::expected<SystemDefinition, std::string> {
    try {
        auto stream = std::ifstream{configPath};
        if (!stream) {
            throw std::runtime_error{fmt::format("can't open config file \"{}\"", configPath.string())};
        }
        // ignore_comments: allow "//" comments in config files
        return SystemDefinition{nlohmann::json::parse(stream, nullptr, true, true), std::move(vars)};
    } catch (std::exception const& e) {
        return std::unexpected{fmt::format("can't load environment from \"{}\": {}", configPath.string(), e.what())};
    }
}

SystemDefinition::SystemDefinition(nlohmann::json json, Vars vars) : json_(std::move(json)), vars_(std::move(vars)) {
    validateShape(json_);
}

auto SystemDefinition::scope() const -> std::expected<std::string, std::string> {
    try {
        auto const rawScope = json_.at("scope").get_ref<std::string const&>();
        auto const value = expandVars(rawScope, vars_, rawScope, false);
        if (!isScopeName(value)) {
            throw std::runtime_error{
                fmt::format("\"scope\" \"{}\" must not be empty and may contain [A-Za-z0-9@._-] only", value)};
        }
        return value;
    } catch (std::exception const& e) {
        return std::unexpected{e.what()};
    }
}

namespace {

// The "queues" section reader; shared by queues() and services() (role queues resolve here)
[[nodiscard]] auto parseQueues(nlohmann::json const& json, SystemDefinition::Vars const& vars)
    -> std::vector<QueueDefinition> {
    static auto const emptyArray = nlohmann::json::array();
    auto const& entries = json.contains("queues") ? json.at("queues") : emptyArray;

    auto const rawScope = json.at("scope").get_ref<std::string const&>();
    auto const expandVarsRef = [&](std::string_view input) {
        return expandVars(input, vars, rawScope, false);
    };

    auto result = std::vector<QueueDefinition>{};
    result.reserve(entries.size());
    for (auto const& entry : entries) {
        auto definition = parseQueueDefinition(entry, expandVarsRef);
        if (std::ranges::find_if(result, [&](auto const& q) {
                return q.name == definition.name;
            }) != result.end()) {
            throw std::runtime_error{fmt::format("duplicated queue name \"{}\"", definition.name)};
        }
        result.push_back(std::move(definition));
    }
    return result;
}

// The "services" section reader; role queue names resolve against @c queues
[[nodiscard]] auto parseServices(nlohmann::json const& json, SystemDefinition::Vars const& vars,
    std::vector<QueueDefinition> const& queues) -> std::vector<ServiceDefinition> {
    static auto const emptyArray = nlohmann::json::array();
    auto const& entries = json.contains("services") ? json.at("services") : emptyArray;

    auto const rawScope = json.at("scope").get_ref<std::string const&>();
    auto const expandVarsRef = [&](std::string_view input) {
        return expandVars(input, vars, rawScope, false);
    };

    auto result = std::vector<ServiceDefinition>{};
    result.reserve(entries.size());
    for (auto const& entry : entries) {
        auto const* name = getOptional(entry, "name");
        if (name == nullptr || !name->is_string()) {
            throw std::runtime_error{"service entry: \"name\" must be a string"};
        }
        auto const value = expandVarsRef(name->get_ref<std::string const&>());
        if (!isServiceName(value)) {
            throw std::runtime_error{
                fmt::format("service name \"{}\" must not be empty and may contain [A-Za-z0-9@._-] only", value)};
        }
        if (std::ranges::find_if(result, [&](auto const& s) {
                return s.name == value;
            }) != result.end()) {
            throw std::runtime_error{fmt::format("duplicated service name \"{}\"", value)};
        }

        auto const* serviceId = getOptional(entry, "service-id");
        if (serviceId == nullptr || !serviceId->is_number_unsigned()) {
            throw std::runtime_error{fmt::format("service \"{}\": \"service-id\" must be an unsigned integer", value)};
        }
        auto const rawServiceId = serviceId->get<std::uint64_t>();
        if (rawServiceId > 0xFFFF || rawServiceId == 0xFFFF) {
            throw std::runtime_error{
                fmt::format("service \"{}\": \"service-id\" {} is out of range [0, 65534]", value, rawServiceId)};
        }

        auto const* roleQueues = getOptional(entry, "queues");
        if (roleQueues == nullptr || !roleQueues->is_object()) {
            throw std::runtime_error{fmt::format("service \"{}\": \"queues\" must be an object", value)};
        }
        auto definition = ServiceDefinition{
            .name = value,
            .serviceId = ServiceId{static_cast<std::uint16_t>(rawServiceId)},
            .queues{},
            .workingDirectory{},
        };
        if (auto const* workingDirectory = getOptional(entry, "working-directory"); workingDirectory != nullptr) {
            if (!workingDirectory->is_string()) {
                throw std::runtime_error{fmt::format("service \"{}\": \"working-directory\" must be a string", value)};
            }
            definition.workingDirectory = expandVarsRef(workingDirectory->get_ref<std::string const&>());
            if (definition.workingDirectory.empty()) {
                throw std::runtime_error{fmt::format("service \"{}\": \"working-directory\" must not be empty", value)};
            }
        }

        for (auto const& [role, queueNameRef] : roleQueues->items()) {
            if (role.empty()) {
                throw std::runtime_error{fmt::format("service \"{}\": queue role must not be empty", value)};
            }
            if (!queueNameRef.is_string()) {
                throw std::runtime_error{
                    fmt::format("service \"{}\": \"{}\" queue name must be a string", value, role)};
            }
            auto const queueName = expandQueueName(queueNameRef.get_ref<std::string const&>(), expandVarsRef);
            auto const found = std::ranges::find_if(queues, [&](auto const& q) {
                return q.name == queueName;
            });
            if (found == queues.end()) {
                throw std::runtime_error{
                    fmt::format("service \"{}\": queue \"{}\" (\"{}\") is not defined in \"queues\"", value, queueName,
                        queueNameRef.get_ref<std::string const&>())};
            }
            if (!definition.queues.emplace(role, *found).second) {
                throw std::runtime_error{fmt::format("service \"{}\": duplicated queue role \"{}\"", value, role)};
            }
        }
        result.push_back(std::move(definition));
    }
    return result;
}

} // namespace

auto SystemDefinition::queues() const -> std::expected<std::vector<QueueDefinition>, std::string> {
    try {
        return parseQueues(json_, vars_);
    } catch (std::exception const& e) {
        return std::unexpected{e.what()};
    }
}

auto SystemDefinition::services() const -> std::expected<std::vector<ServiceDefinition>, std::string> {
    try {
        auto const queues = parseQueues(json_, vars_);
        return parseServices(json_, vars_, queues);
    } catch (std::exception const& e) {
        return std::unexpected{e.what()};
    }
}

auto SystemDefinition::findService(std::string_view name) const -> std::expected<ServiceDefinition, std::string> {
    try {
        auto const queues = parseQueues(json_, vars_);
        auto const services = parseServices(json_, vars_, queues);
        if (auto const found = std::ranges::find_if(services,
                [&](auto const& s) {
                    return s.name == name;
                });
            found != services.end()) {
            return *found;
        }
        return std::unexpected{fmt::format("service \"{}\" is not configured in the environment", name)};
    } catch (std::exception const& e) {
        return std::unexpected{e.what()};
    }
}

} // namespace turbofw
