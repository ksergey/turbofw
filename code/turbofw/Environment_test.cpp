// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>

#include <doctest/doctest.h>
#include <turboq/MPSCMessageQueue.h>
#include <turboq/MulticastMessageQueue.h>

#include "Environment.h"
#include "config/Exception.h"

namespace fs = std::filesystem;

namespace turbofw::testing {
namespace {

constexpr auto kFullConfig = R"({
    "scope": "${TURBOFW_TEST_USER}",
    "working-directory": "${TURBOFW_TEST_DIR}/work",
    "search-path": [ "${TURBOFW_TEST_DIR}/system", "${TURBOFW_TEST_DIR}/work" ],
    "queues": [
        { "name": "metadata@data", "type": "multicast", "capacity": "1MiB" },
        { "name": "metadata@cmd", "type": "mpsc", "length": 256, "slot-size": "2KiB" }
    ]
})";

} // namespace

TEST_SUITE("Environment") {
    TEST_CASE("load full config, expand vars, open mpsc and multicast queues") {
        ::setenv("TURBOFW_TEST_USER", "john.doe", 1);

        auto const dir = std::filesystem::current_path();
        auto vars = Environment::Vars{{"NAME", "my-service"}};
        vars.emplace("TURBOFW_TEST_DIR", dir.string());

        auto const envResult = Environment::loadFromJson(kFullConfig, vars);
        REQUIRE_MESSAGE(envResult, envResult.error());

        auto const& env = envResult.value();
        CHECK_EQ(env.scope(), "john.doe");
        CHECK_EQ(env.workingDirectory(), dir / "work");
        REQUIRE_EQ(env.searchPath().size(), 2);
        CHECK_EQ(env.searchPath()[0], dir / "system");
        REQUIRE_EQ(env.queues().size(), 2);

        // working directory is created
        CHECK(fs::is_directory(dir / "work"));
    }
}

} // namespace turbofw::testing
