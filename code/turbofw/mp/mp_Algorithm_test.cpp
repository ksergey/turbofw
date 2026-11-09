// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <tuple>

#include <doctest/doctest.h>

#include "Algorithm.h"
#include "List.h"

namespace turbofw::mp::testing {

TEST_SUITE("Algorithm") {

    TEST_CASE("Count") {
        using L = List<int, char, double, char, void>;
        CHECK_EQ(Count<L, int>::value, 1);
        CHECK_EQ(Count<L, char>::value, 2);
        CHECK_EQ(Count<L, double>::value, 1);
        CHECK_EQ(Count<L, void>::value, 1);
        CHECK_EQ(Count<L, float>::value, 0);
    }

    TEST_CASE("Contains") {
        using L = List<int, char, double, char, void>;
        CHECK(Contains<L, int>::value);
        CHECK(Contains<L, char>::value);
        CHECK(Contains<L, double>::value);
        CHECK(Contains<L, void>::value);
        CHECK(!Contains<L, float>::value);
        CHECK(!Contains<L, char*>::value);
    }

    TEST_CASE("At; List") {
        using Types = List<int, double, char, float>;
        CHECK(std::is_same_v<At<Types, SizeT<0>>::type, int>);
        CHECK(std::is_same_v<At<Types, SizeT<1>>::type, double>);
        CHECK(std::is_same_v<At<Types, SizeT<2>>::type, char>);
        CHECK(std::is_same_v<At<Types, SizeT<3>>::type, float>);
    }

    TEST_CASE("At; std::tuple") {
        using Types = std::tuple<int, double, char, float>;
        CHECK(std::is_same_v<At<Types, SizeT<0>>::type, int>);
        CHECK(std::is_same_v<At<Types, SizeT<3>>::type, float>);
    }

    TEST_CASE("At_c: sugar over At") {
        using Types = List<int, double, char, float>;
        CHECK(std::is_same_v<At_c<Types, 0>, int>);
        CHECK(std::is_same_v<At_c<Types, 1>, double>);
        CHECK(std::is_same_v<At_c<Types, 2>, char>);
        CHECK(std::is_same_v<At_c<Types, 3>, float>);
        // an out-of-range index does not compile (At_c<Types, 4>): std::tuple_element hard-fails with
        // "tuple_element index out of range", it is not a SFINAE-friendly failure
    }

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

    TEST_CASE("Unique") {
        using L = List<int, char, int, double, void, char>;
        CHECK(std::is_same_v<Unique<L>, List<int, double, void, char>>);
    }
}

} // namespace turbofw::mp::testing
