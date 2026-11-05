// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <tuple>

#include <doctest/doctest.h>

#include "Algorithm.h"
#include "List.h"

namespace turbofw::mp::testing {

TEST_SUITE("Algorithm") {
    TEST_CASE("Find; List") {
        using Types = List<int, double, char, float>;
        CHECK_EQ(Find<Types, int>::value, 0);
        CHECK_EQ(Find<Types, double>::value, 1);
        CHECK_EQ(Find<Types, char>::value, 2);
        CHECK_EQ(Find<Types, float>::value, 3);
        CHECK_EQ(Find<Types, std::size_t>::value, 4);
        CHECK_EQ(Find<Types, char*>::value, 4);
    }

    TEST_CASE("Find; std::tuple") {
        using Types = std::tuple<int, double, char, float>;
        CHECK_EQ(Find<Types, int>::value, 0);
        CHECK_EQ(Find<Types, double>::value, 1);
        CHECK_EQ(Find<Types, char>::value, 2);
        CHECK_EQ(Find<Types, float>::value, 3);
        CHECK_EQ(Find<Types, std::size_t>::value, 4);
        CHECK_EQ(Find<Types, char*>::value, 4);
    }

    template <std::size_t N>
    struct Object {
        static constexpr auto value = N;
    };

    template <typename T>
    using PredA = std::bool_constant<T::value == 7>;
    template <typename T>
    using PredB = std::bool_constant<T::value == 64>;

    TEST_CASE("FindIf") {
        using Objects = List<Object<5>, Object<3>, Object<7>, Object<32>>;
        CHECK_EQ(FindIf<Objects, PredA>::value, 2);
        CHECK_EQ(FindIf<Objects, PredB>::value, 4);
    }

    template <std::size_t N>
    struct Pred {
        template <typename T>
        using fn = std::bool_constant<T::value == N>;
    };

    TEST_CASE("FindIf_q") {
        using Objects = List<Object<5>, Object<3>, Object<7>, Object<32>>;
        CHECK_EQ(FindIf_q<Objects, Pred<5>>::value, 0);
        CHECK_EQ(FindIf_q<Objects, Pred<3>>::value, 1);
        CHECK_EQ(FindIf_q<Objects, Pred<7>>::value, 2);
        CHECK_EQ(FindIf_q<Objects, Pred<32>>::value, 3);
        CHECK_EQ(FindIf_q<Objects, Pred<44>>::value, 4);
        CHECK_EQ(FindIf_q<Objects, Pred<4>>::value, 4);
    }
}

} // namespace turbofw::mp::testing
