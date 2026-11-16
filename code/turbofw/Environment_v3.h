// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <expected>
#include <map>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <turboq/MPSCMessageQueue.h>
#include <turboq/MulticastMessageQueue.h>
#include <turboq/SPSCMessageQueue.h>

#include "SystemDefinition.h"

namespace turbofw::v3 {

/// Runtime environment of one service, built from its \c turbofw::ServiceDefinition.
///
/// Unlike turbofw::SystemDefinition (the json config layer), this one is constructed
/// programmatically: it owns the service definition (already expanded) and creates the service
/// queues. Queue roles are arbitrary ("data"/"cmd" by convention, \see AGENTS.md): the queue
/// type of a role comes from the definition and must match the producer/consumer wrapper type.
///
///     auto envResult = v3::Environment::create(serviceDefinition);
///     auto producerResult = envResult->createProducer<MyProducer>("data");
///     auto consumerResult = envResult->createConsumer<MyConsumer>("cmd");
///
/// A wrapper type derives from the corresponding turboq producer/consumer type and must be
/// constructible from the base instance (a thin wrapper of the tiny impl), e.g.
/// \code{.cpp}
/// struct MyProducer : turboq::MulticastMessageQueue::Producer {
///     using turboq::MulticastMessageQueue::Producer::Producer;
///     // mixins and message code here
/// };
/// \endcode
///
/// \see turbofw::Environment (v1): createProducer()/createConsumer() docblocks for the queue
/// lifetime contract
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

    /// Create (or open only) the role queue and wrap its producer into @c Producer.
    /// @param openOnly when true, opens the queue only (fails if not created yet), without
    ///                 creating the memory source directories; otherwise opens-or-creates with
    ///                 the creation options from the definition
    template <typename Producer>
        requires std::is_base_of_v<turboq::MulticastMessageQueue::Producer, Producer> ||
                 std::is_base_of_v<turboq::MPSCMessageQueue::Producer, Producer> ||
                 std::is_base_of_v<turboq::SPSCMessageQueue::Producer, Producer>
    [[nodiscard]] auto createProducer(std::string_view role, bool openOnly = false) const noexcept
        -> std::expected<Producer, std::string> {
        auto const* definition = findRoleQueue(role);
        if (definition == nullptr) {
            return std::unexpected{
                fmt::format("role \"{}\" is not configured for service \"{}\"", role, definition_.name)};
        }
        if constexpr (std::is_base_of_v<turboq::MulticastMessageQueue::Producer, Producer>) {
            if (definition->type != turbofw::QueueType::Multicast) {
                return std::unexpected{
                    fmt::format("queue \"{}\" (role \"{}\") is not a multicast queue", definition->name, role)};
            }
            return openQueue<turboq::MulticastMessageQueue, Producer, true>(*definition, openOnly);
        } else if constexpr (std::is_base_of_v<turboq::MPSCMessageQueue::Producer, Producer>) {
            if (definition->type != turbofw::QueueType::MPSC) {
                return std::unexpected{
                    fmt::format("queue \"{}\" (role \"{}\") is not an mpsc queue", definition->name, role)};
            }
            return openQueue<turboq::MPSCMessageQueue, Producer, true>(*definition, openOnly);
        } else {
            if (definition->type != turbofw::QueueType::SPSC) {
                return std::unexpected{
                    fmt::format("queue \"{}\" (role \"{}\") is not an spsc queue", definition->name, role)};
            }
            return openQueue<turboq::SPSCMessageQueue, Producer, true>(*definition, openOnly);
        }
    }

    /// Create (or open only) the role queue and wrap its consumer into @c Consumer
    /// \see createProducer() for the parameters
    template <typename Consumer>
        requires std::is_base_of_v<turboq::MulticastMessageQueue::Consumer, Consumer> ||
                 std::is_base_of_v<turboq::MPSCMessageQueue::Consumer, Consumer> ||
                 std::is_base_of_v<turboq::SPSCMessageQueue::Consumer, Consumer>
    [[nodiscard]] auto createConsumer(std::string_view role, bool openOnly = false) const noexcept
        -> std::expected<Consumer, std::string> {
        auto const* definition = findRoleQueue(role);
        if (definition == nullptr) {
            return std::unexpected{
                fmt::format("role \"{}\" is not configured for service \"{}\"", role, definition_.name)};
        }
        if constexpr (std::is_base_of_v<turboq::MulticastMessageQueue::Consumer, Consumer>) {
            if (definition->type != turbofw::QueueType::Multicast) {
                return std::unexpected{
                    fmt::format("queue \"{}\" (role \"{}\") is not a multicast queue", definition->name, role)};
            }
            return openQueue<turboq::MulticastMessageQueue, Consumer, false>(*definition, openOnly);
        } else if constexpr (std::is_base_of_v<turboq::MPSCMessageQueue::Consumer, Consumer>) {
            if (definition->type != turbofw::QueueType::MPSC) {
                return std::unexpected{
                    fmt::format("queue \"{}\" (role \"{}\") is not an mpsc queue", definition->name, role)};
            }
            return openQueue<turboq::MPSCMessageQueue, Consumer, false>(*definition, openOnly);
        } else {
            if (definition->type != turbofw::QueueType::SPSC) {
                return std::unexpected{
                    fmt::format("queue \"{}\" (role \"{}\") is not an spsc queue", definition->name, role)};
            }
            return openQueue<turboq::SPSCMessageQueue, Consumer, false>(*definition, openOnly);
        }
    }

private:
    /// Takes an already validated definition
    explicit Environment(turbofw::ServiceDefinition definition) noexcept : definition_{std::move(definition)} {}

    /// The role queue definition, nullptr when the role is not configured
    [[nodiscard]] auto findRoleQueue(std::string_view role) const noexcept -> turbofw::QueueDefinition const*;

    /// Build the memory source of a queue definition; when @c createDirectories, missing source
    /// directories are created first
    [[nodiscard]] auto makeMemorySource(turbofw::QueueDefinition const& definition, bool createDirectories) const
        -> std::expected<turboq::DefaultMemorySource, std::string>;

    /// Open (or create) the queue of a definition and wrap its producer/consumer into @c Wrapper
    template <typename Q, typename Wrapper, bool IsProducer>
    [[nodiscard]] auto openQueue(turbofw::QueueDefinition const& definition, bool openOnly) const noexcept
        -> std::expected<Wrapper, std::string> {
        try {
            auto sourceResult = makeMemorySource(definition, !openOnly);
            if (!sourceResult) {
                return std::unexpected{std::move(sourceResult).error()};
            }
            auto queue = [&] {
                if (openOnly) {
                    return Q::makeQueue(definition.name, *sourceResult);
                }
                auto const* options = std::get_if<typename Q::CreationOptions>(&definition.creationOptions);
                if (options == nullptr) {
                    throw std::runtime_error{
                        fmt::format("queue \"{}\": creation options do not match its type", definition.name)};
                }
                return Q::makeQueue(definition.name, *options, *sourceResult);
            }();
            if (!queue) {
                return std::unexpected{fmt::format("failed to {} queue \"{}\" ({})", openOnly ? "open" : "create",
                    definition.name, queue.error().message())};
            }
            if constexpr (IsProducer) {
                return Wrapper{queue->createProducer()};
            } else {
                return Wrapper{queue->createConsumer()};
            }
        } catch (std::exception const& e) {
            return std::unexpected{e.what()};
        }
    }

    turbofw::ServiceDefinition definition_;
};

} // namespace turbofw::v3
