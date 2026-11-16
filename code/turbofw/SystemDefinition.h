// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <expected>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "Environment.h"
#include "ServiceDefinition.h"

namespace turbofw {

/// Process environment for a system of services.
///
/// The config is kept as json and read on every call: "${VAR}" entries are expanded and values
/// are built into structs at the moment of the call, so variable changes (vars or the process
/// environment) are picked up without a reload. The actual queue creation happens in
/// turbofw::Environment built from a ServiceDefinition.
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

private:
    nlohmann::json json_;
    Vars vars_;

    /// Takes the parsed config. Throws std::runtime_error when the shape is invalid
    SystemDefinition(nlohmann::json json, Vars vars);

public:
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

    /// Create environment for service
    [[nodiscard]] auto environmentFor(std::string_view name) const -> std::expected<Environment, std::string> {
        return this->findService(name).and_then([](ServiceDefinition definition) {
            return Environment::create(std::move(definition));
        });
    }
};

} // namespace turbofw
