// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include <turboq/MPSCMessageQueue.h>
#include <turboq/MulticastMessageQueue.h>
#include <turboq/Platform.h>
#include <turboq/SPSCMessageQueue.h>

namespace turbofw::detail {

struct EnvironmentConfig;

struct EnvironmentImpl {
    enum class QueueEntryType {
        Multicast,
        MPSC,
        SPSC,
    };

    struct QueueEntry {
        std::string name;
        QueueEntryType type;
        std::optional<std::size_t> capacity;
        std::optional<std::size_t> length;
        std::optional<std::size_t> slotSize;
    };

    std::string scope;
    std::filesystem::path workingDirectory;
    std::vector<std::filesystem::path> searchPath;
    std::vector<QueueEntry> queues;

    using Vars = std::map<std::string, std::string, std::less<>>;

    [[nodiscard]] static auto loadFromJson(std::string_view content, Vars vars) noexcept
        -> std::expected<EnvironmentImpl, std::string>;

    [[nodiscard]] static auto loadFromConfig(std::filesystem::path const& configPath, Vars vars) noexcept
        -> std::expected<EnvironmentImpl, std::string>;

    [[nodiscard]] static auto load(EnvironmentConfig const& config, Vars vars) noexcept
        -> std::expected<EnvironmentImpl, std::string>;

    [[nodiscard]] auto getMulticastQueue(std::string_view queueName, bool openOnly) const noexcept
        -> std::expected<turboq::MulticastMessageQueue, std::string>;

    [[nodiscard]] auto getMPSCQueue(std::string_view queueName, bool openOnly) const noexcept
        -> std::expected<turboq::MPSCMessageQueue, std::string>;

    [[nodiscard]] auto getSPSCQueue(std::string_view queueName, bool openOnly) const noexcept
        -> std::expected<turboq::SPSCMessageQueue, std::string>;

    [[nodiscard]] auto findQueueEntry(std::string_view queueName) const noexcept -> QueueEntry const*;

    [[nodiscard]] auto findQueueEntry(std::string_view queueName, QueueEntryType queueType) const noexcept
        -> QueueEntry const*;
};

} // namespace turbofw::detail
