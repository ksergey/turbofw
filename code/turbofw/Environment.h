// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include <turboq/MPSCMessageQueue.h>
#include <turboq/MulticastMessageQueue.h>
#include <turboq/Platform.h>
#include <turboq/SPSCMessageQueue.h>

namespace turbofw {
namespace detail {

struct EnvironmentConfig;

} // namespace detail

/// Process environment for a service: config scope, working directories, shared memory queues
class Environment {
private:
    enum class QueueEntryType {
        Multicast,
        MPSC,
        SPSC,
    };

    struct QueueEntry {
        std::string name;
        QueueEntryType type;
        std::optional<std::size_t> capacity; ///< for spsc/multicast, bytes
        std::optional<std::size_t> length;   ///< for mpsc, slot count
        std::optional<std::size_t> slotSize; ///< for mpsc, slot size in bytes
    };

    std::string scope_;
    std::filesystem::path workingDirectory_;
    std::vector<std::filesystem::path> searchPath_;
    std::vector<QueueEntry> queues_;

public:
    using Vars = std::map<std::string, std::string, std::less<>>;

    /// Load environment from a json string in memory
    /// vars are additional variables for ${VAR} expansion; process environment applies after them
    /// (e.g. {"NAME", serviceName} since the service name is only known at startup)
    [[nodiscard]] static auto loadFromJson(
        std::string_view content, Vars vars = {}) noexcept -> std::expected<Environment, std::string>;

    /// Load environment from a json config file
    /// vars are additional variables for ${VAR} expansion; process environment applies after them.
    /// Scope expansion common: the expanded scope value becomes the ${SCOPE} variable for the rest of the config
    /// (e.g. {"NAME", appName} since service name is only known at startup)
    [[nodiscard]] static auto loadFromConfig(
        std::filesystem::path const& configPath, Vars vars = {}) noexcept -> std::expected<Environment, std::string>;

    [[nodiscard]] auto scope() const noexcept -> std::string const& {
        return scope_;
    }

    /// Working directory, empty when not configured
    [[nodiscard]] auto workingDirectory() const noexcept -> std::filesystem::path const& {
        return workingDirectory_;
    }

    /// Directories to search files from
    [[nodiscard]] auto searchPath() const noexcept -> std::span<std::filesystem::path const> {
        return searchPath_;
    }

    /// Search file relative to search-path, first match wins
    [[nodiscard]] auto findFile(std::filesystem::path const& relative) const -> std::optional<std::filesystem::path> {
        for (auto const& dir : searchPath_) {
            auto const file = dir / relative;
            if (std::filesystem::exists(file)) {
                return file;
            }
        }
        return std::nullopt;
    }

    /// Queue entry introspection
    [[nodiscard]] auto queues() const noexcept -> std::span<QueueEntry const> {
        return queues_;
    }

    /// Open queue/producer/consumer by name; queue type comes from config,
    /// wrong type request is an error.
    /// @param openOnly when true, opens the queue only (fails if not created yet); otherwise opens-or-creates
    ///                 with options from config
    [[nodiscard]] auto getMulticastQueue(std::string_view name,
        bool openOnly) const noexcept -> std::expected<turboq::MulticastMessageQueue, std::string>;

    [[nodiscard]] auto getMPSCQueue(
        std::string_view name, bool openOnly) const noexcept -> std::expected<turboq::MPSCMessageQueue, std::string>;

    [[nodiscard]] auto getSPSCQueue(
        std::string_view name, bool openOnly) const noexcept -> std::expected<turboq::SPSCMessageQueue, std::string>;

    /// Create a producer.
    /// @tparam T is a type derived from the corresponding turboq producer type;
    /// it must be constructible from the base producer instance (a thin wrapper of the tiny impl)
    /// @param openOnly when true, opens the queue only (fails if not created yet); otherwise opens-or-creates
    ///                 with options from config
    template <typename Producer>
        requires std::is_base_of_v<turboq::MulticastMessageQueue::Producer, Producer>
    [[nodiscard]] TURBOQ_FORCE_INLINE auto createProducer(
        std::string_view name, bool openOnly = true) const noexcept -> std::expected<Producer, std::string> {
        return this->getMulticastQueue(name, openOnly).and_then([](auto queue) -> std::expected<Producer, std::string> {
            try {
                return Producer{queue.createProducer()};
            } catch (std::exception const& e) {
                return std::unexpected{e.what()};
            }
        });
    }

    template <typename Consumer>
        requires std::is_base_of_v<turboq::MulticastMessageQueue::Consumer, Consumer>
    [[nodiscard]] TURBOQ_FORCE_INLINE auto createConsumer(
        std::string_view name, bool openOnly = true) const noexcept -> std::expected<Consumer, std::string> {
        return this->getMulticastQueue(name, openOnly).and_then([](auto queue) -> std::expected<Consumer, std::string> {
            try {
                return Consumer{queue.createConsumer()};
            } catch (std::exception const& e) {
                return std::unexpected{e.what()};
            }
        });
    }

    template <typename Producer>
        requires std::is_base_of_v<turboq::MPSCMessageQueue::Producer, Producer>
    [[nodiscard]] TURBOQ_FORCE_INLINE auto createProducer(
        std::string_view name, bool openOnly = true) const noexcept -> std::expected<Producer, std::string> {
        return this->getMPSCQueue(name, openOnly).and_then([](auto queue) -> std::expected<Producer, std::string> {
            try {
                return Producer{queue.createProducer()};
            } catch (std::exception const& e) {
                return std::unexpected{e.what()};
            }
        });
    }

    template <typename Consumer>
        requires std::is_base_of_v<turboq::MPSCMessageQueue::Consumer, Consumer>
    [[nodiscard]] TURBOQ_FORCE_INLINE auto createConsumer(
        std::string_view name, bool openOnly = true) const noexcept -> std::expected<Consumer, std::string> {
        return this->getMPSCQueue(name, openOnly).and_then([](auto queue) -> std::expected<Consumer, std::string> {
            try {
                return Consumer{queue.createConsumer()};
            } catch (std::exception const& e) {
                return std::unexpected{e.what()};
            }
        });
    }

    template <typename Producer>
        requires std::is_base_of_v<turboq::SPSCMessageQueue::Producer, Producer>
    [[nodiscard]] TURBOQ_FORCE_INLINE auto createProducer(
        std::string_view name, bool openOnly = true) const noexcept -> std::expected<Producer, std::string> {
        return this->getSPSCQueue(name, openOnly).and_then([](auto queue) -> std::expected<Producer, std::string> {
            try {
                return Producer{queue.createProducer()};
            } catch (std::exception const& e) {
                return std::unexpected{e.what()};
            }
        });
    }

    template <typename Consumer>
        requires std::is_base_of_v<turboq::SPSCMessageQueue::Consumer, Consumer>
    [[nodiscard]] TURBOQ_FORCE_INLINE auto createConsumer(
        std::string_view name, bool openOnly = true) const noexcept -> std::expected<Consumer, std::string> {
        return this->getSPSCQueue(name, openOnly).and_then([](auto queue) -> std::expected<Consumer, std::string> {
            try {
                return Consumer{queue.createConsumer()};
            } catch (std::exception const& e) {
                return std::unexpected{e.what()};
            }
        });
    }

private:
    /// Build environment from parsed environment config, used by loadFromJson()/loadFromConfig()
    [[nodiscard]] static auto load(
        detail::EnvironmentConfig const& config, Vars vars) noexcept -> std::expected<Environment, std::string>;
};

} // namespace turbofw
