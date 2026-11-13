// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <expected>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <variant>

#include <fmt/format.h>

#include <turboq/MPSCMessageQueue.h>
#include <turboq/MulticastMessageQueue.h>
#include <turboq/Platform.h>
#include <turboq/SPSCMessageQueue.h>

#include "MemorySource.h"

namespace turbofw::detail {

struct EnvironmentConfig;

struct EnvironmentImpl {
    enum class QueueEntryType {
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

    struct QueueEntry {
        std::string name;
        QueueEntryType type;
        CreationOptionsVariant creationOptions;
    };

    /// Statically configured service: identity and its queues by role ("cmd"/"data" required)
    struct ServiceEntry {
        std::string name;                          ///< unique service name
        std::uint16_t serviceId;                   ///< assigned by configuration
        std::map<std::string, std::string> queues; ///< role -> expanded queue name
    };

    std::string scope;
    std::filesystem::path workingDirectory;
    std::vector<std::filesystem::path> searchPath;
    std::vector<QueueEntry> queues;
    std::vector<ServiceEntry> services;
    MemorySource memorySource;

    explicit EnvironmentImpl(std::optional<std::filesystem::path> mount) : memorySource{std::move(mount)} {}

    using Vars = std::map<std::string, std::string, std::less<>>;

    [[nodiscard]] static auto loadFromJson(
        std::string_view content, Vars vars) noexcept -> std::expected<EnvironmentImpl, std::string>;

    [[nodiscard]] static auto loadFromConfig(
        std::filesystem::path const& configPath, Vars vars) noexcept -> std::expected<EnvironmentImpl, std::string>;

    [[nodiscard]] static auto load(
        EnvironmentConfig const& config, Vars vars) noexcept -> std::expected<EnvironmentImpl, std::string>;

    /// Open or create turboq queue
    template <typename Q>
        requires std::is_same_v<Q, turboq::MulticastMessageQueue> || std::is_same_v<Q, turboq::MPSCMessageQueue> ||
                     std::is_same_v<Q, turboq::SPSCMessageQueue>
    [[nodiscard]] auto getQueue(
        std::string_view queueName, bool openOnly) const noexcept -> std::expected<Q, std::string> {
        constexpr auto queueType = [] {
            if constexpr (std::is_same_v<Q, turboq::MulticastMessageQueue>) {
                return QueueEntryType::Multicast;
            } else if constexpr (std::is_same_v<Q, turboq::MPSCMessageQueue>) {
                return QueueEntryType::MPSC;
            } else if constexpr (std::is_same_v<Q, turboq::SPSCMessageQueue>) {
                return QueueEntryType::SPSC;
            }
        }();

        auto const queueEntry = [&]() noexcept -> QueueEntry const* {
            auto const found = std::ranges::find_if(queues, [&](auto const& entry) {
                return entry.name == queueName;
            });
            if (found == queues.end()) {
                return nullptr;
            }
            if (found->type != queueType) {
                return nullptr;
            }
            return &*found;
        }();
        if (!queueEntry) {
            return std::unexpected{fmt::format("{} queue \"{}\" not found", queueType, queueName)};
        }

        auto const createQueue = [&] {
            if (openOnly) {
                return Q::makeQueue(queueName, memorySource);
            } else {
                return Q::makeQueue(
                    queueName, *std::get_if<typename Q::CreationOptions>(&queueEntry->creationOptions), memorySource);
            }
        };

        return createQueue().transform_error([&](std::error_code const& ec) {
            return fmt::format("failed to get {} queue \"{}\" ({})", queueType, queueName, ec.message());
        });
    }
};

/// fmt::format helper for QueueEntryType
auto format_as(EnvironmentImpl::QueueEntryType type) noexcept -> std::string_view;

} // namespace turbofw::detail
