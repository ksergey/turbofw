// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "Environment.h"

#include <cerrno>
#include <cstring>
#include <utility>

#include <fmt/format.h>

#include <linux/magic.h>
#include <sys/statfs.h>

#include "config/Exception.h"

namespace turbofw {
namespace {

/// Mount info of a directory
struct MountInfo {
    bool hugepages = false;
    std::size_t pageSize = 0;
};

/// Detect fs type and page size for a directory
auto statMount(std::filesystem::path const& dir) -> MountInfo {
    struct ::statfs stats;
    if (::statfs(dir.c_str(), &stats) != 0) {
        throw config::Exception{"can't get mount info for \"{}\": {}", dir.string(), std::strerror(errno)};
    }
    auto const fsType = static_cast<unsigned long long>(stats.f_type);
    if (fsType == HUGETLBFS_MAGIC) {
        return MountInfo{.hugepages = true, .pageSize = static_cast<std::size_t>(stats.f_bsize)};
    }
    auto const pageSize = stats.f_bsize > 0 ? static_cast<std::size_t>(stats.f_bsize) : 0;
    return MountInfo{.hugepages = false, .pageSize = pageSize};
}

/// Create directories, throws Exception on error
auto createDirectories(std::filesystem::path const& dir) -> void {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        throw config::Exception{"can't create \"{}\": {}", dir.string(), ec.message()};
    }
}

/// Convert config queue type string to enum
auto toQueueType(std::string const& type) -> Environment::QueueType {
    if (type == "spsc") {
        return Environment::QueueType::Spsc;
    }
    if (type == "mpsc") {
        return Environment::QueueType::Mpsc;
    }
    if (type == "multicast") {
        return Environment::QueueType::Multicast;
    }
    throw config::Exception{"unknown queue type \"{}\"", type};
}

/// Validate queue creation options against its type
auto checkQueueOptions(Environment::QueueConfig const& config) -> Environment::QueueEntry {
    auto const type = toQueueType(config.type);
    auto entry = Environment::QueueEntry{
        .name = config.name,
        .type = type,
    };
    switch (type) {
        case Environment::QueueType::Spsc:
        case Environment::QueueType::Multicast:
            if (!config.capacity.has_value()) {
                throw config::Exception{"queue \"{}\": \"capacity\" is required for \"{}\" queues", config.name,
                    config.type};
            }
            if (config.length.has_value() || config.slotSize.has_value()) {
                throw config::Exception{
                    "queue \"{}\": \"length\" and \"slot-size\" are only valid for \"mpsc\" queues", config.name};
            }
            if (config.capacity->bytes == 0) {
                throw config::Exception{"queue \"{}\": \"capacity\" must be greater than zero", config.name};
            }
            entry.capacity = static_cast<std::size_t>(config.capacity->bytes);
            break;
        case Environment::QueueType::Mpsc:
            if (!config.length.has_value() || !config.slotSize.has_value()) {
                throw config::Exception{
                    "queue \"{}\": \"length\" and \"slot-size\" are required for \"mpsc\" queues", config.name};
            }
            if (config.capacity.has_value()) {
                throw config::Exception{"queue \"{}\": \"capacity\" is only valid for \"spsc\"/\"multicast\" queues",
                    config.name};
            }
            if (*config.length == 0) {
                throw config::Exception{"queue \"{}\": \"length\" must be greater than zero", config.name};
            }
            if (config.slotSize->bytes == 0) {
                throw config::Exception{"queue \"{}\": \"slot-size\" must be greater than zero", config.name};
            }
            entry.length = static_cast<std::size_t>(*config.length);
            entry.slotSize = static_cast<std::size_t>(config.slotSize->bytes);
            break;
        default: break;
    }
    return entry;
}

} // namespace

auto Environment::load(std::filesystem::path const& configPath, std::map<std::string, std::string, std::less<>> vars)
    -> std::expected<Environment, std::string> {
    try {
        ScopeConfig config;
        if (auto result = config::fromJsonFile(config, configPath); !result) {
            return std::unexpected{
                fmt::format("can't load environment config from \"{}\": {}", configPath.string(), result.error())};
        }

        // expand scope first, then it is available to the rest of config as ${SCOPE}
        auto const scope = config::expandVars(config.scope, vars);
        auto const validNameChar = [](char c) {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                || c == '@' || c == '.' || c == '_' || c == '-';
        };
        if (scope.empty() || !std::all_of(scope.begin(), scope.end(), validNameChar)) {
            throw config::Exception{"\"scope\" \"{}\" must not be empty and may contain [A-Za-z0-9@._-] only", scope};
        }
        vars.emplace("SCOPE", scope);

        auto const expandString = [&](std::string_view value) {
            return config::expandVars(value, vars);
        };
        auto const expandPath = [&](std::filesystem::path const& value) {
            return std::filesystem::path{expandString(value.string())};
        };

        auto env = Environment{};
        env.scope_ = scope;

        if (config.workingDirectory.has_value()) {
            env.workingDirectory_ = expandPath(*config.workingDirectory);
            if (env.workingDirectory_.empty()) {
                throw config::Exception{"\"working-directory\" must not be empty"};
            }
            createDirectories(env.workingDirectory_);
        }
        env.searchPath_.reserve(config.searchPath.size());
        for (auto const& dir : config.searchPath) {
            auto const expanded = expandPath(dir);
            if (expanded.empty()) {
                throw config::Exception{"\"search-path\" entries must not be empty"};
            }
            env.searchPath_.push_back(expanded);
        }

        // normalize queues: expand is not needed (names are fixed), validate options before any fs side effect
        env.queues_.reserve(config.queues.size());
        for (auto const& queueConfig : config.queues) {
            auto entry = checkQueueOptions(queueConfig);
            auto const it = std::find_if(env.queues_.begin(), env.queues_.end(), [&](QueueEntry const& q) {
                return q.name == entry.name;
            });
            if (it != env.queues_.end()) {
                throw config::Exception{"duplicated queue name \"{}\"", entry.name};
            }
            env.queues_.push_back(std::move(entry));
        }

        // fs side effects come last: config is already fully validated
        if (!env.queues_.empty()) {
            if (config.queuesOpt.base.empty()) {
                throw config::Exception{"queues-opt \"base\" must not be empty"};
            }
            auto const base = expandPath(config.queuesOpt.base);
            createDirectories(base);
            auto const mount = statMount(base);
            if (config.queuesOpt.hugepages && !mount.hugepages) {
                throw config::Exception{"queues-opt \"base\" \"{}\" is not a hugetlbfs mount", base.string()};
            }
            env.memorySource_ = std::make_unique<turboq::DefaultMemorySource>(
                base, mount.pageSize > 0 ? mount.pageSize : turboq::detail::getDefaultPageSize());
        }

        return env;
    } catch (std::exception const& e) {
        return std::unexpected{std::string{e.what()}};
    }
}

} // namespace turbofw
