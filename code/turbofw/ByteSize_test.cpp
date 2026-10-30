// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <cstdint>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "ByteSize.h"
#include "config/Json.h"

namespace turbofw {

TEST_SUITE("ByteSize") {
    TEST_CASE("parse units") {
        REQUIRE_EQ(ByteSize::fromString("0B").value().bytes, 0);
        REQUIRE_EQ(ByteSize::fromString("1000B").value().bytes, 1000);
        REQUIRE_EQ(ByteSize::fromString("100B").value().bytes, 100);
        REQUIRE_EQ(ByteSize::fromString("4KiB").value().bytes, std::uint64_t{4} * 1024);
        REQUIRE_EQ(ByteSize::fromString("100MiB").value().bytes, std::uint64_t{100} * 1024 * 1024);
        REQUIRE_EQ(ByteSize::fromString("1GiB").value().bytes, std::uint64_t{1} * 1024 * 1024 * 1024);
        REQUIRE_EQ(ByteSize::fromString("1TiB").value().bytes, std::uint64_t{1} * 1024 * 1024 * 1024 * 1024);
    }

    TEST_CASE("bare numbers are bytes") {
        REQUIRE_EQ(ByteSize::fromString("512").value().bytes, 512);
        REQUIRE_EQ(ByteSize::fromString("0").value().bytes, 0);
    }

    TEST_CASE("spaces are trimmed") {
        REQUIRE_EQ(ByteSize::fromString(" 1KiB ").value().bytes, 1024);
    }

    TEST_CASE("invalid literals are errors") {
        REQUIRE_FALSE(ByteSize::fromString("").has_value());
        REQUIRE_FALSE(ByteSize::fromString("100MB").has_value());
        REQUIRE_FALSE(ByteSize::fromString("100kib").has_value());
        REQUIRE_FALSE(ByteSize::fromString("100XB").has_value());
        REQUIRE_FALSE(ByteSize::fromString("1.5MiB").has_value());
        REQUIRE_FALSE(ByteSize::fromString("MiB").has_value());
        REQUIRE_FALSE(ByteSize::fromString("18446744073709551616MiB").has_value());

        auto const result = ByteSize::fromString("1MB");
        REQUIRE_FALSE(result.has_value());
        REQUIRE_NE(result.error().find("invalid size literal"), std::string::npos);
    }

    TEST_CASE("json mapping: string and number") {
        auto value = ByteSize{};

        turbofw::config::JsonMapping<ByteSize>::read(nlohmann::json::parse(R"("100MiB")"), value);
        REQUIRE_EQ(value.bytes, std::uint64_t{100} * 1024 * 1024);

        turbofw::config::JsonMapping<ByteSize>::read(nlohmann::json::parse("4096"), value);
        REQUIRE_EQ(value.bytes, 4096);

        nlohmann::json json;
        turbofw::config::JsonMapping<ByteSize>::write(json, ByteSize{2048});
        REQUIRE_EQ(json.is_number_unsigned(), true);
        REQUIRE_EQ(json.get<std::uint64_t>(), 2048);

        REQUIRE_THROWS_AS(
            (void)turbofw::config::JsonMapping<ByteSize>::read(nlohmann::json::parse(R"("1MB")"), value),
            turbofw::config::Exception);
    }
}

} // namespace turbofw
