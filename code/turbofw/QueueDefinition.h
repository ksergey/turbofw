// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <variant>

#include <turboq/MPSCMessageQueue.h>
#include <turboq/MulticastMessageQueue.h>
#include <turboq/SPSCMessageQueue.h>

namespace turbofw {

/// A queue definition
struct QueueDefinition {
    enum class QueueType { Multicast, MPSC, SPSC };

    // clang-format off
    using CreationOptions = std::variant<
        turboq::MulticastMessageQueue::CreationOptions,
        turboq::MPSCMessageQueue::CreationOptions,
        turboq::SPSCMessageQueue::CreationOptions
    >;
    // clang-format on

    struct MemorySourceOptions {
        /// Where the queue files are created
        std::filesystem::path path{"/dev/shm"};
        /// Page size for the queue file mappings
        std::size_t pageSize{4096};
    };

    std::string name;
    QueueType type; // TODO: could be removed because CreationOptions point to exactly queue type?
    CreationOptions creationOptions;
    MemorySourceOptions memorySourceOptions;
};

/// fmt::format helper for QueueDefinition::QueueType
inline auto format_as(QueueDefinition::QueueType type) noexcept -> std::string_view {
    using namespace std::string_view_literals;

    switch (type) {
    case QueueDefinition::QueueType::Multicast: return "multicast"sv;
    case QueueDefinition::QueueType::MPSC: return "mpsc"sv;
    case QueueDefinition::QueueType::SPSC: return "spsc"sv;
    }
    return "unknown"sv;
}

} // namespace turbofw
