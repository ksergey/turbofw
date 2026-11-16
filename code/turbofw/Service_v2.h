// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <expected>
#include <filesystem>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <fmt/format.h>
#include <turboq/MPSCMessageQueue.h>
#include <turboq/MulticastMessageQueue.h>

#include "Environment_v2.h"
#include "detail/MemorySource.h"

namespace turbofw::v2 {

/// Everything a single service needs from the environment: identity, its queues, and the
/// producer/consumer creation.
///
/// Unlike turbofw::Environment (v1), no queue methods live on the environment itself: queue
/// creation belongs to the service. A Service binds one statically configured service
/// (Environment::service()): its queues are the "cmd"/"data" roles of the "services" entry,
/// and createProducer()/createConsumer() open (or create) the queues from the "queues"
/// section, with the options and the memory source from the environment config.
///
///     auto service = env.service("metadata");
///     auto producer = service.createProducer<MyProducer>();  // the "data" queue (Multicast)
///     auto consumer = service.createConsumer<MyConsumer>();  // the "cmd" queue (MPSC)
///
/// The environment must outlive the Service only while its queues are being created: the
/// producer/consumer are independent once created. Failures come back as std::unexpected
/// (with a message), except the constructor, which throws when the service is not configured.
class Service {
public:
    /// Bind to the statically configured service `name`.
    /// Throws std::runtime_error when the service is not configured (or its queues have an
    /// access error), std::system_error when the memory source can't be set up
    Service(Environment const& env, std::string_view name) {
        auto const entry = env.findService(name);
        if (!entry) {
            throw std::runtime_error{fmt::format("service \"{}\" is not configured in the environment", name)};
        }
        entry_ = std::move(*entry);
        queues_ = env.queues();
        auto const mount = env.memorySourceMount();
        // v1 created the mount directory at load; keep the same behavior for the file-based source
        if (mount) {
            std::filesystem::create_directories(*mount);
        }
        source_.emplace(mount);
    }

    /// The (expanded) service name
    [[nodiscard]] auto name() const noexcept -> std::string const& {
        return entry_.name;
    }

    /// The service id assigned by configuration
    [[nodiscard]] auto serviceId() const noexcept -> std::uint16_t {
        return entry_.serviceId;
    }

    /// The service queues: role -> expanded queue name
    [[nodiscard]] auto queues() const noexcept -> std::map<std::string, std::string> const& {
        return entry_.queues;
    }

    /// Open (or create) the "data" queue and wrap its producer into `Producer`
    /// (a type derived from turboq::MulticastMessageQueue::Producer, e.g. a MakeProducer)
    template <typename Producer>
        requires std::is_base_of_v<turboq::MulticastMessageQueue::Producer, Producer>
    [[nodiscard]] auto createProducer() const noexcept -> std::expected<Producer, std::string> {
        try {
            auto const& queue = findQueue(kDataQueueRole, QueueEntry::Type::Multicast);
            auto const& options = *std::get_if<turboq::MulticastMessageQueue::CreationOptions>(&queue.creationOptions);
            auto queueResult = turboq::MulticastMessageQueue::makeQueue(queue.name, options, *source_);
            if (!queueResult) {
                return std::unexpected{fmt::format(
                    "failed to open multicast queue \"{}\" ({})", queue.name, queueResult.error().message())};
            }
            return Producer{queueResult->createProducer()};
        } catch (std::exception const& e) {
            return std::unexpected{e.what()};
        }
    }

    /// Open (or create) the "cmd" queue and wrap its consumer into `Consumer`
    /// (a type derived from turboq::MPSCMessageQueue::Consumer, e.g. a MakeConsumer)
    template <typename Consumer>
        requires std::is_base_of_v<turboq::MPSCMessageQueue::Consumer, Consumer>
    [[nodiscard]] auto createConsumer() const noexcept -> std::expected<Consumer, std::string> {
        try {
            auto const& queue = findQueue(kCmdQueueRole, QueueEntry::Type::MPSC);
            auto const& options = *std::get_if<turboq::MPSCMessageQueue::CreationOptions>(&queue.creationOptions);
            auto queueResult = turboq::MPSCMessageQueue::makeQueue(queue.name, options, *source_);
            if (!queueResult) {
                return std::unexpected{
                    fmt::format("failed to open mpsc queue \"{}\" ({})", queue.name, queueResult.error().message())};
            }
            return Consumer{queueResult->createConsumer()};
        } catch (std::exception const& e) {
            return std::unexpected{e.what()};
        }
    }

private:
    ServiceEntry entry_;
    std::vector<QueueEntry> queues_; ///< the environment "queues", for lookups by role queue name
    std::optional<detail::MemorySource> source_;

    /// The service queue of a role, checked against the expected queue type.
    /// Throws std::runtime_error when the role is missing or the queue does not match
    [[nodiscard]] auto findQueue(std::string_view role, QueueEntry::Type type) const -> QueueEntry const& {
        auto const roleEntry = entry_.queues.find(std::string{role});
        if (roleEntry == entry_.queues.end()) {
            throw std::runtime_error{fmt::format("service \"{}\": no \"{}\" queue", entry_.name, role)};
        }
        auto const found = std::ranges::find_if(queues_, [&](QueueEntry const& entry) {
            return entry.name == roleEntry->second;
        });
        if (found == queues_.end() || found->type != type) {
            throw std::runtime_error{fmt::format(
                "service \"{}\": \"{}\" queue \"{}\" is not a {} queue", entry_.name, role, roleEntry->second, type)};
        }
        return *found;
    }
};

// Environment::service(), declared in Environment_v2.h: the Service return type is complete only here
inline auto Environment::service(std::string_view name) const -> Service {
    return Service{*this, name};
}

} // namespace turbofw::v2
