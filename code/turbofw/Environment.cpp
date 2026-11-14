// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "Environment.h"

namespace turbofw {

auto Environment::loadFromJson(std::string_view content, Vars vars) noexcept
    -> std::expected<Environment, std::string> {
    return EnvironmentImpl::loadFromJson(content, vars).transform([](EnvironmentImpl&& impl) {
        return Environment{std::move(impl)};
    });
}

auto Environment::loadFromFile(std::filesystem::path const& configPath, Vars vars) noexcept
    -> std::expected<Environment, std::string> {
    return EnvironmentImpl::loadFromFile(configPath, vars).transform([](EnvironmentImpl&& impl) {
        return Environment{std::move(impl)};
    });
}

auto Environment::findFile(std::filesystem::path const& relative) const -> std::optional<std::filesystem::path> {
    for (auto const& dir : this->searchPath()) {
        auto const file = dir / relative;
        if (std::filesystem::exists(file)) {
            return file;
        }
    }
    return std::nullopt;
}

} // namespace turbofw
