// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include <turboq/MPSCMessageQueue.h>
#include <turboq/MulticastMessageQueue.h>
#include <turboq/SPSCMessageQueue.h>

#include "config/ByteSize.h"
#include "config/ExpandVars.h"
#include "config/Json.h"

namespace turbofw {

/// Process environment for a trading service: config scope, working directories, shared memory queues
class Environment {
public:
    /// Message queue type
    enum class QueueType { Spsc, Mpsc, Multicast };

    /// Queue entry from config
    struct QueueConfig {
        std::string name;
        std::string type;
        std::optional<config::ByteSize> capacity;
        std::optional<std::uint64_t> length;
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

    /// Shared memory options for queues
    struct QueuesOpt {
        bool hugepages = false;
        std::filesystem::path base = std::filesystem::path{"/dev/shm"};

        template <typename DTO>
        void serialize(DTO& dto) {
            // clang-format off
            dto
                & config::value<"hugepages">(&hugepages).defaultValue(false)
                & config::value<"base">(&base).defaultValue(std::filesystem::path{"/dev/shm"})
            ;
            // clang-format on
        }
    };

    /// Whole environment config (one scope per file)
    struct ScopeConfig {
        std::string scope;
        std::optional<std::filesystem::path> workingDirectory;
        std::vector<std::filesystem::path> searchPath;
        QueuesOpt queuesOpt;
        std::vector<QueueConfig> queues;

        template <typename DTO>
        void serialize(DTO& dto) {
            // clang-format off
            dto
                & config::value<"scope">(&scope)
                & config::value<"working-directory">(&workingDirectory).defaultValue(std::nullopt)
                & config::value<"search-path">(&searchPath).defaultValue(std::vector<std::filesystem::path>{})
                & config::value<"queues-opt">(&queuesOpt).defaultValue(QueuesOpt{})
                & config::value<"queues">(&queues).defaultValue(std::vector<QueueConfig>{})
            ;
            // clang-format on
        }
    };

    /// Queue entry, config resolved against queue type
    struct QueueEntry {
        std::string name;
        QueueType type;
        std::size_t capacity = 0; ///< for spsc/multicast, bytes
        std::size_t length = 0;   ///< for mpsc, slot count
        std::size_t slotSize = 0; ///< for mpsc, slot size in bytes
    };

    /// Load environment from config file
    /// @param[in] configPath is path to environment config file
    /// @param[in] vars is additional variables for ${VAR} expansion, then process environment applies
    ///                (e.g. {"NAME", appName} since service name is only known at startup)
    static auto load(std::filesystem::path const& configPath, std::map<std::string, std::string, std::less<>> vars = {})
        -> std::expected<Environment, std::string>;

    /// Queue entry introspection
    [[nodiscard]] auto queues() const noexcept -> std::span<QueueEntry const> {
        return queues_;
    }

    /// Configured scope name
    [[nodiscard]] auto scope() const noexcept -> std::string_view {
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

    /// Open queue/producer/consumer by name, type and creation options come from config
    template <typename T>
    auto getQueue(std::string_view name) const noexcept -> std::expected<T, std::string>;

private:
    std::string scope_;
    std::filesystem::path workingDirectory_;
    std::vector<std::filesystem::path> searchPath_;
    std::vector<QueueEntry> queues_;
    std::unique_ptr<turboq::DefaultMemorySource> memorySource_;
};

namespace detail {

enum class QueueAccessKind { Queue, Producer, Consumer };

/// Trait mapping turboq queue/producer/consumer types to Environment::QueueType
template <typename T>
struct QueueTraits {}; // no traits: type is not related to queues

template <>
struct QueueTraits<turboq::SPSCMessageQueue> {
    static constexpr auto kind = QueueAccessKind::Queue;
    static constexpr auto queueType = Environment::QueueType::Spsc;
};

template <>
struct QueueTraits<turboq::SPSCMessageQueue::Producer> {
    static constexpr auto kind = QueueAccessKind::Producer;
    static constexpr auto queueType = Environment::QueueType::Spsc;
};

template <>
struct QueueTraits<turboq::SPSCMessageQueue::Consumer> {
    static constexpr auto kind = QueueAccessKind::Consumer;
    static constexpr auto queueType = Environment::QueueType::Spsc;
};

template <>
struct QueueTraits<turboq::MPSCMessageQueue> {
    static constexpr auto kind = QueueAccessKind::Queue;
    static constexpr auto queueType = Environment::QueueType::Mpsc;
};

template <>
struct QueueTraits<turboq::MPSCMessageQueue::Producer> {
    static constexpr auto kind = QueueAccessKind::Producer;
    static constexpr auto queueType = Environment::QueueType::Mpsc;
};

template <>
struct QueueTraits<turboq::MPSCMessageQueue::Consumer> {
    static constexpr auto kind = QueueAccessKind::Consumer;
    static constexpr auto queueType = Environment::QueueType::Mpsc;
};

template <>
struct QueueTraits<turboq::MulticastMessageQueue> {
    static constexpr auto kind = QueueAccessKind::Queue;
    static constexpr auto queueType = Environment::QueueType::Multicast;
};

template <>
struct QueueTraits<turboq::MulticastMessageQueue::Producer> {
    static constexpr auto kind = QueueAccessKind::Producer;
    static constexpr auto queueType = Environment::QueueType::Multicast;
};

template <>
struct QueueTraits<turboq::MulticastMessageQueue::Consumer> {
    static constexpr auto kind = QueueAccessKind::Consumer;
    static constexpr auto queueType = Environment::QueueType::Multicast;
};

/// Queue type name for error messages
constexpr auto queueTypeName(Environment::QueueType type) noexcept -> std::string_view {
    switch (type) {
        case Environment::QueueType::Spsc: return "spsc";
        case Environment::QueueType::Mpsc: return "mpsc";
        case Environment::QueueType::Multicast: return "multicast";
        default: return "<unknown>";
    }
}

/// Convert std::error_code to string
inline auto errorToString(std::error_code const& ec) -> std::string {
    return ec.message();
}

} // namespace detail

/// Type is a turboq queue, producer or consumer of one of supported queue types
template <typename T>
concept QueueAccessorType = requires {
    detail::QueueTraits<T>::queueType;
    detail::QueueTraits<T>::kind;
};

template <typename T>
auto Environment::getQueue(std::string_view name) const noexcept -> std::expected<T, std::string> {
    static_assert(QueueAccessorType<T>, "T must be a turboq queue, producer or consumer type");

    auto const traitsQueueType = detail::QueueTraits<T>::queueType;

    auto const found = std::find_if(queues_.begin(), queues_.end(), [&](QueueEntry const& entry) {
        return entry.name == name;
    });
    if (found == queues_.end()) {
        return std::unexpected{fmt::format("unknown queue \"{}\"", name)};
    }
    auto const* entry = &*found;

    if (!memorySource_) {
        return std::unexpected{"no shared memory source is configured"};
    }

    if (entry->type != traitsQueueType) {
        return std::unexpected{fmt::format("queue \"{}\" is {}, but {} was requested", name,
            detail::queueTypeName(entry->type), detail::queueTypeName(traitsQueueType))};
    }

    if constexpr (detail::QueueTraits<T>::kind == detail::QueueAccessKind::Queue) {
        if constexpr (traitsQueueType == QueueType::Mpsc) {
            return turboq::MPSCMessageQueue::makeQueue(entry->name,
                       turboq::MPSCMessageQueue::CreationOptions{.slotSizeHint = entry->slotSize, .lengthHint = entry->length},
                       *memorySource_)
                .transform_error(detail::errorToString);
        } else if constexpr (traitsQueueType == QueueType::Multicast) {
            return turboq::MulticastMessageQueue::makeQueue(entry->name,
                       turboq::MulticastMessageQueue::CreationOptions{.capacityHint = entry->capacity}, *memorySource_)
                .transform_error(detail::errorToString);
        } else {
            return turboq::SPSCMessageQueue::makeQueue(entry->name,
                       turboq::SPSCMessageQueue::CreationOptions{.capacityHint = entry->capacity}, *memorySource_)
                .transform_error(detail::errorToString);
        }
    } else if constexpr (detail::QueueTraits<T>::kind == detail::QueueAccessKind::Producer) {
        if constexpr (traitsQueueType == QueueType::Mpsc) {
            return turboq::MPSCMessageQueue::makeProducer(entry->name,
                       turboq::MPSCMessageQueue::CreationOptions{.slotSizeHint = entry->slotSize, .lengthHint = entry->length},
                       *memorySource_)
                .transform_error(detail::errorToString);
        } else if constexpr (traitsQueueType == QueueType::Multicast) {
            return turboq::MulticastMessageQueue::makeProducer(entry->name,
                       turboq::MulticastMessageQueue::CreationOptions{.capacityHint = entry->capacity}, *memorySource_)
                .transform_error(detail::errorToString);
        } else {
            return turboq::SPSCMessageQueue::makeProducer(entry->name,
                       turboq::SPSCMessageQueue::CreationOptions{.capacityHint = entry->capacity}, *memorySource_)
                .transform_error(detail::errorToString);
        }
    } else {
        if constexpr (traitsQueueType == QueueType::Mpsc) {
            return turboq::MPSCMessageQueue::makeConsumer(entry->name,
                       turboq::MPSCMessageQueue::CreationOptions{.slotSizeHint = entry->slotSize, .lengthHint = entry->length},
                       *memorySource_)
                .transform_error(detail::errorToString);
        } else if constexpr (traitsQueueType == QueueType::Multicast) {
            return turboq::MulticastMessageQueue::makeConsumer(entry->name,
                       turboq::MulticastMessageQueue::CreationOptions{.capacityHint = entry->capacity}, *memorySource_)
                .transform_error(detail::errorToString);
        } else {
            return turboq::SPSCMessageQueue::makeConsumer(entry->name,
                       turboq::SPSCMessageQueue::CreationOptions{.capacityHint = entry->capacity}, *memorySource_)
                .transform_error(detail::errorToString);
        }
    }
}

} // namespace turbofw
