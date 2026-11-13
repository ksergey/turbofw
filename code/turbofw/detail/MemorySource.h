// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string_view>
#include <system_error>
#include <tuple>

#include <turboq/MemorySource.h>

namespace turbofw::detail {

/// Memory source that creates missing parent directories of a queue name on OpenOrCreate.
/// Queue names may contain '/' to group queues per system ("<scope>/<service>@<role>"):
/// the name is joined with the source path, so "/dev/shm/dev/metadata@data" needs the "dev"
/// directory to exist; the default turboq source does not create it.
/// Mount: where the queue files live; nullopt = turboq default (tmpfs scan: /dev/shm, then /tmp)
class MemorySource final : public turboq::MemorySource {
private:
    std::optional<turboq::DefaultMemorySource> source_;

public:
    /// Construct the underlying source (may throw: mount point scan)
    explicit MemorySource(std::optional<std::filesystem::path> mount) {
        if (mount.has_value()) {
            source_.emplace(*mount);
        } else {
            source_.emplace();
        }
    }

    /// \see MemorySource::open
    [[nodiscard]] auto open(std::string_view name, OpenFlags flags) const noexcept
        -> std::expected<std::tuple<turboq::File, std::size_t>, std::error_code> override {
        if (flags == OpenOrCreate) {
            // create parent directories of the queue file; ignore a missing directory part
            auto const filePath = source_->path() / name;
            std::error_code ec;
            std::filesystem::create_directories(filePath.parent_path(), ec);
            if (ec) {
                return std::unexpected{ec};
            }
        }
        return source_->open(name, flags);
    }
};

} // namespace turbofw::detail
