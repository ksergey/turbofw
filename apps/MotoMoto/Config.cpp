// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "Config.h"

#include <fmt/format.h>
#include <fmt/std.h>

#include <turbofw/config/Json.h>

namespace app {

namespace conf = turbofw::config;

template <typename DTO>
void ServiceConfig::serialize(DTO& dto) {
    // clang-format off
    dto
        & conf::value<"name">(&name).defaultValue("MotoMoto")
    ;
    // clang-format on
}

template <typename DTO>
void LoggerConfig::serialize(DTO& dto) {
    // clang-format off
    dto
    ;
    // clang-format on
}

template <typename DTO>
void Config::serialize(DTO& dto) {
    // clang-format off
    dto
        & conf::value<"service">(&service)
        & conf::value<"logger">(&logger)
    ;
    // clang-format on
}

auto Config::loadFromFile(std::filesystem::path const& configPath) noexcept -> std::expected<Config, std::string> {
    Config cfg;
    if (auto result = conf::fromJsonFile(cfg, configPath); !result) {
        return std::unexpected{fmt::format("failed to load config from file \"{}\" ({})", configPath, result.error())};
    }
    return cfg;
}

auto Config::toJson() const -> std::string {
    return conf::toJson(*this).value();
}

} // namespace app
