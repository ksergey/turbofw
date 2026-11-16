// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <expected>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

#include <turboq/MPSCMessageQueue.h>
#include <turboq/MulticastMessageQueue.h>
#include <turboq/Platform.h>
#include <turboq/SPSCMessageQueue.h>

#include "SystemDefinition.h"

namespace turbofw {

/// Runtime environment of one service, built from its \c turbofw::ServiceDefinition
/// (typically from turbofw::SystemDefinition, the json config layer).
///
/// It owns the service definition (already expanded) and creates the service queues. Queue roles are arbitrary
/// ("data"/"cmd" by convention, \see AGENTS.md): the queue type of a role comes from the definition and must match the
/// producer/consumer wrapper type.
///
///     auto envResult = Environment::create(serviceDefinition);
///     auto producerResult = envResult->createProducer<MyProducer>("data");
///     auto consumerResult = envResult->createConsumer<MyConsumer>("cmd");
///
/// A wrapper type derives from the corresponding turboq producer/consumer type and must be
/// constructible from the base instance (a thin wrapper of the tiny impl), e.g.
/// \code{.cpp}
/// struct MyProducer : turboq::MulticastMessageQueue::Producer {
///     explicit MyProducer(turboq::MulticastMessageQueue::Producer&& base)
///         : turboq::MulticastMessageQueue::Producer(std::move(base)) {}
///     // mixins and message code here
/// };
/// \endcode
///
class Environment {
public:
    /// Validate the service definition (names, roles, creation options, memory source options)
    /// and construct the environment; a message describes the first invalid entry
    [[nodiscard]] static auto create(turbofw::ServiceDefinition definition) noexcept
        -> std::expected<Environment, std::string>;

    [[nodiscard]] auto name() const noexcept -> std::string const& {
        return definition_.name;
    }

    /// \see turbofw::ServiceId
    [[nodiscard]] auto serviceId() const noexcept -> ServiceId {
        return definition_.serviceId;
    }

    /// The service queues by role
    [[nodiscard]] auto queues() const noexcept -> std::map<std::string, turbofw::QueueDefinition> const& {
        return definition_.queues;
    }

    /// The service working directory, empty when not set (created by the caller, not here)
    [[nodiscard]] auto workingDirectory() const noexcept -> std::filesystem::path const& {
        return definition_.workingDirectory;
    }

    /// Create a multicast producer (opens-or-creates the role queue first).
    /// @tparam Producer is a type derived from the corresponding turboq producer type;
    /// it must be constructible from the base producer instance (a thin wrapper of the tiny impl),
    /// \see the class docblock for an example
    /// @param openOnly when true, opens the queue only (fails if not created yet), without
    ///                 creating the memory source directories; otherwise opens-or-creates with
    ///                 the creation options from the definition.
    ///                 The queue (and its single-producer lock) stays open for the environment lifetime, which
    ///                 is what makes the single-producer guarantee hold; the environment must outlive the producer
    template <typename Producer>
        requires std::is_base_of_v<turboq::MulticastMessageQueue::Producer, Producer>
    [[nodiscard]] TURBOQ_FORCE_INLINE auto createProducer(std::string_view role, bool openOnly = true) const noexcept
        -> std::expected<Producer, std::string> {
        try {
            return getQueue<turboq::MulticastMessageQueue>(role, openOnly)
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
    [[nodiscard]] TURBOQ_FORCE_INLINE auto createConsumer(std::string_view role, bool openOnly = true) const noexcept
        -> std::expected<Consumer, std::string> {
        try {
            return getQueue<turboq::MulticastMessageQueue>(role, openOnly)
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
    [[nodiscard]] TURBOQ_FORCE_INLINE auto createProducer(std::string_view role, bool openOnly = true) const noexcept
        -> std::expected<Producer, std::string> {
        try {
            return getQueue<turboq::MPSCMessageQueue>(role, openOnly).transform([](turboq::MPSCMessageQueue queue) {
                return Producer{queue.createProducer()};
            });
        } catch (std::exception const& e) {
            return std::unexpected{e.what()};
        }
    }

    /// Create the (single) mpsc consumer, see createProducer() for template/param docs
    template <typename Consumer>
        requires std::is_base_of_v<turboq::MPSCMessageQueue::Consumer, Consumer>
    [[nodiscard]] TURBOQ_FORCE_INLINE auto createConsumer(std::string_view role, bool openOnly = true) const noexcept
        -> std::expected<Consumer, std::string> {
        try {
            return getQueue<turboq::MPSCMessageQueue>(role, openOnly).transform([](turboq::MPSCMessageQueue queue) {
                return Consumer{queue.createConsumer()};
            });
        } catch (std::exception const& e) {
            return std::unexpected{e.what()};
        }
    }

    /// Create an spsc producer, see createProducer() for template/param docs
    template <typename Producer>
        requires std::is_base_of_v<turboq::SPSCMessageQueue::Producer, Producer>
    [[nodiscard]] TURBOQ_FORCE_INLINE auto createProducer(std::string_view role, bool openOnly = true) const noexcept
        -> std::expected<Producer, std::string> {
        try {
            return getQueue<turboq::SPSCMessageQueue>(role, openOnly).transform([](turboq::SPSCMessageQueue queue) {
                return Producer{queue.createProducer()};
            });
        } catch (std::exception const& e) {
            return std::unexpected{e.what()};
        }
    }

    /// Create the (single) spsc consumer, see createProducer() for template/param docs
    template <typename Consumer>
        requires std::is_base_of_v<turboq::SPSCMessageQueue::Consumer, Consumer>
    [[nodiscard]] TURBOQ_FORCE_INLINE auto createConsumer(std::string_view role, bool openOnly = true) const noexcept
        -> std::expected<Consumer, std::string> {
        try {
            return getQueue<turboq::SPSCMessageQueue>(role, openOnly).transform([](turboq::SPSCMessageQueue queue) {
                return Consumer{queue.createConsumer()};
            });
        } catch (std::exception const& e) {
            return std::unexpected{e.what()};
        }
    }

private:
    /// Takes an already validated definition
    explicit Environment(turbofw::ServiceDefinition definition) noexcept : definition_{std::move(definition)} {}

    /// Open (or open only) the role queue of the given family; the role queue type must match
    template <typename Q>
        requires std::is_same_v<Q, turboq::MulticastMessageQueue> || std::is_same_v<Q, turboq::MPSCMessageQueue> ||
                 std::is_same_v<Q, turboq::SPSCMessageQueue>
    [[nodiscard]] auto getQueue(std::string_view role, bool openOnly) const noexcept -> std::expected<Q, std::string> {
        constexpr auto queueType = [] {
            if constexpr (std::is_same_v<Q, turboq::MulticastMessageQueue>) {
                return turbofw::QueueType::Multicast;
            } else if constexpr (std::is_same_v<Q, turboq::MPSCMessageQueue>) {
                return turbofw::QueueType::MPSC;
            } else if constexpr (std::is_same_v<Q, turboq::SPSCMessageQueue>) {
                return turbofw::QueueType::SPSC;
            }
        }();

        auto const* definition = findRoleQueue(role);
        if (definition == nullptr) {
            return std::unexpected{fmt::format("{} queue (role \"{}\") not found", queueType, role)};
        }
        if (definition->type != queueType) {
            return std::unexpected{
                fmt::format("{} queue \"{}\" (role \"{}\") not found", queueType, definition->name, role)};
        }

        auto const source = makeMemorySource(*definition, !openOnly);
        if (!source) {
            return std::unexpected{std::move(source).error()};
        }

        auto const createQueue = [&] {
            if (openOnly) {
                return Q::makeQueue(definition->name, *source);
            } else {
                return Q::makeQueue(
                    definition->name, *std::get_if<typename Q::CreationOptions>(&definition->creationOptions), *source);
            }
        };

        return createQueue().transform_error([&](std::error_code const& ec) {
            return fmt::format("failed to get {} queue \"{}\" ({})", queueType, definition->name, ec.message());
        });
    }

    /// The role queue definition, nullptr when the role is not configured
    [[nodiscard]] auto findRoleQueue(std::string_view role) const noexcept -> turbofw::QueueDefinition const*;

    /// Build the memory source of a queue definition; when @c createDirectories, missing source
    /// directories are created first
    [[nodiscard]] auto makeMemorySource(turbofw::QueueDefinition const& definition, bool createDirectories) const
        -> std::expected<turboq::DefaultMemorySource, std::string>;

    turbofw::ServiceDefinition definition_;
};

} // namespace turbofw
