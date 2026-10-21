// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <ctime>

namespace turbofw {

/// Wall-clock time in nanoseconds since the Unix epoch (CLOCK_REALTIME).
/// Use for timestamps that leave the process (messages, logs).
[[nodiscard]] inline auto realtimeNanos() noexcept -> std::uint64_t {
    ::timespec ts;
    ::clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ull + static_cast<std::uint64_t>(ts.tv_nsec);
}

/// Monotonic time in nanoseconds (CLOCK_MONOTONIC).
/// Use for measuring intervals and latencies inside a host.
[[nodiscard]] inline auto monotonicNanos() noexcept -> std::uint64_t {
    ::timespec ts;
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ull + static_cast<std::uint64_t>(ts.tv_nsec);
}

} // namespace turbofw
