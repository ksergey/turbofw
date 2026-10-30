// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <cstdint>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include "ByteSize.h"

namespace turbofw::config {

TEST_SUITE("config/ByteSize") {
    TEST_CASE("parse units") {
        REQUIRE_EQ(ByteSize::parse("0B").bytes, 0);
        REQUIRE_EQ(ByteSize::parse("1000B").bytes, 1000);
        REQUIRE_EQ(ByteSize::parse("100B").bytes, 100);
        REQUIRE_EQ(ByteSize::parse("4KiB").bytes, std::uint64_t{4} * 1024);
        REQUIRE_EQ(ByteSize::parse("100MiB").bytes, std::uint64_t{100} * 1024 * 1024);
        REQUIRE_EQ(ByteSize::parse("1GiB").bytes, std::uint64_t{1} * 1024 * 1024 * 1024);
        REQUIRE_EQ(ByteSize::parse("1TiB").bytes, std::uint64_t{1} * 1024 * 1024 * 1024 * 1024);
    }

    TEST_CASE("bare numbers are bytes") {
        REQUIRE_EQ(ByteSize::parse("512").bytes, 512);
        REQUIRE_EQ(ByteSize::parse("0").bytes, 0);
    }

    TEST_CASE("spaces are trimmed") {
        REQUIRE_EQ(ByteSize::parse(" 1KiB ").bytes, 1024);
    }

    TEST_CASE("invalid literals are errors") {
        REQUIRE_THROWS_AS((void)ByteSize::parse(""), Exception);
        REQUIRE_THROWS_AS((void)ByteSize::parse("100MB"), Exception);
        REQUIRE_THROWS_AS((void)ByteSize::parse("100kib"), Exception);
        REQUIRE_THROWS_AS((void)ByteSize::parse("100XB"), Exception);
        REQUIRE_THROWS_AS((void)ByteSize::parse("1.5MiB"), Exception);
        REQUIRE_THROWS_AS((void)ByteSize::parse("MiB"), Exception);
        REQUIRE_THROWS_AS((void)ByteSize::parse("18446744073709551616MiB"), Exception);
    }

    TEST_CASE("json mapping: string and number") {
        auto value = ByteSize{};

        JsonMapping<ByteSize>::read(nlohmann::json::parse(R"("100MiB")"), value);
        REQUIRE_EQ(value.bytes, std::uint64_t{100} * 1024 * 1024);

        JsonMapping<ByteSize>::read(nlohmann::json::parse("4096"), value);
        REQUIRE_EQ(value.bytes, 4096);

        nlohmann::json json;
        JsonMapping<ByteSize>::write(json, ByteSize{2048});
        REQUIRE_EQ(json.is_number_unsigned(), true);
        REQUIRE_EQ(json.get<std::uint64_t>(), 2048);

        REQUIRE_THROWS_AS((void)JsonMapping<ByteSize>::read(nlohmann::json::parse(R"("1MB")"), value), Exception);
    }
}

} // namespace turbofw::config
