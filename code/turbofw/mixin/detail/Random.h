// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <random>

#include <turbofw/Clock.h>

namespace turbofw::mixin::detail {

/// splitmix64: a tiny stateful PRNG for id generation on hot paths. Pure arithmetic -- no syscalls, no allocations,
/// a few ns per draw. Not cryptographically secure; the seed (see \ref randomSeed) is.
class SplitMix64 {
public:
    explicit SplitMix64(std::uint64_t seed) noexcept : state_{seed} {}

    [[nodiscard]] auto next() noexcept -> std::uint64_t {
        state_ += 0x9E3779B97F4A7C15ull;
        auto z = state_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

private:
    std::uint64_t state_;
};

/// A seed for \ref SplitMix64 mixed from `std::random_device` and the monotonic clock, so the sequence differs
/// across processes and restarts even where the device is deterministic. Construction-time only: never call on a
/// hot path.
[[nodiscard]] inline auto randomSeed() noexcept -> std::uint64_t {
    std::random_device device;
    auto const seed = (std::uint64_t{device()} << 32) ^ std::uint64_t{device()};
    return seed ^ monotonicNanos();
}

} // namespace turbofw::mixin::detail
