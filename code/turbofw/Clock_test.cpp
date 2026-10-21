// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <doctest/doctest.h>

#include "Clock.h"

namespace turbofw::testing {

TEST_SUITE("Clock") {
    TEST_CASE("monotonic clock does not go backwards") {
        auto const t0 = monotonicNanos();
        auto const t1 = monotonicNanos();
        REQUIRE_LE(t0, t1);
    }

    TEST_CASE("realtime clock is after 2020-01-01") {
        REQUIRE_GT(realtimeNanos(), 1'577'836'800ull * 1'000'000'000ull);
    }
}

} // namespace turbofw::testing
