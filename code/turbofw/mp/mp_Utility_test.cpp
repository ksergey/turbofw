// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <type_traits>
#include <utility>

#include <doctest/doctest.h>

#include "Integral.h"
#include "List.h"
#include "Utility.h"

namespace turbofw::mp::testing {

TEST_SUITE("Integral") {

    TEST_CASE("SizeT is an integral constant of std::size_t") {
        using Two = SizeT<2>;
        CHECK_EQ(Two::value, 2);
        CHECK(std::is_same_v<typename Two::value_type, std::size_t>);
    }

    // non-bool `value` members: ToBool normalizes through conversion to bool
    struct Three {
        static constexpr int value = 3;
    };
    struct Zero {
        static constexpr int value = 0;
    };

    TEST_CASE("ToBool normalizes a trait to a bool constant") {
        CHECK(std::is_same_v<ToBool<std::true_type>, std::bool_constant<true>>);
        CHECK(std::is_same_v<ToBool<std::false_type>, std::bool_constant<false>>);
        CHECK(std::is_same_v<ToBool<Three>, std::bool_constant<true>>);
        CHECK(std::is_same_v<ToBool<Zero>, std::bool_constant<false>>);
    }
}

TEST_SUITE("Utility") {

    // mixins to inherit from
    struct Trace {
        static constexpr int tag = 1;
    };
    struct Stamp {
        static constexpr int tag = 2;
    };

    TEST_CASE("Inherit inherits from every element of the list") {
        using Composed = Inherit<List<Trace, Stamp>>;
        CHECK(std::is_base_of_v<Trace, Composed>);
        CHECK(std::is_base_of_v<Stamp, Composed>);

        auto composed = Composed{};
        // members of every base are reachable in mixin style: qualify by base, `tag` is ambiguous otherwise
        CHECK_EQ(composed.Trace::tag, 1);
        CHECK_EQ(composed.Stamp::tag, 2);
    }

    TEST_CASE("Inherit of an empty list inherits from nothing") {
        using Composed = Inherit<List<>>;
        // a struct with no bases is still constructible
        CHECK(!std::is_same_v<Composed, void>);
        CHECK((std::is_constructible_v<Composed>));
    }
}

} // namespace turbofw::mp::testing
