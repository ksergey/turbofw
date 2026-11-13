// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <ranges>

#include <turboq-logger/Logging.h>
#include <turboq/MPSCMessageQueue.h>
#include <turboq/MulticastMessageQueue.h>
#include <turboq/SPSCMessageQueue.h>

#include "detail/EnvironmentImpl.h"

namespace turbofw {

/// Process environment for a service: config scope, working directories, shared memory queues
class Environment {
private:
    using EnvironmentImpl = detail::EnvironmentImpl;

    EnvironmentImpl impl_;

    explicit Environment(EnvironmentImpl&& impl) noexcept : impl_{std::move(impl)} {}

public:
    /// Extra variables for ${VAR} expansion, applied before the process environment
    using Vars = EnvironmentImpl::Vars;

    /// Queue definition
    using QueueEntry = EnvironmentImpl::QueueEntry;

    /// Statically configured service: identity and its queues by role ("cmd"/"data" required)
    using ServiceEntry = EnvironmentImpl::ServiceEntry;

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

    /// Config scope name: the name of the system/deployment the service belongs to; all services
    /// of one system share it ([A-Za-z0-9@._-] only). The expanded scope value is available to the
    /// rest of the config as ${SCOPE}
    [[nodiscard]] auto scope() const noexcept -> std::string const& {
        return impl_.scope;
    }

    /// Working directory, empty when not configured
    [[nodiscard]] auto workingDirectory() const noexcept -> std::filesystem::path const& {
        return impl_.workingDirectory;
    }

    /// Directories to search files from, in config order
    [[nodiscard]] auto searchPath() const noexcept -> std::span<std::filesystem::path const> {
        return impl_.searchPath;
    }

    /// Search a file relative to search-path entries, first match wins.
    /// Returns std::nullopt when the file is not found in any search-path entry
    [[nodiscard]] auto findFile(std::filesystem::path const& relative) const -> std::optional<std::filesystem::path>;

    /// Queues configured for the scope: names, types and creation options (introspection)
    /// \see EnvironmentImpl::QueueEntry
    [[nodiscard]] auto queues() const noexcept -> std::span<QueueEntry const> {
        return std::span{impl_.queues};
    }

    /// Services statically configured in the environment ("services" section): identity and
    /// queue names by role, empty when the section is absent
    /// \see EnvironmentImpl::ServiceEntry
    [[nodiscard]] auto services() const noexcept -> std::span<ServiceEntry const> {
        return std::span{impl_.services};
    }

    /// Find a statically configured service by name, nullptr when absent
    [[nodiscard]] auto findService(std::string_view name) const noexcept -> ServiceEntry const* {
        auto const found = std::ranges::find_if(impl_.services, [&](auto const& service) {
            return service.name == name;
        });
        return found != impl_.services.end() ? &*found : nullptr;
    }

    /// Create a producer (opens-or-creates the queue first).
    /// @tparam Producer is a type derived from the corresponding turboq producer type;
    /// it must be constructible from the base producer instance (a thin wrapper of the tiny impl)
    /// @param openOnly when true, opens the queue only (fails if not created yet); otherwise opens-or-creates
    ///                 with options from config.
    ///                 The queue (and its single-producer lock) stays open for the environment lifetime, which
    ///                 is what makes the single-producer guarantee hold; the environment must outlive the producer
    template <typename Producer>
        requires std::is_base_of_v<turboq::MulticastMessageQueue::Producer, Producer>
    [[nodiscard]] TURBOQ_FORCE_INLINE auto createProducer(
        std::string_view name, bool openOnly = true) const noexcept -> std::expected<Producer, std::string> {
        try {
            return impl_.getQueue<turboq::MulticastMessageQueue>(name, openOnly)
                .transform([](turboq::MulticastMessageQueue queue) {
                    return Producer{queue.createProducer()};
                });
        } catch (std::exception const& e) {
            return std::unexpected{e.what()};
        }
    }

    /// Create a multicast consumer, see createProducer() for template/param docs
    template <typename Consumer>
        requires std::is_base_of_v<turboq::MulticastMessageQueue::Consumer, Consumer>
    [[nodiscard]] TURBOQ_FORCE_INLINE auto createConsumer(
        std::string_view name, bool openOnly = true) const noexcept -> std::expected<Consumer, std::string> {
        try {
            return impl_.getQueue<turboq::MulticastMessageQueue>(name, openOnly)
                .transform([](turboq::MulticastMessageQueue queue) {
                    return Consumer{queue.createConsumer()};
                });
        } catch (std::exception const& e) {
            return std::unexpected{e.what()};
        }
    }

    /// Create an mpsc producer, see createProducer() for template/param docs
    template <typename Producer>
        requires std::is_base_of_v<turboq::MPSCMessageQueue::Producer, Producer>
    [[nodiscard]] TURBOQ_FORCE_INLINE auto createProducer(
        std::string_view name, bool openOnly = true) const noexcept -> std::expected<Producer, std::string> {
        try {
            return impl_.getQueue<turboq::MPSCMessageQueue>(name, openOnly)
                .transform([](turboq::MPSCMessageQueue queue) {
                    return Producer{queue.createProducer()};
                });
        } catch (std::exception const& e) {
            return std::unexpected{e.what()};
        }
    }

    /// Create the (single) mpsc consumer, see createProducer() for template/param docs
    template <typename Consumer>
        requires std::is_base_of_v<turboq::MPSCMessageQueue::Consumer, Consumer>
    [[nodiscard]] TURBOQ_FORCE_INLINE auto createConsumer(
        std::string_view name, bool openOnly = true) const noexcept -> std::expected<Consumer, std::string> {
        try {
            return impl_.getQueue<turboq::MPSCMessageQueue>(name, openOnly)
                .transform([](turboq::MPSCMessageQueue queue) {
                    return Consumer{queue.createConsumer()};
                });
        } catch (std::exception const& e) {
            return std::unexpected{e.what()};
        }
    }

    /// Create an spsc producer, see createProducer() for template/param docs
    template <typename Producer>
        requires std::is_base_of_v<turboq::SPSCMessageQueue::Producer, Producer>
    [[nodiscard]] TURBOQ_FORCE_INLINE auto createProducer(
        std::string_view name, bool openOnly = true) const noexcept -> std::expected<Producer, std::string> {
        try {
            return impl_.getQueue<turboq::SPSCMessageQueue>(name, openOnly)
                .transform([](turboq::SPSCMessageQueue queue) {
                    return Producer{queue.createProducer()};
                });
        } catch (std::exception const& e) {
            return std::unexpected{e.what()};
        }
    }

    /// Create the (single) spsc consumer, see createProducer() for template/param docs
    template <typename Consumer>
        requires std::is_base_of_v<turboq::SPSCMessageQueue::Consumer, Consumer>
    [[nodiscard]] TURBOQ_FORCE_INLINE auto createConsumer(
        std::string_view name, bool openOnly = true) const noexcept -> std::expected<Consumer, std::string> {
        try {
            return impl_.getQueue<turboq::SPSCMessageQueue>(name, openOnly)
                .transform([](turboq::SPSCMessageQueue queue) {
                    return Consumer{queue.createConsumer()};
                });
        } catch (std::exception const& e) {
            return std::unexpected{e.what()};
        }
    }

    void initLogger();
    void shutdownLogger();
};

} // namespace turbofw
