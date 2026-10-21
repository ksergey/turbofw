// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <algorithm>

#include <doctest/doctest.h>

#include "Version.h"

namespace turbofw::testing {

TEST_SUITE("Version") {
    TEST_CASE("version has major.minor.patch form") {
        auto const v = version();
        REQUIRE_FALSE(v.empty());
        REQUIRE_EQ(std::ranges::count(v, '.'), 2);
    }
}

} // namespace turbofw::testing
