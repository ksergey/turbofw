// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "EnvironmentImpl.h"

#include <cstdlib>
#include <optional>

#include <turboq/Platform.h>

#include <turbofw/config/ByteSize.h>
#include <turbofw/config/Json.h>

namespace turbofw::detail {

struct QueueEntryConfig {
    std::string name; ///< may contain "${VAR}" entries, expanded and validated on load
    std::string type;
    std::optional<config::ByteSize> capacity;
    std::optional<std::uint64_t> length; ///< for mpsc, slot count (not bytes)
    std::optional<config::ByteSize> slotSize;

    template <typename DTO>
    void serialize(DTO& dto) {
        using namespace std::string_view_literals;

        // clang-format off
        dto
            & config::value<"name">(&name)
            & config::value<"type">(&type).validate(config::oneOf("spsc"sv, "mpsc"sv, "multicast"sv))
            & config::value<"capacity">(&capacity).defaultValue(std::nullopt)
            & config::value<"length">(&length).defaultValue(std::nullopt)
            & config::value<"slot-size">(&slotSize).defaultValue(std::nullopt)
        ;
        // clang-format on
    }
};

struct ServiceConfig {
    std::string name;
    std::uint16_t serviceId;
    std::map<std::string, std::string> queues; ///< role -> queue name (may contain "${VAR}" entries)

    template <typename DTO>
    void serialize(DTO& dto) {
        // clang-format off
        dto
            & config::value<"name">(&name).validate(config::match("[A-Za-z0-9@._-]+"))
            & config::value<"service-id">(&serviceId)
            & config::value<"queues">(&queues)
        ;
        // clang-format on
    }
};

struct EnvironmentConfig {
    std::string scope;
    std::optional<std::filesystem::path> workingDirectory;
    std::optional<std::filesystem::path> memorySource;
    std::vector<std::filesystem::path> searchPath;
    std::vector<QueueEntryConfig> queues;
    std::vector<ServiceConfig> services;

    template <typename DTO>
    void serialize(DTO& dto) {
        // clang-format off
        dto
            & config::value<"scope">(&scope)
            & config::value<"working-directory">(&workingDirectory).defaultValue()
            & config::value<"memory-source">(&memorySource).defaultValue()
            & config::value<"search-path">(&searchPath).defaultValue()
            & config::value<"queues">(&queues).defaultValue()
            & config::value<"services">(&services).defaultValue()
        ;
        // clang-format on
    }
};

namespace {

// Expand "${VAR}" entries in input using @c vars, then process environment variables.
// Throws Exception on unknown variable or bad syntax
[[nodiscard]] auto expandVars(std::string_view input, EnvironmentImpl::Vars const& vars) -> std::string {
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
        } else if (auto const value = ::getenv(std::string{name}.c_str()); value != nullptr) {
            result.append(value);
        } else {
            throw std::runtime_error{fmt::format("unknown variable \"{}\" in \"{}\"", name, input)};
        }
        pos = end + 1;
    }
    return result;
}

} // namespace

auto EnvironmentImpl::loadFromJson(
    std::string_view content, Vars vars) noexcept -> std::expected<EnvironmentImpl, std::string> {
    EnvironmentConfig config;
    if (auto result = config::fromJson(config, content); !result) {
        return std::unexpected{fmt::format("can't load environment from json: {}", result.error())};
    }
    return load(config, std::move(vars));
}

auto EnvironmentImpl::loadFromConfig(
    std::filesystem::path const& configPath, Vars vars) noexcept -> std::expected<EnvironmentImpl, std::string> {
    detail::EnvironmentConfig config;
    if (auto result = config::fromJsonFile(config, configPath); !result) {
        return std::unexpected{
            fmt::format("can't load environment from \"{}\": {}", configPath.string(), result.error())};
    }
    return load(config, std::move(vars));
}

auto EnvironmentImpl::load(
    EnvironmentConfig const& config, Vars vars) noexcept -> std::expected<EnvironmentImpl, std::string> {
    try {
        auto const expandString = [&](std::string_view value) {
            return expandVars(value, vars);
        };
        auto const expandPath = [&](std::filesystem::path const& value) {
            return std::filesystem::path{expandString(value.string())};
        };
        auto const createDirectories = [](std::filesystem::path const& value) {
            std::error_code ec;
            std::filesystem::create_directories(value, ec);
            if (ec) {
                throw std::runtime_error{fmt::format("can't create \"{}\": {}", value.string(), ec.message())};
            }
        };
        auto const makeQueueEntry = [](detail::QueueEntryConfig const& config) -> QueueEntry {
            if (config.type == "multicast") {
                if (!config.capacity.has_value()) {
                    throw std::runtime_error{
                        fmt::format("queue \"{}\": \"capacity\" is required for \"multicast\" queues", config.name)};
                }
                if (config.length.has_value() || config.slotSize.has_value()) {
                    throw std::runtime_error{fmt::format(
                        "queue \"{}\": \"length\" and \"slot-size\" are only valid for \"mpsc\" queues", config.name)};
                }
                if (config.capacity->bytes == 0) {
                    throw std::runtime_error{
                        fmt::format("queue \"{}\": \"capacity\" must be greater than zero", config.name)};
                }
                return QueueEntry{
                    .name = config.name,
                    .type = QueueEntryType::Multicast,
                    .creationOptions{
                        turboq::MulticastMessageQueue::CreationOptions{.capacityHint = config.capacity->bytes}},
                };
            } else if (config.type == "mpsc") {
                if (!config.length.has_value() || !config.slotSize.has_value()) {
                    throw std::runtime_error{fmt::format(
                        "queue \"{}\": \"length\" and \"slot-size\" are required for \"mpsc\" queues", config.name)};
                }
                if (config.capacity.has_value()) {
                    throw std::runtime_error{fmt::format(
                        "queue \"{}\": \"capacity\" is only valid for \"spsc\"/\"multicast\" queues", config.name)};
                }
                if (*config.length == 0) {
                    throw std::runtime_error{
                        fmt::format("queue \"{}\": \"length\" must be greater than zero", config.name)};
                }
                if (config.slotSize->bytes == 0) {
                    throw std::runtime_error{
                        fmt::format("queue \"{}\": \"slot-size\" must be greater than zero", config.name)};
                }
                return QueueEntry{
                    .name = config.name,
                    .type = QueueEntryType::MPSC,
                    .creationOptions{turboq::MPSCMessageQueue::CreationOptions{
                        .slotSizeHint = config.slotSize->bytes, .lengthHint = *config.length}},
                };
            } else if (config.type == "spsc") {
                if (!config.capacity.has_value()) {
                    throw std::runtime_error{
                        fmt::format("queue \"{}\": \"capacity\" is required for \"spsc\" queues", config.name)};
                }
                if (config.length.has_value() || config.slotSize.has_value()) {
                    throw std::runtime_error{fmt::format(
                        "queue \"{}\": \"length\" and \"slot-size\" are only valid for \"mpsc\" queues", config.name)};
                }
                if (config.capacity->bytes == 0) {
                    throw std::runtime_error{
                        fmt::format("queue \"{}\": \"capacity\" must be greater than zero", config.name)};
                }
                return QueueEntry{
                    .name = config.name,
                    .type = QueueEntryType::SPSC,
                    .creationOptions{turboq::SPSCMessageQueue::CreationOptions{.capacityHint = config.capacity->bytes}},
                };
            } else {
                throw std::runtime_error{fmt::format("unknown queue type \"{}\"", config.type)};
            }
        };

        auto scope = expandString(config.scope);
        if (scope.empty() || !std::ranges::all_of(scope, [](char c) {
                return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '@' ||
                       c == '.' || c == '_' || c == '-';
            })) {
            throw std::runtime_error{
                fmt::format("\"scope\" \"{}\" must not be empty and may contain [A-Za-z0-9@._-] only", scope)};
        }

        // Make expanded scope available to the rest of the config as ${SCOPE};
        // it wins over a user-provided "SCOPE" since config is the source of truth
        vars.insert_or_assign("SCOPE", scope);

        // Memory source mount: where the queue files are created
        auto mount = std::optional<std::filesystem::path>{};
        if (config.memorySource.has_value()) {
            mount = expandPath(*config.memorySource);
            if (mount->empty()) {
                throw std::runtime_error{"\"memory-source\" must not be empty"};
            }
            createDirectories(*mount);
        }

        auto env = EnvironmentImpl{std::move(mount)};

        // Scope
        env.scope = std::move(scope);

        // Working directory
        if (config.workingDirectory.has_value()) {
            env.workingDirectory = expandPath(*config.workingDirectory);
            if (env.workingDirectory.empty()) {
                throw std::runtime_error{"\"working-directory\" must not be empty"};
            }
            createDirectories(env.workingDirectory);
        }

        // Search path
        env.searchPath.reserve(config.searchPath.size());
        for (auto const& dir : config.searchPath) {
            auto const expanded = expandPath(dir);
            if (expanded.empty()) {
                throw std::runtime_error{"\"search-path\" entries must not be empty"};
            }
            // missing entries are fine: findFile() just skips them
            env.searchPath.push_back(expanded);
        }

        // Queues
        auto const validateQueueName = [](std::string_view name, std::string_view source) {
            auto const valid = [&] {
                if (name.empty() || name.front() == '/' || name.back() == '/') {
                    return false;
                }
                // no absolute override: "path / absolute-name" would replace the mount path entirely
                if (!std::ranges::all_of(name, [](char c) {
                        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '@' ||
                               c == '.' || c == '_' || c == '-' || c == '/';
                    })) {
                    return false;
                }
                // each '/'-separated part must be a plain component: no "." / ".." (traversal), no "//"
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
        };

        env.queues.reserve(config.queues.size());
        for (auto const& queueConfig : config.queues) {
            auto const name = expandString(queueConfig.name);
            validateQueueName(name, queueConfig.name);
            if (auto const found = std::ranges::find_if(env.queues,
                    [&](auto const& q) {
                        return q.name == name;
                    });
                found != env.queues.end()) {
                throw std::runtime_error{fmt::format("duplicated queue name \"{}\"", name)};
            }
            auto queueEntry = makeQueueEntry(queueConfig);
            queueEntry.name = name;
            env.queues.push_back(std::move(queueEntry));
        }

        // Services
        env.services.reserve(config.services.size());
        for (auto const& serviceConfig : config.services) {
            if (std::ranges::find_if(env.services, [&](auto const& s) {
                    return s.name == serviceConfig.name;
                }) != env.services.end()) {
                throw std::runtime_error{fmt::format("duplicated service name \"{}\"", serviceConfig.name)};
            }
            auto const& queues = serviceConfig.queues;
            if (!queues.contains("cmd") || !queues.contains("data")) {
                throw std::runtime_error{
                    fmt::format("service \"{}\": \"cmd\" and \"data\" queues are required", serviceConfig.name)};
            }
            auto serviceEntry = ServiceEntry{
                .name = serviceConfig.name,
                .serviceId = serviceConfig.serviceId,
                .queues{},
            };
            for (auto const& [role, queueName] : queues) {
                auto const expanded = expandString(queueName);
                validateQueueName(expanded, queueName);
                if (std::ranges::find_if(env.queues, [&](auto const& q) {
                        return q.name == expanded;
                    }) == env.queues.end()) {
                    throw std::runtime_error{
                        fmt::format("service \"{}\": queue \"{}\" (\"{}\") is not defined in \"queues\"",
                            serviceConfig.name, expanded, queueName)};
                }
                serviceEntry.queues.emplace(role, expanded);
            }
            env.services.push_back(std::move(serviceEntry));
        }

        return env;
    } catch (std::exception const& e) {
        return std::unexpected{e.what()};
    }
}

auto format_as(EnvironmentImpl::QueueEntryType type) noexcept -> std::string_view {
    using namespace std::string_view_literals;

    switch (type) {
    case EnvironmentImpl::QueueEntryType::Multicast: return "multicast"sv;
    case EnvironmentImpl::QueueEntryType::MPSC: return "mpsc"sv;
    case EnvironmentImpl::QueueEntryType::SPSC: return "spsc"sv;
    }
    return "unknown"sv;
}

} // namespace turbofw::detail
