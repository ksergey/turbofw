// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <expected>
#include <map>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <turboq/MPSCMessageQueue.h>
#include <turboq/MulticastMessageQueue.h>
#include <turboq/SPSCMessageQueue.h>

#include "Types.h"

namespace turbofw::v2 {

/// Process environment for a system of services
class Environment {
public:
    /// Extra variables for ${VAR} expansion, applied before the process environment
    using Vars = std::map<std::string, std::string, std::less<>>;

    enum class QueueType { Multicast, MPSC, SPSC };

    // clang-format off
    using CreationOptions = std::variant<
        turboq::MulticastMessageQueue::CreationOptions,
        turboq::MPSCMessageQueue::CreationOptions,
        turboq::SPSCMessageQueue::CreationOptions
    >;
    // clang-format on

    struct MemorySourceOptions {
        std::filesystem::path path{"/dev/shm"};
        std::size_t pageSize{4096};
    };

    /// A queue definition
    struct QueueDefinition {
        std::string name;
        QueueType type; // TODO: could be removed because CreationOptions point to exactly queue type?
        CreationOptions creationOptions;
        MemorySourceOptions memorySourceOptions;
    };

    /// A service definition
    struct ServiceDefinition {
        std::string name;
        ServiceId serviceId;
        std::map<std::string, QueueDefinition> queues;
        std::filesystem::path workingDirectory;
    };

    /// Load environment from a json string in memory
    /// \see the class docblock for what is validated
    [[nodiscard]] static auto loadFromJson(
        std::string_view content, Vars vars = {}) noexcept -> std::expected<Environment, std::string>;

    /// Load environment from a json config file
    /// \see the class docblock for what is validated
    [[nodiscard]] static auto loadFromFile(
        std::filesystem::path const& configPath, Vars vars = {}) noexcept -> std::expected<Environment, std::string>;

    /// Config scope: the name of the system/deployment all services of one system belong to;
    /// expanded (and charset-validated) on every call
    [[nodiscard]] auto scope() const -> std::expected<std::string, std::string>;

    /// Queues configured for the scope: names, types and creation options, read from json and
    /// expanded on every call. Throws std::runtime_error on an access error
    [[nodiscard]] auto queues() const -> std::expected<std::vector<QueueDefinition>, std::string>;

    /// Services statically configured in the environment, read from json and expanded on every
    /// call. Throws std::runtime_error on an access error
    [[nodiscard]] auto services() const -> std::expected<std::vector<ServiceDefinition>, std::string>;

    /// Find a statically configured service by (expanded) name
    [[nodiscard]] auto findService(std::string_view name) const -> std::expected<ServiceDefinition, std::string>;
};

} // namespace turbofw::v2
