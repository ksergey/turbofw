// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <expected>
#include <filesystem>
#include <string>

#include <turbofw/Logging.h>

namespace app {

struct ServiceConfig {
    std::string name; ///< Service name

    template <typename DTO>
    void serialize(DTO& dto);
};

struct LoggerConfig {
    std::string logLevel; ///< Max log verbosity level

    // TODO: add queue size, etc...
    template <typename DTO>
    void serialize(DTO& dto);
};

struct Config {
    ServiceConfig service;
    LoggerConfig logger;

    template <typename DTO>
    void serialize(DTO& dto);

    /// Load config from json file
    static auto loadFromFile(std::filesystem::path const& configPath) noexcept -> std::expected<Config, std::string>;

    /// Dump config to jsons string
    [[nodiscard]] auto toJson() const -> std::string;
};

} // namespace app
