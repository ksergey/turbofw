// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "Environment.h"

#include <cstdlib>

#include <turboq/Platform.h>

#include "config/ByteSize.h"
#include "config/Json.h"

namespace turbofw {
namespace detail {

struct QueueEntryConfig {
    std::string name;
    std::string type;
    std::optional<config::ByteSize> capacity;
    std::optional<std::uint64_t> length; ///< for mpsc, slot count (not bytes)
    std::optional<config::ByteSize> slotSize;

    template <typename DTO>
    void serialize(DTO& dto) {
        using namespace std::string_view_literals;

        // clang-format off
        dto
            & config::value<"name">(&name).validate(config::match("[A-Za-z0-9@._-]+"))
            & config::value<"type">(&type).validate(config::oneOf("spsc"sv, "mpsc"sv, "multicast"sv))
            & config::value<"capacity">(&capacity).defaultValue(std::nullopt)
            & config::value<"length">(&length).defaultValue(std::nullopt)
            & config::value<"slot-size">(&slotSize).defaultValue(std::nullopt)
        ;
        // clang-format on
    }
};

struct EnvironmentConfig {
    std::string scope;
    std::optional<std::filesystem::path> workingDirectory;
    std::vector<std::filesystem::path> searchPath;
    std::vector<QueueEntryConfig> queues;

    template <typename DTO>
    void serialize(DTO& dto) {
        // clang-format off
        dto
            & config::value<"scope">(&scope)
            & config::value<"working-directory">(&workingDirectory).defaultValue()
            & config::value<"search-path">(&searchPath).defaultValue()
            & config::value<"queues">(&queues).defaultValue()
        ;
        // clang-format on
    }
};

} // namespace detail

namespace {

// Expand "${VAR}" entries in input using @c vars, then process environment variables.
// Throws Exception on unknown variable or bad syntax
[[nodiscard]] auto expandVars(std::string_view input, Environment::Vars const& vars) -> std::string {
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
        } else if (auto const value = ::getenv(std::string(name).c_str()); value != nullptr) {
            result.append(value);
        } else {
            throw std::runtime_error{fmt::format("unknown variable \"{}\" in \"{}\"", name, input)};
        }
        pos = end + 1;
    }
    return result;
}

} // namespace

auto Environment::load(
    detail::EnvironmentConfig const& config, Vars vars) noexcept -> std::expected<Environment, std::string> {
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
                    .capacity = config.capacity->bytes,
                    .length = std::nullopt,
                    .slotSize = std::nullopt,
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
                    .capacity = std::nullopt,
                    .length = *config.length,
                    .slotSize = config.slotSize->bytes,
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
                    .capacity = config.capacity->bytes,
                    .length = std::nullopt,
                    .slotSize = std::nullopt,
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

        auto env = Environment{};

        // Scope
        env.scope_ = std::move(scope);

        // Working directory
        if (config.workingDirectory.has_value()) {
            env.workingDirectory_ = expandPath(*config.workingDirectory);
            if (env.workingDirectory_.empty()) {
                throw std::runtime_error{"\"working-directory\" must not be empty"};
            }
            createDirectories(env.workingDirectory_);
        }

        // Search path
        env.searchPath_.reserve(config.searchPath.size());
        for (auto const& dir : config.searchPath) {
            auto const expanded = expandPath(dir);
            if (expanded.empty()) {
                throw std::runtime_error{"\"search-path\" entries must not be empty"};
            }
            // missing entries are fine: findFile() just skips them
            env.searchPath_.push_back(expanded);
        }

        // Queues
        env.queues_.reserve(config.queues.size());
        for (auto const& queueConfig : config.queues) {
            if (auto const found = std::ranges::find_if(env.queues_,
                    [&](auto const& q) {
                        return q.name == queueConfig.name;
                    });
                found != env.queues_.end()) {
                throw std::runtime_error{fmt::format("duplicated queue name \"{}\"", queueConfig.name)};
            }
            env.queues_.push_back(makeQueueEntry(queueConfig));
        }

        return env;
    } catch (std::exception const& e) {
        return std::unexpected{e.what()};
    }
}

auto Environment::loadFromJson(
    std::string_view content, Vars vars) noexcept -> std::expected<Environment, std::string> {
    detail::EnvironmentConfig config;
    if (auto result = config::fromJson(config, content); !result) {
        return std::unexpected{fmt::format("can't load environment fron json: {}", result.error())};
    }
    return load(config, std::move(vars));
}

auto Environment::loadFromConfig(
    std::filesystem::path const& configPath, Vars vars) noexcept -> std::expected<Environment, std::string> {
    detail::EnvironmentConfig config;
    if (auto result = config::fromJsonFile(config, configPath); !result) {
        return std::unexpected{
            fmt::format("can't load environment from \"{}\": {}", configPath.string(), result.error())};
    }
    return load(config, std::move(vars));
}

auto Environment::getMulticastQueueCached(
    std::string_view name, bool openOnly, std::string& error) const -> turboq::MulticastMessageQueue* {
    try {
        auto const found = std::ranges::find_if(queues_, [&](auto const& entry) {
            return entry.name == name;
        });
        if (found == queues_.end()) {
            error = fmt::format("unknown queue \"{}\"", name);
            return nullptr;
        }
        if (found->type != QueueEntryType::Multicast) {
            error = fmt::format("queue \"{}\" is not multicast queue", name);
            return nullptr;
        }

        auto& entry = openQueues_[std::string{name}];
        if (entry.multicast.has_value()) {
            return &*entry.multicast;
        }
        auto queue = (openOnly ? turboq::MulticastMessageQueue::makeQueue(name)
                          : turboq::MulticastMessageQueue::makeQueue(
                                name, turboq::MulticastMessageQueue::CreationOptions{
                                          .capacityHint = *found->capacity}))
                         .transform_error([](std::error_code const& ec) {
                             return ec.message();
                         });
        if (!queue) {
            error = std::move(queue).error();
            return nullptr;
        }
        entry = OpenQueue{};
        entry.multicast.emplace(std::move(*queue));
        return &*entry.multicast;
    } catch (std::exception const& e) {
        error = e.what();
        return nullptr;
    }
}

auto Environment::getMPSCQueueCached(
    std::string_view name, bool openOnly, std::string& error) const -> turboq::MPSCMessageQueue* {
    try {
        auto const found = std::ranges::find_if(queues_, [&](auto const& entry) {
            return entry.name == name;
        });
        if (found == queues_.end()) {
            error = fmt::format("unknown queue \"{}\"", name);
            return nullptr;
        }
        if (found->type != QueueEntryType::MPSC) {
            error = fmt::format("queue \"{}\" is not mpsc queue", name);
            return nullptr;
        }

        auto& entry = openQueues_[std::string{name}];
        if (entry.mpsc.has_value()) {
            return &*entry.mpsc;
        }
        auto queue = (openOnly ? turboq::MPSCMessageQueue::makeQueue(name)
                          : turboq::MPSCMessageQueue::makeQueue(
                                name, turboq::MPSCMessageQueue::CreationOptions{
                                          .slotSizeHint = *found->slotSize,
                                          .lengthHint = *found->length}))
                         .transform_error([](std::error_code const& ec) {
                             return ec.message();
                         });
        if (!queue) {
            error = std::move(queue).error();
            return nullptr;
        }
        entry = OpenQueue{};
        entry.mpsc.emplace(std::move(*queue));
        return &*entry.mpsc;
    } catch (std::exception const& e) {
        error = e.what();
        return nullptr;
    }
}

auto Environment::getSPSCQueueCached(
    std::string_view name, bool openOnly, std::string& error) const -> turboq::SPSCMessageQueue* {
    try {
        auto const found = std::ranges::find_if(queues_, [&](auto const& entry) {
            return entry.name == name;
        });
        if (found == queues_.end()) {
            error = fmt::format("unknown queue \"{}\"", name);
            return nullptr;
        }
        if (found->type != QueueEntryType::SPSC) {
            error = fmt::format("queue \"{}\" is not spsc queue", name);
            return nullptr;
        }

        auto& entry = openQueues_[std::string{name}];
        if (entry.spsc.has_value()) {
            return &*entry.spsc;
        }
        auto queue = (openOnly ? turboq::SPSCMessageQueue::makeQueue(name)
                          : turboq::SPSCMessageQueue::makeQueue(
                                name, turboq::SPSCMessageQueue::CreationOptions{
                                          .capacityHint = *found->capacity}))
                         .transform_error([](std::error_code const& ec) {
                             return ec.message();
                         });
        if (!queue) {
            error = std::move(queue).error();
            return nullptr;
        }
        entry = OpenQueue{};
        entry.spsc.emplace(std::move(*queue));
        return &*entry.spsc;
    } catch (std::exception const& e) {
        error = e.what();
        return nullptr;
    }
}

auto Environment::getMulticastQueue(
    std::string_view name, bool openOnly) const noexcept -> std::expected<turboq::MulticastMessageQueue, std::string> {
    auto error = std::string{};
    if (this->getMulticastQueueCached(name, openOnly, error) == nullptr) {
        return std::unexpected{std::move(error)};
    }
    // The environment keeps the queue itself open (with the file locks); return an independent handle
    return turboq::MulticastMessageQueue::makeQueue(name).transform_error([](std::error_code const& ec) {
        return ec.message();
    });
}

auto Environment::getMPSCQueue(
    std::string_view name, bool openOnly) const noexcept -> std::expected<turboq::MPSCMessageQueue, std::string> {
    auto error = std::string{};
    if (this->getMPSCQueueCached(name, openOnly, error) == nullptr) {
        return std::unexpected{std::move(error)};
    }
    // The environment keeps the queue itself open (with the file locks); return an independent handle
    return turboq::MPSCMessageQueue::makeQueue(name).transform_error([](std::error_code const& ec) {
        return ec.message();
    });
}

auto Environment::getSPSCQueue(
    std::string_view name, bool openOnly) const noexcept -> std::expected<turboq::SPSCMessageQueue, std::string> {
    auto error = std::string{};
    if (this->getSPSCQueueCached(name, openOnly, error) == nullptr) {
        return std::unexpected{std::move(error)};
    }
    // The environment keeps the queue itself open (with the file locks); return an independent handle
    return turboq::SPSCMessageQueue::makeQueue(name).transform_error([](std::error_code const& ec) {
        return ec.message();
    });
}

} // namespace turbofw
