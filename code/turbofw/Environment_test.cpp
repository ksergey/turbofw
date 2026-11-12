// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <system_error>

#include <doctest/doctest.h>
#include <fmt/format.h>
#include <turboq/MPSCMessageQueue.h>
#include <turboq/MulticastMessageQueue.h>

#include "Environment.h"

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
        auto ec = std::error_code{};
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

auto makeVars(std::filesystem::path const& dir) -> Environment::Vars {
    ::setenv("TURBOFW_TEST_USER", "john.doe", 1);
    auto vars = Environment::Vars{{"NAME", "my-service"}};
    vars.emplace("TURBOFW_TEST_DIR", dir.string());
    return vars;
}

constexpr auto kFullConfig = R"({
    "scope": "${TURBOFW_TEST_USER}",
    "working-directory": "${TURBOFW_TEST_DIR}/work/${SCOPE}",
    "search-path": [ "${TURBOFW_TEST_DIR}/system", "${TURBOFW_TEST_DIR}/work" ],
    "queues": [
        { "name": "turbofw-env-test@data", "type": "multicast", "capacity": "1MiB" },
        { "name": "turbofw-env-test@cmd", "type": "mpsc", "length": 256, "slot-size": "2KiB" }
    ]
})";

} // namespace

TEST_SUITE("Environment") {
    TEST_CASE("load from json, expand vars, ${SCOPE} uses expanded value") {
        auto dir = TempDir{};

        auto const envResult = Environment::loadFromJson(kFullConfig, makeVars(dir.path()));
        REQUIRE_MESSAGE(envResult, envResult.error());

        auto const& env = envResult.value();
        CHECK_EQ(env.scope(), "john.doe");
        CHECK_EQ(env.workingDirectory(), dir.path() / "work" / "john.doe");
        REQUIRE_EQ(env.searchPath().size(), 2);
        CHECK_EQ(env.searchPath()[0], dir.path() / "system");
        REQUIRE_EQ(env.queues().size(), 2);

        // working directory is created (with expanded ${SCOPE} inside)
        CHECK(fs::is_directory(dir.path() / "work" / "john.doe"));

        // queue entries introspection
        CHECK_EQ(env.queues()[0].name, "turbofw-env-test@data");
        CHECK_EQ(env.queues()[1].name, "turbofw-env-test@cmd");
    }

    TEST_CASE("scope expansion falls back to process environment") {
        auto dir = TempDir{};

        auto const config = R"({ "scope": "${TURBOFW_TEST_USER}" })";
        auto const envResult = Environment::loadFromJson(config, Environment::Vars{});
        REQUIRE_MESSAGE(envResult, envResult.error());
        CHECK_EQ(envResult->scope(), "john.doe");
    }

    TEST_CASE("load from config file") {
        auto dir = TempDir{};

        auto const config = R"({ "scope": "file-scope" })";
        {
            auto stream = std::ofstream{dir.path() / "env.json", std::ios::binary};
            stream << config;
        }
        auto const envResult = Environment::loadFromConfig(dir.path() / "env.json");
        REQUIRE_MESSAGE(envResult, envResult.error());
        CHECK_EQ(envResult->scope(), "file-scope");
        CHECK_EQ(envResult->queues().size(), 0);
    }

    TEST_CASE("minimal config without queues works, nothing on disk is touched") {
        auto dir = TempDir{};
        auto envResult = Environment::loadFromJson(R"({ "scope": "test" })");
        REQUIRE_MESSAGE(envResult, envResult.error());
        CHECK_EQ(envResult->queues().size(), 0);
        CHECK_EQ(envResult->workingDirectory(), std::filesystem::path{});
    }

    TEST_CASE("findFile searches search-path, first match wins") {
        auto dir = TempDir{};

        auto const envResult = Environment::loadFromJson(kFullConfig, makeVars(dir.path()));
        REQUIRE_MESSAGE(envResult, envResult.error());
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
        fs::create_directories(fileSecond.parent_path());
        {
            auto stream = std::ofstream{fileSecond, std::ios::binary};
            stream << "content";
        }
        auto const foundAgain = env.findFile("some-file.txt");
        REQUIRE(foundAgain.has_value());
        CHECK_EQ(*foundAgain, file);
    }

    TEST_CASE("unknown queue name, queue type mismatch") {
        auto dir = TempDir{};

        auto const envResult = Environment::loadFromJson(kFullConfig, makeVars(dir.path()));
        REQUIRE_MESSAGE(envResult, envResult.error());
        auto const& env = envResult.value();

        auto const unknown = env.createProducer<turboq::MPSCMessageQueue::Producer>("unknown@queue", true);
        REQUIRE_FALSE(unknown);
        REQUIRE_NE(unknown.error().find("unknown queue \"unknown@queue\""), std::string::npos);

        auto const mismatch = env.createProducer<turboq::MPSCMessageQueue::Producer>("turbofw-env-test@data", true);
        REQUIRE_FALSE(mismatch);
        REQUIRE_NE(mismatch.error().find("is not mpsc queue"), std::string::npos);
    }

    TEST_CASE("duplicated queue name is an error") {
        auto dir = TempDir{};

        auto const config = R"({
            "scope": "test",
            "queues": [
                { "name": "q@cmd", "type": "mpsc", "length": 64, "slot-size": "1KiB" },
                { "name": "q@cmd", "type": "multicast", "capacity": "1MiB" }
            ]
        })";
        auto const result = Environment::loadFromJson(config);
        REQUIRE_FALSE(result);
        REQUIRE_NE(result.error().find("duplicated queue name \"q@cmd\""), std::string::npos);
    }

    TEST_CASE("config validation errors") {
        auto const check = [](std::string_view config, std::string_view expectedError, Environment::Vars vars = {}) {
            auto const result = Environment::loadFromJson(config, std::move(vars));
            REQUIRE_FALSE_MESSAGE(result.has_value(), config);
            REQUIRE_NE(result.error().find(expectedError), std::string::npos);
        };

        check(R"({ "scope": "test", "unknownKey": 1 })", "unknown key \"unknownKey\"");
        check(R"({ "scope": "${NOPE_42}" })", "unknown variable \"NOPE_42\"");
        check(R"({ "scope": "" })", "\"scope\""); // empty value: must match scope-name validation
        check(R"({ "scope": "have space" })", "\"scope\" \"have space\"");
        check(R"({ "scope": "test", "queues": [ { "name": "q@data", "type": "sneaky", "capacity": "1KiB" } ] })",
            "not pass validation");
        check(R"({ "scope": "test", "queues": [ { "name": "../escape", "type": "multicast" } ] })",
            "not pass validation");
        check(R"({ "scope": "test", "queues": [ { "name": "q@data", "type": "multicast" } ] })",
            "\"capacity\" is required");
        check(
            R"({ "scope": "test", "queues": [ { "name": "q@data", "type": "multicast", "capacity": "1MiB", "length": 4 } ] })",
            "\"length\" and \"slot-size\" are only valid");
        check(R"({ "scope": "test", "queues": [ { "name": "q@data", "type": "multicast", "capacity": "0B" } ] })",
            "must be greater than zero");
        check(R"({ "scope": "test", "queues": [ { "name": "q@cmd", "type": "mpsc", "length": 64 } ] })",
            "\"length\" and \"slot-size\" are required");
        check(
            R"({ "scope": "test", "queues": [ { "name": "q@cmd", "type": "mpsc", "length": 0, "slot-size": "1KiB" } ] })",
            "must be greater than zero");
        check(
            R"({ "scope": "test", "queues": [ { "name": "q@cmd", "type": "mpsc", "length": 64, "slot-size": "1KiB", "capacity": "1KiB" } ] })",
            "\"capacity\" is only valid");
        check(R"({ "scope": "test", "queues": [ { "name": "q@cmd", "type": "multicast", "capacity": "1MB" } ] })",
            "invalid size literal");
        // "working-directory" expanding to an empty value is an error
        check(R"({ "scope": "test", "working-directory": "${REQUIRED}" })", "must not be empty",
            Environment::Vars{{"REQUIRED", ""}});
        check(R"({})", "\"scope\"");
    }

    TEST_CASE("missing config file is an error") {
        auto dir = TempDir{};

        auto const result = Environment::loadFromConfig(dir.path() / "nope.json");
        REQUIRE_FALSE(result);
        REQUIRE_NE(result.error().find("nope.json"), std::string::npos);
    }

    // Queue opening tests need a real shared memory mount, skipped when unavailable
    TEST_CASE("queue open-or-create and round trips") {
        if (!fs::exists("/dev/shm")) {
            MESSAGE("skip: /dev/shm is not available");
            return;
        }

        auto dir = TempDir{};
        auto const envResult = Environment::loadFromJson(kFullConfig, makeVars(dir.path()));
        REQUIRE_MESSAGE(envResult, envResult.error());
        auto const& env = envResult.value();

        // mpsc round trip: create + payload + consume
        {
            auto producer = env.createProducer<turboq::MPSCMessageQueue::Producer>("turbofw-env-test@cmd", false);
            REQUIRE_MESSAGE(producer, producer.error());

            auto consumer = env.createConsumer<turboq::MPSCMessageQueue::Consumer>("turbofw-env-test@cmd", false);
            REQUIRE_MESSAGE(consumer, consumer.error());

            auto const payload = std::uint64_t{42};
            auto const prepared = producer->prepare(sizeof(payload));
            REQUIRE(prepared.size() >= sizeof(payload));
            *reinterpret_cast<std::uint64_t*>(prepared.data()) = payload;
            producer->commit();

            auto const fetched = consumer->fetch();
            REQUIRE(fetched.size() >= sizeof(payload));
            CHECK_EQ(*reinterpret_cast<std::uint64_t const*>(fetched.data()), payload);
            consumer->consume();
        }

        // queue exists now: openOnly must succeed
        {
            auto producer = env.createProducer<turboq::MPSCMessageQueue::Producer>("turbofw-env-test@cmd", true);
            REQUIRE_MESSAGE(producer, producer.error());
        }

        // multicast round trip
        {
            auto producer = env.createProducer<turboq::MulticastMessageQueue::Producer>("turbofw-env-test@data", false);
            REQUIRE_MESSAGE(producer, producer.error());

            auto consumer = env.createConsumer<turboq::MulticastMessageQueue::Consumer>("turbofw-env-test@data", false);
            REQUIRE_MESSAGE(consumer, consumer.error());

            auto const prepared = producer->prepare(64);
            REQUIRE(prepared.size() == 64);
            producer->commit();

            auto const fetched = consumer->fetch();
            REQUIRE_NE(fetched.size(), 0);
            consumer->consume();
        }
    }

    TEST_CASE("queue reopen with mismatched config options fails") {
        if (!fs::exists("/dev/shm")) {
            MESSAGE("skip: /dev/shm is not available");
            return;
        }

        auto const makeConfig = [](std::uint64_t length, std::string_view slotSize) {
            return fmt::format(R"({{
                "scope": "test",
                "queues": [ {{ "name": "turbofw-env-test@opts", "type": "mpsc",
                              "length": {}, "slot-size": "{}" }} ]
            }})",
                length, slotSize);
        };

        // create the queue with one set of options
        auto const first = Environment::loadFromJson(makeConfig(64, "2KiB"));
        REQUIRE_MESSAGE(first, first.error());
        auto const queue = first->createProducer<turboq::MPSCMessageQueue::Producer>("turbofw-env-test@opts", false);
        REQUIRE_MESSAGE(queue, queue.error());

        // opening with a different length/slot-size is a size mismatch; the error text comes
        // from turboq (a size-mismatch error code) and does not carry the queue name
        auto const second = Environment::loadFromJson(makeConfig(256, "1KiB"));
        REQUIRE_MESSAGE(second, second.error());
        auto const reopened =
            second->createProducer<turboq::MPSCMessageQueue::Producer>("turbofw-env-test@opts", false);
        REQUIRE_FALSE_MESSAGE(reopened, "reopen with different options must fail");
        REQUIRE_FALSE(reopened.error().empty());
    }
}

} // namespace turbofw::testing
