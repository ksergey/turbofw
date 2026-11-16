// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include <turboq/MPSCMessageQueue.h>
#include <turboq/MulticastMessageQueue.h>
#include <turboq/SPSCMessageQueue.h>

#include "Types.h"

namespace turbofw {

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

/// Process environment for a system of services.
///
/// Unlike turbofw::Environment (v1), which fully expands and validates the config at load, this
/// one keeps the config as json and reads it on every call: "${VAR}" entries are expanded and
/// values are built into structs at the moment of the call, so variable changes (vars or the
/// process environment) are picked up without a reload.
///
/// Validation happens only at load: the config must be a json object with a "scope" (non-empty
/// string) section; "queues" and "services" are optional arrays: absent = empty.
/// Cross-references are not checked at load. Access errors (an unknown variable, a bad queue
/// name or creation options after the expansion, a "service-id" out of range, an unknown role
/// queue name) come back as std::unexpected with a message on every call.
///
/// The expanded scope is available to the rest of the config as "${SCOPE}"; the expansion
/// sources are (in order): the vars passed to load, "${SCOPE}", the process environment.
///
/// \see the accessors docblocks for the struct fields
class SystemDefinition {
public:
    /// Extra variables for ${VAR} expansion, applied before the process environment
    using Vars = std::map<std::string, std::string, std::less<>>;

    /// Load environment from a json string in memory
    /// \see the class docblock for what is validated
    [[nodiscard]] static auto loadFromJson(std::string_view content, Vars vars = {}) noexcept
        -> std::expected<SystemDefinition, std::string>;

    /// Load environment from a json config file
    /// \see the class docblock for what is validated
    [[nodiscard]] static auto loadFromFile(std::filesystem::path const& configPath, Vars vars = {}) noexcept
        -> std::expected<SystemDefinition, std::string>;

    /// Config scope: the name of the system/deployment all services of one system belong to;
    /// expanded (and charset-validated) on every call
    [[nodiscard]] auto scope() const -> std::expected<std::string, std::string>;

    /// Queues configured for the scope: names, types and creation options, read from json and
    /// expanded on every call
    [[nodiscard]] auto queues() const -> std::expected<std::vector<QueueDefinition>, std::string>;

    /// Services statically configured in the environment, read from json and expanded on every
    /// call. Every role queue name is resolved against queues()
    [[nodiscard]] auto services() const -> std::expected<std::vector<ServiceDefinition>, std::string>;

    /// Find a statically configured service by (expanded) name
    [[nodiscard]] auto findService(std::string_view name) const -> std::expected<ServiceDefinition, std::string>;

private:
    /// Takes the parsed config. Throws std::runtime_error when the shape is invalid
    SystemDefinition(nlohmann::json json, Vars vars);

    nlohmann::json json_;
    Vars vars_;
};

} // namespace turbofw
