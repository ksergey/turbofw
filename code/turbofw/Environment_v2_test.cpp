// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <system_error>

#include <doctest/doctest.h>
#include <turboq/MPSCMessageQueue.h>
#include <turboq/MulticastMessageQueue.h>

#include "Environment_v2.h"
#include "Service_v2.h"

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
            path_ = fs::temp_directory_path() / ("turbofw-env-v2-test-" + std::to_string(dist(engine)));
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

constexpr auto kFullConfig = R"({
    "scope": "dev",
    "memory-source": "${TURBOFW_V2_TEST_DIR}/queues",
    "queues": [
        // everything the service publishes
        { "name": "${SCOPE}/metadata@data", "type": "multicast", "capacity": "1MiB" },
        // requests to the service
        { "name": "${SCOPE}/metadata@cmd", "type": "mpsc", "length": 256, "slot-size": "2KiB" },
        { "name": "${SCOPE}/extra", "type": "spsc", "capacity": "64KiB" }
    ],
    "services": [
        {
            "name": "metadata",
            "service-id": 1,
            "queues": {
                "data": "${SCOPE}/metadata@data",
                "cmd": "${SCOPE}/metadata@cmd"
            }
        },
        {
            "name": "other",
            "service-id": 2,
            "queues": {
                "data": "${SCOPE}/extra"
            }
        }
    ]
})";

} // namespace

/// Call a [[nodiscard]] accessor discarding the result (for CHECK_THROWS_AS)
template <typename F>
void ignoreResult(F&& fn) {
    static_cast<void>(std::invoke(std::forward<F>(fn)));
}

TEST_SUITE("Environment (v2)") {
    TEST_CASE("access expands variables at call time") {
        auto dir = TempDir{};
        ::setenv("TURBOFW_V2_TEST_DIR", dir.path().string().c_str(), 1);

        auto const envResult = v2::Environment::loadFromJson(kFullConfig);
        REQUIRE_MESSAGE(envResult, envResult.error());
        auto const& env = envResult.value();

        CHECK_EQ(env.scope(), "dev");
        CHECK_EQ(env.memorySourceMount(), dir.path() / "queues");

        auto const queues = env.queues();
        REQUIRE_EQ(queues.size(), 3);
        CHECK_EQ(queues[0].name, "dev/metadata@data");
        CHECK_EQ(queues[0].type, v2::QueueEntry::Type::Multicast);
        CHECK_EQ(std::get<turboq::MulticastMessageQueue::CreationOptions>(queues[0].creationOptions).capacityHint,
            1u * 1024 * 1024);
        CHECK_EQ(queues[1].name, "dev/metadata@cmd");
        CHECK_EQ(queues[1].type, v2::QueueEntry::Type::MPSC);
        CHECK_EQ(std::get<turboq::MPSCMessageQueue::CreationOptions>(queues[1].creationOptions).lengthHint, 256u);
        CHECK_EQ(
            std::get<turboq::MPSCMessageQueue::CreationOptions>(queues[1].creationOptions).slotSizeHint, 2u * 1024);
        CHECK_EQ(queues[2].name, "dev/extra");
        CHECK_EQ(queues[2].type, v2::QueueEntry::Type::SPSC);

        auto const services = env.services();
        REQUIRE_EQ(services.size(), 2);
        CHECK_EQ(services[0].name, "metadata");
        CHECK_EQ(services[0].serviceId, 1);
        REQUIRE_EQ(services[0].queues.size(), 2);
        CHECK_EQ(services[0].queues.at("data"), "dev/metadata@data");
        CHECK_EQ(services[0].queues.at("cmd"), "dev/metadata@cmd");
        CHECK_EQ(services[1].name, "other");
        CHECK_EQ(services[1].serviceId, 2);

        // the queues were NOT created on disk by the accessors
        CHECK_FALSE(fs::exists(dir.path() / "queues"));

        auto const found = env.findService("other");
        REQUIRE(found);
        CHECK_EQ(found->serviceId, 2);
        CHECK_EQ(found->queues.at("data"), "dev/extra");
        CHECK_EQ(env.findService("missing"), std::nullopt);

        ::unsetenv("TURBOFW_V2_TEST_DIR");
    }

    TEST_CASE("a variable change is picked up without a reload") {
        auto const config = R"({ "scope": "${TURBOFW_V2_TEST_VAR}" })";

        auto const first = v2::Environment::loadFromJson(config);
        REQUIRE_MESSAGE(first, first.error());
        ::setenv("TURBOFW_V2_TEST_VAR", "before", 1);
        CHECK_EQ(first->scope(), "before");
        ::setenv("TURBOFW_V2_TEST_VAR", "after", 1);
        CHECK_EQ(first->scope(), "after");
        ::unsetenv("TURBOFW_V2_TEST_VAR");
    }

    TEST_CASE("user vars win over the process environment") {
        auto const config = R"({ "scope": "${TURBOFW_V2_TEST_VAR}" })";
        ::setenv("TURBOFW_V2_TEST_VAR", "from-process-env", 1);

        auto const envResult = v2::Environment::loadFromJson(config, v2::Vars{{"TURBOFW_V2_TEST_VAR", "from-vars"}});
        REQUIRE_MESSAGE(envResult, envResult.error());
        CHECK_EQ(envResult->scope(), "from-vars");

        ::unsetenv("TURBOFW_V2_TEST_VAR");
    }

    TEST_CASE("${SCOPE} uses the expanded scope") {
        auto const config = R"({
            "scope": "${TURBOFW_V2_TEST_VAR}",
            "queues": [ { "name": "${SCOPE}/q", "type": "multicast", "capacity": "1KiB" } ],
            "services": []
        })";
        ::setenv("TURBOFW_V2_TEST_VAR", "x", 1);

        auto const envResult = v2::Environment::loadFromJson(config);
        REQUIRE_MESSAGE(envResult, envResult.error());
        REQUIRE_EQ(envResult->queues().size(), 1);
        CHECK_EQ(envResult->queues()[0].name, "x/q");

        ::unsetenv("TURBOFW_V2_TEST_VAR");
    }

    TEST_CASE("unknown variable, bad name or bad options throw at access") {
        SUBCASE("unknown variable") {
            auto const envResult = v2::Environment::loadFromJson(R"({
                "scope": "dev",
                "queues": [ { "name": "${NOPE}", "type": "multicast", "capacity": "1KiB" } ],
                "services": []
            })");
            REQUIRE_MESSAGE(envResult, envResult.error());
            CHECK_THROWS_AS(ignoreResult([&] {
                return envResult->queues();
            }),
                std::runtime_error);
        }

        SUBCASE("unterminated variable") {
            auto const envResult = v2::Environment::loadFromJson(R"({
                "scope": "dev",
                "queues": [ { "name": "q${", "type": "multicast", "capacity": "1KiB" } ],
                "services": []
            })");
            REQUIRE_MESSAGE(envResult, envResult.error());
            CHECK_THROWS_AS(ignoreResult([&] {
                return envResult->queues();
            }),
                std::runtime_error);
        }

        SUBCASE("queue name escaping the mount") {
            auto const envResult = v2::Environment::loadFromJson(R"({
                "scope": "dev",
                "queues": [ { "name": "../escape", "type": "multicast", "capacity": "1KiB" } ],
                "services": []
            })");
            REQUIRE_MESSAGE(envResult, envResult.error());
            CHECK_THROWS_AS(ignoreResult([&] {
                return envResult->queues();
            }),
                std::runtime_error);
        }

        SUBCASE("missing multicast capacity") {
            auto const envResult = v2::Environment::loadFromJson(R"({
                "scope": "dev",
                "queues": [ { "name": "q", "type": "multicast" } ],
                "services": []
            })");
            REQUIRE_MESSAGE(envResult, envResult.error());
            CHECK_THROWS_AS(ignoreResult([&] {
                return envResult->queues();
            }),
                std::runtime_error);
        }

        SUBCASE("mpsc with capacity instead of length/slot-size") {
            auto const envResult = v2::Environment::loadFromJson(R"({
                "scope": "dev",
                "queues": [ { "name": "q", "type": "mpsc", "capacity": "1KiB" } ],
                "services": []
            })");
            REQUIRE_MESSAGE(envResult, envResult.error());
            CHECK_THROWS_AS(ignoreResult([&] {
                return envResult->queues();
            }),
                std::runtime_error);
        }

        SUBCASE("service-id out of range") {
            auto const envResult = v2::Environment::loadFromJson(R"({
                "scope": "dev",
                "queues": [],
                "services": [ { "name": "s", "service-id": 65536, "queues": {} } ]
            })");
            REQUIRE_MESSAGE(envResult, envResult.error());
            CHECK_THROWS_AS(ignoreResult([&] {
                return envResult->services();
            }),
                std::runtime_error);
        }

        SUBCASE("empty memory-source") {
            auto const envResult = v2::Environment::loadFromJson(R"({
                "scope": "dev",
                "memory-source": "",
                "queues": [],
                "services": []
            })");
            REQUIRE_MESSAGE(envResult, envResult.error());
            CHECK_THROWS_AS(ignoreResult([&] {
                return envResult->memorySourceMount();
            }),
                std::runtime_error);
        }
    }

    TEST_CASE("shape validation happens only at creation") {
        SUBCASE("not a json object") {
            CHECK_FALSE(v2::Environment::loadFromJson("[]"));
        }
        SUBCASE("queues and services are optional") {
            CHECK_FALSE(v2::Environment::loadFromJson(R"({})")); // "scope" is required
            REQUIRE_MESSAGE(
                v2::Environment::loadFromJson(R"({ "scope": "dev" })"), "\"queues\" and \"services\" absent = empty");
            REQUIRE_MESSAGE(
                v2::Environment::loadFromJson(R"({ "scope": "dev", "queues": [] })"), "\"services\" absent = empty");
        }
        SUBCASE("bad section kinds") {
            CHECK_FALSE(v2::Environment::loadFromJson(R"({ "scope": 1, "queues": [], "services": [] })"));
            CHECK_FALSE(v2::Environment::loadFromJson(R"({ "scope": "dev", "queues": {}, "services": [] })"));
            CHECK_FALSE(v2::Environment::loadFromJson(R"({ "scope": "dev", "queues": [], "services": {} })"));
        }
        SUBCASE("bad queue entry") {
            CHECK_FALSE(
                v2::Environment::loadFromJson(R"({ "scope": "dev", "queues": [ { "name": "q" } ], "services": [] })"));
            CHECK_FALSE(v2::Environment::loadFromJson(R"({
                "scope": "dev",
                "queues": [ { "name": "q", "type": "unknown" } ],
                "services": []
            })"));
        }
        SUBCASE("bad service entry") {
            CHECK_FALSE(v2::Environment::loadFromJson(R"({
                "scope": "dev",
                "queues": [],
                "services": [ { "name": "s", "service-id": "one", "queues": {} } ]
            })"));
            CHECK_FALSE(v2::Environment::loadFromJson(R"({
                "scope": "dev",
                "queues": [],
                "services": [ { "name": "s", "service-id": 1, "queues": { "data": 1 } } ]
            })"));
        }
        SUBCASE("bad memory-source kind") {
            CHECK_FALSE(v2::Environment::loadFromJson(
                R"({ "scope": "dev", "memory-source": 1, "queues": [], "services": [] })"));
        }
        SUBCASE("no cross-references at creation: a service may name an undefined queue") {
            auto const envResult = v2::Environment::loadFromJson(R"({
                "scope": "dev",
                "queues": [],
                "services": [ { "name": "s", "service-id": 1, "queues": { "data": "nowhere" } } ]
            })");
            REQUIRE_MESSAGE(envResult, envResult.error());
        }
    }

    TEST_CASE("scope charset is validated at access") {
        auto const envResult = v2::Environment::loadFromJson(R"({
            "scope": "${TURBOFW_V2_TEST_VAR}",
            "queues": [],
            "services": []
        })");
        REQUIRE_MESSAGE(envResult, envResult.error());

        ::setenv("TURBOFW_V2_TEST_VAR", "bad/scope", 1);
        CHECK_THROWS_AS(ignoreResult([&] {
            return envResult->scope();
        }),
            std::runtime_error);
        ::setenv("TURBOFW_V2_TEST_VAR", "good", 1);
        CHECK_EQ(envResult->scope(), "good");

        ::unsetenv("TURBOFW_V2_TEST_VAR");
    }

    TEST_CASE("load from file") {
        auto dir = TempDir{};
        {
            auto stream = std::ofstream{dir.path() / "env.json", std::ios::binary};
            stream << R"({ "scope": "from-file", "queues": [], "services": [] })";
        }
        auto const envResult = v2::Environment::loadFromFile(dir.path() / "env.json");
        REQUIRE_MESSAGE(envResult, envResult.error());
        CHECK_EQ(envResult->scope(), "from-file");

        CHECK_FALSE(v2::Environment::loadFromFile(dir.path() / "missing.json"));
    }

    TEST_CASE("Service creates its producer and consumer") {
        auto dir = TempDir{};
        ::setenv("TURBOFW_V2_TEST_DIR", dir.path().string().c_str(), 1);

        auto const envResult = v2::Environment::loadFromJson(kFullConfig);
        REQUIRE_MESSAGE(envResult, envResult.error());

        auto const service = envResult->service("metadata");
        CHECK_EQ(service.name(), "metadata");
        CHECK_EQ(service.serviceId(), 1);
        REQUIRE_EQ(service.queues().size(), 2);
        CHECK_EQ(service.queues().at("data"), "dev/metadata@data");

        auto const producer = service.createProducer<turboq::MulticastMessageQueue::Producer>();
        REQUIRE_MESSAGE(producer, producer.error());
        auto const consumer = service.createConsumer<turboq::MPSCMessageQueue::Consumer>();
        REQUIRE_MESSAGE(consumer, consumer.error());

        // the queues are on disk now (the memory source created the parent directories)
        CHECK(fs::exists(dir.path() / "queues" / "dev" / "metadata@data"));
        CHECK(fs::exists(dir.path() / "queues" / "dev" / "metadata@cmd"));

        ::unsetenv("TURBOFW_V2_TEST_DIR");
    }

    TEST_CASE("Service errors") {
        auto dir = TempDir{};
        ::setenv("TURBOFW_V2_TEST_DIR", dir.path().string().c_str(), 1);

        auto const envResult = v2::Environment::loadFromJson(kFullConfig);
        REQUIRE_MESSAGE(envResult, envResult.error());

        SUBCASE("unknown service") {
            CHECK_THROWS_AS(ignoreResult([&] {
                return envResult->service("missing");
            }),
                std::runtime_error);
        }

        SUBCASE("wrong queue type") {
            // the "data" role of "other" points to an spsc queue
            auto const service = envResult->service("other");
            auto const producer = service.createProducer<turboq::MulticastMessageQueue::Producer>();
            REQUIRE_FALSE(producer);
            CHECK_NE(producer.error().find("is not a multicast queue"), std::string::npos);
        }

        SUBCASE("missing role") {
            // "other" has no "cmd" role
            auto const service = envResult->service("other");
            auto const consumer = service.createConsumer<turboq::MPSCMessageQueue::Consumer>();
            REQUIRE_FALSE(consumer);
            CHECK_NE(consumer.error().find("no \"cmd\" queue"), std::string::npos);
        }

        SUBCASE("queue not defined in \"queues\"") {
            // no cross-reference validation at load: the error surfaces here
            auto const config = R"({
                "scope": "dev",
                "memory-source": "${TURBOFW_V2_TEST_DIR}/queues",
                "queues": [],
                "services": [ { "name": "s", "service-id": 1, "queues": { "data": "nowhere" } } ]
            })";
            auto const bare = v2::Environment::loadFromJson(config);
            REQUIRE_MESSAGE(bare, bare.error());
            auto const service = bare->service("s");
            auto const producer = service.createProducer<turboq::MulticastMessageQueue::Producer>();
            REQUIRE_FALSE(producer);
            CHECK_NE(producer.error().find("is not a multicast queue"), std::string::npos);
        }

        ::unsetenv("TURBOFW_V2_TEST_DIR");
    }
}

} // namespace turbofw::testing
