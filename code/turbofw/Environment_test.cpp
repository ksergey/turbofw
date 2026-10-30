// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include <doctest/doctest.h>
#include <turboq/MPSCMessageQueue.h>
#include <turboq/MulticastMessageQueue.h>

#include "Environment.h"
#include "config/Exception.h"

namespace fs = std::filesystem;

namespace turbofw::testing {
namespace {

/// Unique temp directory, removes itself on destruction
class TempDir {
public:
    TempDir() {
        auto engine = std::default_random_engine{std::random_device{}()};
        auto dist = std::uniform_int_distribution<std::uint32_t>{100000, 999999};
        for (;;) {
            path_ = fs::temp_directory_path() / ("turbofw-env-test-" + std::to_string(dist(engine)));
            if (!fs::exists(path_)) {
                break;
            }
        }
        fs::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }

    TempDir(TempDir const&) = delete;
    TempDir& operator=(TempDir const&) = delete;

    [[nodiscard]] auto path() const noexcept -> fs::path const& {
        return path_;
    }

private:
    fs::path path_;
};

/// Write config file content into a temp file
auto writeConfig(TempDir const& dir, std::string const& content) -> fs::path {
    auto const path = dir.path() / "env.json";
    auto stream = std::ofstream{path, std::ios::binary};
    stream << content;
    return path;
}

constexpr auto kFullConfig = R"({
    "scope": "${TURBOFW_TEST_USER}",
    "working-directory": "${TURBOFW_TEST_DIR}/work",
    "search-path": [ "${TURBOFW_TEST_DIR}/system", "${TURBOFW_TEST_DIR}/work" ],
    "queues-opt": { "hugepages": false, "base": "${TURBOFW_TEST_DIR}/queues/${SCOPE}" },
    "queues": [
        { "name": "metadata@data", "type": "multicast", "capacity": "1MiB" },
        { "name": "metadata@cmd", "type": "mpsc", "length": 256, "slot-size": "2KiB" }
    ]
})";

} // namespace

TEST_SUITE("Environment") {
    TEST_CASE("load full config, expand vars, open mpsc and multicast queues") {
        ::setenv("TURBOFW_TEST_USER", "john.doe", 1);

        auto dir = TempDir{};
        auto vars = std::map<std::string, std::string, std::less<>>{{"NAME", "my-service"}};
        vars.emplace("TURBOFW_TEST_DIR", dir.path().string());

        auto const envResult = Environment::load(writeConfig(dir, kFullConfig), vars);
        REQUIRE_MESSAGE(envResult, envResult.error());

        auto const& env = envResult.value();
        CHECK_EQ(env.scope(), "john.doe");
        CHECK_EQ(env.workingDirectory(), dir.path() / "work");
        REQUIRE_EQ(env.searchPath().size(), 2);
        CHECK_EQ(env.searchPath()[0], dir.path() / "system");
        REQUIRE_EQ(env.queues().size(), 2);

        // working directory is created
        CHECK(fs::is_directory(dir.path() / "work"));

        // base dir with scope is created
        auto const baseDir = dir.path() / "queues" / "john.doe";
        CHECK(fs::is_directory(baseDir));

        // mpsc round trip
        {
            auto producer = env.getQueue<turboq::MPSCMessageQueue::Producer>("metadata@cmd");
            REQUIRE(producer);
            auto consumer = env.getQueue<turboq::MPSCMessageQueue::Consumer>("metadata@cmd");
            REQUIRE(consumer);

            auto const payload = std::uint64_t{42};
            auto const prepared = producer->prepare(sizeof(payload));
            REQUIRE(prepared.size() == sizeof(payload));
            *reinterpret_cast<std::uint64_t*>(prepared.data()) = payload;
            producer->commit();

            auto const fetched = consumer->fetch();
            REQUIRE(fetched.size() == sizeof(payload));
            CHECK_EQ(*reinterpret_cast<std::uint64_t const*>(fetched.data()), payload);
            consumer->consume();
        }

        // multicast round trip
        {
            auto producer = env.getQueue<turboq::MulticastMessageQueue::Producer>("metadata@data");
            REQUIRE(producer);
            auto consumerResult = env.getQueue<turboq::MulticastMessageQueue::Consumer>("metadata@data");
            REQUIRE(consumerResult);
            auto consumer = std::move(*consumerResult);

            auto const prepared = producer->prepare(64);
            REQUIRE(prepared.size() == 64);
            producer->commit();

            auto const fetched = consumer.fetch();
            REQUIRE_NE(fetched.size(), 0);
            consumer.consume();
        }

        // queue files are created in base dir
        auto names = std::string{};
        std::error_code ec;
        for (auto const& entry : fs::directory_iterator(baseDir, ec)) {
            names += entry.path().filename().string();
        }
        REQUIRE_FALSE(ec);
        REQUIRE_NE(names.find("metadata@cmd"), std::string::npos);
        REQUIRE_NE(names.find("metadata@data"), std::string::npos);
    }

    TEST_CASE("findFile searches search-path") {
        ::setenv("TURBOFW_TEST_USER", "john.doe", 1);

        auto dir = TempDir{};
        auto vars = std::map<std::string, std::string, std::less<>>{{"NAME", "my-service"}};
        vars.emplace("TURBOFW_TEST_DIR", dir.path().string());

        auto const envResult = Environment::load(writeConfig(dir, kFullConfig), vars);
        REQUIRE(envResult);
        auto const& env = envResult.value();

        CHECK_FALSE(env.findFile("missing-file.txt").has_value());

        auto const file = dir.path() / "system" / "some-file.txt";
        fs::create_directories(file.parent_path());
        {
            auto stream = std::ofstream{file, std::ios::binary};
            stream << "content";
        }
        auto const found = env.findFile("some-file.txt");
        REQUIRE(found.has_value());
        CHECK_EQ(*found, file);

        // first match wins
        auto const fileSecond = dir.path() / "work" / "some-file.txt";
        {
            auto stream = std::ofstream{fileSecond, std::ios::binary};
            stream << "content";
        }
        auto const foundAgain = env.findFile("some-file.txt");
        REQUIRE(foundAgain.has_value());
        CHECK_EQ(*foundAgain, file);
    }

    TEST_CASE("unknown queue name is an error") {
        ::setenv("TURBOFW_TEST_USER", "john.doe", 1);

        auto dir = TempDir{};
        auto vars = std::map<std::string, std::string, std::less<>>{{"NAME", "my-service"}};
        vars.emplace("TURBOFW_TEST_DIR", dir.path().string());

        auto const envResult = Environment::load(writeConfig(dir, kFullConfig), vars);
        REQUIRE(envResult);

        auto const result = envResult->getQueue<turboq::MPSCMessageQueue::Producer>("unknown@queue");
        REQUIRE_FALSE(result);
        REQUIRE_NE(result.error().find("unknown queue \"unknown@queue\""), std::string::npos);
    }

    TEST_CASE("queue type mismatch is an error") {
        ::setenv("TURBOFW_TEST_USER", "john.doe", 1);

        auto dir = TempDir{};
        auto vars = std::map<std::string, std::string, std::less<>>{{"NAME", "my-service"}};
        vars.emplace("TURBOFW_TEST_DIR", dir.path().string());

        auto const envResult = Environment::load(writeConfig(dir, kFullConfig), vars);
        REQUIRE(envResult);

        auto const result = envResult->getQueue<turboq::MulticastMessageQueue::Producer>("metadata@cmd");
        REQUIRE_FALSE(result);
        REQUIRE_NE(result.error().find("is mpsc"), std::string::npos);
        REQUIRE_NE(result.error().find("multicast was requested"), std::string::npos);
    }

    TEST_CASE("second mpsc consumer is an error") {
        ::setenv("TURBOFW_TEST_USER", "john.doe", 1);

        auto dir = TempDir{};
        auto vars = std::map<std::string, std::string, std::less<>>{{"NAME", "my-service"}};
        vars.emplace("TURBOFW_TEST_DIR", dir.path().string());

        auto const envResult = Environment::load(writeConfig(dir, kFullConfig), vars);
        REQUIRE(envResult);

        auto const first = envResult->getQueue<turboq::MPSCMessageQueue::Consumer>("metadata@cmd");
        REQUIRE(first);
        auto const second = envResult->getQueue<turboq::MPSCMessageQueue::Consumer>("metadata@cmd");
        REQUIRE_FALSE(second);
    }

    TEST_CASE("unknown config key is an error") {
        auto dir = TempDir{};
        auto const config = writeConfig(dir, R"({
            "scope": "test",
            "unknown-key": 1
        })");

        auto const result = Environment::load(config);
        REQUIRE_FALSE(result);
        REQUIRE_NE(result.error().find("unknown key \"unknown-key\""), std::string::npos);
    }

    TEST_CASE("unknown variable is an error") {
        auto dir = TempDir{};
        auto const config = writeConfig(dir, R"({
            "scope": "${TURBOFW_TEST_UNKNOWN_VARIABLE_42}"
        })");

        auto const result = Environment::load(config);
        REQUIRE_FALSE(result);
        REQUIRE_NE(result.error().find("unknown variable \"TURBOFW_TEST_UNKNOWN_VARIABLE_42\""), std::string::npos);
    }

    TEST_CASE("bad queue type is an error") {
        auto dir = TempDir{};
        auto const config = writeConfig(dir, R"({
            "scope": "test",
            "queues": [
                { "name": "q@data", "type": "sneaky", "capacity": "1MiB" }
            ]
        })");

        auto const result = Environment::load(config);
        REQUIRE_FALSE(result);
        REQUIRE_NE(result.error().find("not pass validation"), std::string::npos);
    }

    TEST_CASE("missing capacity for multicast is an error") {
        auto dir = TempDir{};
        auto const config = writeConfig(dir, R"({
            "scope": "test",
            "queues": [
                { "name": "q@data", "type": "multicast" }
            ]
        })");

        auto const result = Environment::load(config);
        REQUIRE_FALSE(result);
        REQUIRE_NE(result.error().find("\"capacity\" is required"), std::string::npos);
    }

    TEST_CASE("missing slot-size for mpsc is an error") {
        auto dir = TempDir{};
        auto const config = writeConfig(dir, R"({
            "scope": "test",
            "queues": [
                { "name": "q@cmd", "type": "mpsc", "length": 128 }
            ]
        })");

        auto const result = Environment::load(config);
        REQUIRE_FALSE(result);
        REQUIRE_NE(result.error().find("\"length\" and \"slot-size\" are required"), std::string::npos);
    }

    TEST_CASE("capacity for mpsc is an error") {
        auto dir = TempDir{};
        auto const config = writeConfig(dir, R"({
            "scope": "test",
            "queues": [
                { "name": "q@cmd", "type": "mpsc", "length": 128, "slot-size": "1KiB", "capacity": "1MiB" }
            ]
        })");

        auto const result = Environment::load(config);
        REQUIRE_FALSE(result);
        REQUIRE_NE(result.error().find("\"capacity\" is only valid"), std::string::npos);
    }

    TEST_CASE("duplicated queue name is an error") {
        auto dir = TempDir{};
        auto const config = writeConfig(dir, R"({
            "scope": "test",
            "queues": [
                { "name": "q@cmd", "type": "mpsc", "length": 128, "slot-size": "1KiB" },
                { "name": "q@cmd", "type": "multicast", "capacity": "1MiB" }
            ]
        })");

        auto const result = Environment::load(config);
        REQUIRE_FALSE(result);
        REQUIRE_NE(result.error().find("duplicated queue name \"q@cmd\""), std::string::npos);
    }

    TEST_CASE("bad size literal is an error") {
        auto dir = TempDir{};
        auto const config = writeConfig(dir, R"({
            "scope": "test",
            "queues": [
                { "name": "q@data", "type": "multicast", "capacity": "1MB" }
            ]
        })");

        auto const result = Environment::load(config);
        REQUIRE_FALSE(result);
        REQUIRE_NE(result.error().find("invalid size literal"), std::string::npos);
    }

    TEST_CASE("bad queue name characters are an error") {
        auto dir = TempDir{};
        auto const config = writeConfig(dir, R"({
            "scope": "test",
            "queues": [
                { "name": "../escape", "type": "multicast", "capacity": "1MiB" }
            ]
        })");

        auto const result = Environment::load(config);
        REQUIRE_FALSE(result);
        REQUIRE_NE(result.error().find("not pass validation"), std::string::npos);
    }

    TEST_CASE("missing config file is an error") {
        auto dir = TempDir{};

        auto const result = Environment::load(dir.path() / "nope.json");
        REQUIRE_FALSE(result);
        REQUIRE_NE(result.error().find("nope.json"), std::string::npos);
    }

    TEST_CASE("minimal config without queues works") {
        auto dir = TempDir{};
        auto vars = std::map<std::string, std::string, std::less<>>{{"NAME", "my-service"}};
        vars.emplace("TURBOFW_TEST_DIR", dir.path().string());

        auto const config = writeConfig(dir, R"({
            "scope": "${SCOPE_UTILS}",
            "working-directory": "${TURBOFW_TEST_DIR}/work/${NAME}",
            "queues-opt": { "base": "${TURBOFW_TEST_DIR}/queues" }
        })");

        ::setenv("SCOPE_UTILS", "util-scope", 1);
        // scope itself can use ${NAME}
        auto const result = Environment::load(config, vars);
        REQUIRE(result);
        CHECK_EQ(result->scope(), "util-scope");

        // ${SCOPE} in working-directory is banned since scope is expanded first with variables only
        auto const config2 = writeConfig(dir, R"({
            "scope": "other",
            "working-directory": "${TURBOFW_TEST_DIR}/work/${SCOPE}"
        })");
        auto const result2 = Environment::load(config2, vars);
        REQUIRE_MESSAGE(result2, result2.error());
        CHECK_EQ(result2->workingDirectory(), dir.path() / "work" / "other");
    }
}

} // namespace turbofw::testing
