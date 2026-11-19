// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <tuple>
#include <type_traits>

#include <doctest/doctest.h>

#include "TypeList.h"

namespace turbofw::mp::testing {

TEST_SUITE("TypeList") {

    TEST_CASE("IsTypeList") {
        CHECK(IsTypeList<TypeList<int, char>>::value);
        CHECK(IsTypeList<std::tuple<int, char>>::value);
        CHECK(IsTypeList<TypeList<>>::value);
        // only class template instantiations are lists; plain types are not
        CHECK(!IsTypeList<int>::value);
        CHECK(!IsTypeList<char*>::value);
    }

    TEST_CASE("Size") {
        CHECK_EQ((Size<TypeList<int, char, void>>::value), 3);
        CHECK_EQ((Size<TypeList<>>::value), 0);
        CHECK_EQ((Size<std::tuple<int, char>>::value), 2);
    }

    TEST_CASE("Front; First is another name for Front") {
        CHECK(std::is_same_v<Front<TypeList<int, char>>, int>);
        CHECK(std::is_same_v<First<TypeList<int, char>>, int>);
        CHECK(std::is_same_v<Front<TypeList<int>>, int>);
        // Front on an empty list does not compile: the primary template stays undefined
    }

    TEST_CASE("PopFront; Rest is another name for PopFront") {
        CHECK(std::is_same_v<PopFront<TypeList<int, char, void>>, TypeList<char, void>>);
        CHECK(std::is_same_v<First<TypeList<int, char, void>>, int>);
        // a one-element list empties out
        CHECK(std::is_same_v<Rest<TypeList<int>>, TypeList<>>);
        // PopFront on an empty list does not compile: the primary template stays undefined
    }

    TEST_CASE("PushFront") {
        CHECK(std::is_same_v<PushFront<TypeList<char>, int>, TypeList<int, char>>);
        // several elements at once, they keep the given order
        CHECK(std::is_same_v<PushFront<TypeList<char>, int, double>, TypeList<int, double, char>>);
        // pushing into an empty list
        CHECK(std::is_same_v<PushFront<TypeList<>, int>, TypeList<int>>);
    }

    TEST_CASE("PushBack") {
        CHECK(std::is_same_v<PushBack<TypeList<char>, int>, TypeList<char, int>>);
        CHECK(std::is_same_v<PushBack<TypeList<char>, int, double>, TypeList<char, int, double>>);
        CHECK(std::is_same_v<PushBack<TypeList<>, int>, TypeList<int>>);
    }

    TEST_CASE("Rename") {
        CHECK(std::is_same_v<Rename<TypeList<int, char>, std::tuple>, std::tuple<int, char>>);
        CHECK(std::is_same_v<Rename<std::tuple<int, char>, TypeList>, TypeList<int, char>>);
    }

    TEST_CASE("Clear drains the list but keeps its container form") {
        CHECK(std::is_same_v<Clear<TypeList<int, char, void>>, TypeList<>>);
        CHECK(std::is_same_v<Clear<std::tuple<int, char>>, std::tuple<>>);
    }
}

} // namespace turbofw::mp::testing
