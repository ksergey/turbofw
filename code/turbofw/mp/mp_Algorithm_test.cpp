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

    TEST_CASE("Unique; empty list") {
        CHECK(std::is_same_v<Unique<List<>>, List<>>);
    }

    // a never-matching predicate
    template <typename>
    using PredNothing = std::bool_constant<false>;

    TEST_CASE("CountIf") {
        using Objects = List<Object<5>, Object<3>, Object<7>, Object<32>>;
        CHECK_EQ(CountIf<Objects, PredA>::value, 1);
        CHECK_EQ(CountIf<Objects, PredB>::value, 0); // nothing matches PredB (== 64), see FindIf
        CHECK_EQ(CountIf<Objects, PredNothing>::value, 0);
    }

    // a trait with a non-bool `value`: CountIf normalizes through ToBool, a nonzero value counts
    struct NonBoolTrait {
        static constexpr int value = 3;
    };

    // a predicate whose result is that non-bool trait
    template <typename>
    using PredNonBoolTrait = NonBoolTrait;

    TEST_CASE("CountIf normalizes the predicate result to bool") {
        // the predicate is instantiated for every element, so it must be valid on all of them
        using L = List<void, double>;
        CHECK_EQ(CountIf<L, PredNothing>::value, 0);      // int 0 is false
        CHECK_EQ(CountIf<L, PredNonBoolTrait>::value, 2); // int 3 counts: ToBool normalizes
    }

    TEST_CASE("CountIf_q") {
        using Objects = List<Object<5>, Object<3>, Object<7>, Object<32>>;
        CHECK_EQ(CountIf_q<Objects, Pred<5>>::value, 1);
        CHECK_EQ(CountIf_q<Objects, Pred<32>>::value, 1);
        CHECK_EQ(CountIf_q<Objects, Pred<44>>::value, 0);
    }

    // a mapping metafunction (Transform takes F first)
    template <typename T>
    using MapPointer = T*;

    template <std::size_t N>
    struct ObjectQ {
        static constexpr std::size_t value = N;
    };

    // a quoted mapping metafunction (Transform_q takes Q first)
    struct QuotedMap {
        template <typename T>
        using fn = ObjectQ<sizeof(T)>;
    };

    TEST_CASE("Transform; List") {
        using L = List<int, char, double>;
        CHECK((std::is_same_v<Transform<MapPointer, L>, List<int*, char*, double*>>));
    }

    TEST_CASE("Transform; empty list short-circuits, form preserved") {
        CHECK(std::is_same_v<Transform<MapPointer, List<>>, List<>>);
        CHECK(std::is_same_v<Transform<MapPointer, std::tuple<>>, std::tuple<>>);
    }

    TEST_CASE("Transform; std::tuple keeps its container form") {
        using L = std::tuple<int, char>;
        CHECK((std::is_same_v<Transform<MapPointer, L>, std::tuple<int*, char*>>));
    }

    TEST_CASE("Transform_q") {
        using L = List<char, std::int64_t>;
        CHECK((std::is_same_v<Transform_q<QuotedMap, L>, List<ObjectQ<1>, ObjectQ<8>>>));
    }

    TEST_CASE("Defer; public mp_defer-style application") {
        CHECK(std::is_same_v<Defer<MapPointer, std::int64_t>, std::int64_t*>);
        // a mapping that takes the elements as a pack
        CHECK(std::is_same_v<Defer<List, void, double>, List<void, double>>);
    }

    TEST_CASE("Count and Find are FindIf/CountIf over the sameness predicate") {
        using Types = List<int, double, char, float>;
        // turbofw::mp::detail::SameAs is the shared engine: value-shaped Count/Find must be exactly the
        // engine re-wrapped, so either type comparison holds
        CHECK((std::is_same_v<CountIf<Types, detail::SameAs<char>::template fn>, Count<Types, char>>));
        CHECK((std::is_same_v<FindIf<Types, detail::SameAs<char>::template fn>, Find<Types, char>>));
    }
}

} // namespace turbofw::mp::testing
