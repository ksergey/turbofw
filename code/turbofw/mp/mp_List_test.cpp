// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <tuple>
#include <type_traits>

#include <doctest/doctest.h>

#include "List.h"

namespace turbofw::mp::testing {

TEST_SUITE("List") {

    TEST_CASE("IsList") {
        CHECK(IsList<List<int, char>>::value);
        CHECK(IsList<std::tuple<int, char>>::value);
        CHECK(IsList<List<>>::value);
        // only class template instantiations are lists; plain types are not
        CHECK(!IsList<int>::value);
        CHECK(!IsList<char*>::value);
    }

    TEST_CASE("Size") {
        CHECK_EQ((Size<List<int, char, void>>::value), 3);
        CHECK_EQ((Size<List<>>::value), 0);
        CHECK_EQ((Size<std::tuple<int, char>>::value), 2);
    }

    TEST_CASE("Front; First is another name for Front") {
        CHECK(std::is_same_v<Front<List<int, char>>, int>);
        CHECK(std::is_same_v<First<List<int, char>>, int>);
        CHECK(std::is_same_v<Front<List<int>>, int>);
        // Front on an empty list does not compile: the primary template stays undefined
    }

    TEST_CASE("PopFront; Rest is another name for PopFront") {
        CHECK(std::is_same_v<PopFront<List<int, char, void>>, List<char, void>>);
        CHECK(std::is_same_v<First<List<int, char, void>>, int>);
        // a one-element list empties out
        CHECK(std::is_same_v<Rest<List<int>>, List<>>);
        // PopFront on an empty list does not compile: the primary template stays undefined
    }

    TEST_CASE("PushFront") {
        CHECK(std::is_same_v<PushFront<List<char>, int>, List<int, char>>);
        // several elements at once, they keep the given order
        CHECK(std::is_same_v<PushFront<List<char>, int, double>, List<int, double, char>>);
        // pushing into an empty list
        CHECK(std::is_same_v<PushFront<List<>, int>, List<int>>);
    }

    TEST_CASE("PushBack") {
        CHECK(std::is_same_v<PushBack<List<char>, int>, List<char, int>>);
        CHECK(std::is_same_v<PushBack<List<char>, int, double>, List<char, int, double>>);
        CHECK(std::is_same_v<PushBack<List<>, int>, List<int>>);
    }

    TEST_CASE("Rename") {
        CHECK(std::is_same_v<Rename<List<int, char>, std::tuple>, std::tuple<int, char>>);
        CHECK(std::is_same_v<Rename<std::tuple<int, char>, List>, List<int, char>>);
    }

    TEST_CASE("Clear drains the list but keeps its container form") {
        CHECK(std::is_same_v<Clear<List<int, char, void>>, List<>>);
        CHECK(std::is_same_v<Clear<std::tuple<int, char>>, std::tuple<>>);
    }
}

} // namespace turbofw::mp::testing
