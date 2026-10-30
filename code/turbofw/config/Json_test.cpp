// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "Json.h"
#include "Validator.h"

using namespace std::string_view_literals;

namespace turbofw::config {

struct ObjectA {
    std::string path;
    std::int32_t count = 0;
    std::optional<std::int32_t> size;

    template <typename DTO>
    void serialize(DTO& dto) {
        // clang-format off
        dto
            & value<"path">(&path)
            & value<"count">(&count).defaultValue(45)
            & value<"size">(&size).defaultValue(std::nullopt)
        ;
        // clang-format on
    }
};

struct ObjectB {
    std::string status;
    std::vector<std::string> events;
    int sourceID = 0;
};

template <typename DTO>
void serialize(ObjectB& obj, DTO& dto) {
    // clang-format off
    dto
        & value<"status">(&obj.status).defaultValue("nothing")
        & value<"events">(&obj.events).defaultValue(std::vector<std::string>{})
        & value<"sourceID">(&obj.sourceID).defaultValue(77)
    ;
    // clang-format on
}

struct ObjectC {
    std::string status;
    int entityID = 0;
};

template <typename DTO>
void serialize(ObjectC& obj, DTO& dto) {
    // clang-format off
    dto
        & value<"status">(&obj.status)
            .defaultValue("none")
            .validate(oneOf("init"sv, "new"sv, "cancelled"sv, "replaced"sv))
        & value<"entityID">(&obj.entityID)
    ;
    // clang-format on
}

/// ObjectC with unknown nested key test
struct OuterStrict {
    ObjectC inner;

    template <typename DTO>
    void serialize(DTO& dto) {
        // clang-format off
        dto & value<"inner">(&inner);
        // clang-format on
    }
};

struct QueueEntry {
    std::string name;
    std::string type;
    std::vector<std::string> tags;

    template <typename DTO>
    void serialize(DTO& dto) {
        // clang-format off
        dto
            & value<"name">(&name)
            & value<"type">(&type).validate(oneOf("mpsc"sv, "multicast"sv))
            & value<"tags">(&tags).defaultValue(std::vector<std::string>{})
        ;
        // clang-format on
    }
};

struct QueueListConfig {
    std::vector<QueueEntry> queues;

    template <typename DTO>
    void serialize(DTO& dto) {
        // clang-format off
        dto & value<"queues">(&queues);
        // clang-format on
    }
};

struct ItemListConfig {
    std::vector<ObjectC> items;

    template <typename DTO>
    void serialize(DTO& dto) {
        // clang-format off
        dto & value<"items">(&items);
        // clang-format on
    }
};

struct PathConfig {
    std::filesystem::path root;
    std::vector<std::filesystem::path> searchPath;

    template <typename DTO>
    void serialize(DTO& dto) {
        // clang-format off
        dto
            & value<"root">(&root)
            & value<"search-path">(&searchPath).defaultValue(std::vector<std::filesystem::path>{})
        ;
        // clang-format on
    }
};

struct DurationConfig {
    std::chrono::milliseconds timeout{};

    template <typename DTO>
    void serialize(DTO& dto) {
        // clang-format off
        dto & value<"timeout">(&timeout).defaultValue(std::chrono::milliseconds{100});
        // clang-format on
    }
};

TEST_SUITE("config") {
    TEST_CASE("read object, write object") {
        auto result = ObjectA{};

        auto const json0 = nlohmann::json::parse(R"({"path" : "/dev/shm", "count" : 17})");
        JsonMapping<ObjectA>::read(json0, result);

        REQUIRE_EQ(result.path, "/dev/shm");
        REQUIRE_EQ(result.count, 17);

        auto const json1 = nlohmann::json::parse(R"({"path" : "/dev", "size" : 120})");
        JsonMapping<ObjectA>::read(json1, result);

        REQUIRE_EQ(result.path, "/dev");
        REQUIRE_EQ(result.count, 45);
        REQUIRE(result.size.has_value());
        REQUIRE_EQ(result.size.value(), 120);
    }

    TEST_CASE("free-function serialize, defaults, scalar instead of list") {
        auto config = ObjectB{};

        {
            auto const result = fromJson(config, R"({
                "status": "draw",
                "events": [ "line", "circle" ]
            })");

            REQUIRE(result);
            REQUIRE_EQ(config.status, "draw");
            REQUIRE_EQ(config.sourceID, 77);

            auto const out = toJson(config);
            REQUIRE(out);
            REQUIRE_NE(out->find("sourceID"), std::string::npos);
        }

        {
            config = ObjectB{};
            auto const result = fromJson(config, R"({
                "status": "draw",
                "events": "dot",
                "sourceID": 133
            })");

            REQUIRE(result);
            REQUIRE_EQ(config.status, "draw");
            REQUIRE_EQ(config.sourceID, 133);
            REQUIRE_EQ(config.events.size(), 1);
            REQUIRE_EQ(config.events.front(), "dot");
        }
    }

    TEST_CASE("validator rejects unknown enum value") {
        auto config = ObjectC{};

        auto const result = fromJson(config, R"({
            "status": "draw",
            "entityID": 1000
        })");

        REQUIRE_FALSE(result);
        REQUIRE_NE(result.error().find("not pass validation"), std::string::npos);
    }

    TEST_CASE("strict mode rejects unknown root key") {
        auto config = ObjectC{};

        auto const result = fromJson(config, R"({
            "status": "new",
            "entityID": 1,
            "unknownKey": 55
        })");

        REQUIRE_FALSE(result);
        REQUIRE_NE(result.error().find("unknown key \"unknownKey\""), std::string::npos);
    }

    TEST_CASE("strict mode rejects unknown nested key") {
        auto config = OuterStrict{};

        auto const result = fromJson(config, R"({
            "inner": {
                "status": "new",
                "entityID": 2,
                "extra": true
            }
        })");

        REQUIRE_FALSE(result);
        REQUIRE_NE(result.error().find("unknown key \"extra\""), std::string::npos);
    }

    TEST_CASE("nested object with vector of objects") {
        auto config = QueueListConfig{};
        auto const result = fromJson(config, R"({
            "queues": [
                { "name": "metadata@data", "type": "multicast" },
                { "name": "metadata@cmd", "type": "mpsc", "tags": ["a", "b"] }
            ]
        })");

        REQUIRE(result);
        REQUIRE_EQ(config.queues.size(), 2);
        REQUIRE_EQ(config.queues[0].name, "metadata@data");
        REQUIRE_EQ(config.queues[0].type, "multicast");
        REQUIRE(config.queues[0].tags.empty());
        REQUIRE_EQ(config.queues[1].tags.size(), 2);
        REQUIRE_EQ(config.queues[1].tags.back(), "b");
    }

    TEST_CASE("bad type value rejects whole config") {
        auto config = ItemListConfig{};
        auto const result = fromJson(config, R"({
            "items": [
                { "status": "init", "entityID": 1 },
                { "status": "draw", "entityID": 2 }
            ]
        })");

        REQUIRE_FALSE(result);
    }

    TEST_CASE("missing required field is an error") {
        auto config = ObjectA{};

        auto const result = fromJson(config, R"({ "count" : 1 })");

        REQUIRE_FALSE(result);
        REQUIRE_NE(result.error().find("path"), std::string::npos);
    }

    TEST_CASE("comments in json content are ignored") {
        auto config = ObjectA{};

        auto const result = fromJson(config, R"({
            // market data queue
            "path": "/dev/shm",
            "count": 3 // trailing
        })");

        REQUIRE(result);
        REQUIRE_EQ(config.path, "/dev/shm");
        REQUIRE_EQ(config.count, 3);
    }

    TEST_CASE("filesystem path mapping") {
        auto config = PathConfig{};
        auto const result = fromJson(config, R"({
            "root": "/var/local/turbofw",
            "search-path": [ "/etc/system", "/var/data" ]
        })");

        REQUIRE(result);
        REQUIRE_EQ(config.root.string(), "/var/local/turbofw");
        REQUIRE_EQ(config.searchPath.size(), 2);
        REQUIRE_EQ(config.searchPath.back().string(), "/var/data");
    }

    TEST_CASE("chrono duration mapping") {
        auto config = DurationConfig{};
        auto result = fromJson(config, R"({ "timeout": 550 })");

        REQUIRE(result);
        REQUIRE_EQ(config.timeout.count(), 550);

        result = fromJson(config, R"({})");

        REQUIRE(result);
        REQUIRE_EQ(config.timeout.count(), 100);
    }

    TEST_CASE("broken json content is an error") {
        auto config = ObjectA{};

        auto const result = fromJson(config, R"json({ "path": "x" )json");

        REQUIRE_FALSE(result);
    }

    TEST_CASE("missing file is an error") {
        auto config = ObjectA{};

        auto const result = fromJsonFile(config, "/nonexistent/turbofw/config.json");

        REQUIRE_FALSE(result);
        REQUIRE_NE(result.error().find("nonexistent"), std::string::npos);
    }
}

} // namespace turbofw::config
