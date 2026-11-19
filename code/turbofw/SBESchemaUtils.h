// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <concepts>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include "CtString.h"
#include "mp/Algorithm.h"
#include "mp/List.h"

namespace turbofw {

/// Checks M is a generated SBE message type decodable from a raw buffer: it carries the (schemaId, templateId)
/// dispatch key and the per-schema message header type alias. Comparison happens against the header fields read
/// from the buffer, not against the message's fixed constants.
template <typename M>
concept SBEMessage = requires(std::span<std::byte> buffer) {
    typename M::Header;
    typename M::SizeHint;
    { M::sbeSchemaId() } -> std::convertible_to<std::uint16_t>;
    { M::sbeTemplateId() } -> std::convertible_to<std::uint16_t>;
    M::wrapAndApplyHeader(buffer);
};

/// Checks the message has variable-size parts (repeating groups or var-length data fields): its SizeHint is not
/// empty, so the sizes of those parts must be passed to the send operation
template <typename M>
concept SBEMessageWithVars = requires { typename M::SizeHint; } && (std::tuple_size_v<typename M::SizeHint> != 0);

/// Checks F is a generated field of SBE message
template <typename F>
concept SBEField = requires {
    { std::remove_cvref_t<F>::sbeRefName() } -> std::convertible_to<std::string_view>;
};

/// Checks F is a generated field and this field is repeating group
template <typename F>
concept SBEFieldIsGroup = SBEField<F> && requires {
    typename std::remove_cvref_t<F>::Fields;
    { std::remove_cvref_t<F>::sbeGroupName() } -> std::convertible_to<std::string_view>;
};

/// Checks F is a generated field and this field is variable length data
template <typename F>
concept SBEFieldIsData = SBEField<F> && requires {
    typename std::remove_cvref_t<F>::Fields;
    { std::remove_cvref_t<F>::sbeDataName() } -> std::convertible_to<std::string_view>;
};

/// Checks F is a generated field and this field is composite type
template <typename F>
concept SBEFieldIsComposite = SBEField<F> && requires {
    typename std::remove_cvref_t<F>::Fields;
    { std::remove_cvref_t<F>::sbeCompositeName() } -> std::convertible_to<std::string_view>;
};

/// Checks F is composite with only 2 fields - mantissa and exponent
template <typename F>
concept SBEFieldIsCompositeDecimal =
    SBEFieldIsComposite<F> && mp::Size<typename std::remove_cvref_t<F>::Fields>::value == 2 &&
    mp::At_c<typename std::remove_cvref_t<F>::Fields, 0>::sbeRefName() == std::string_view{"mantissa"} &&
    mp::At_c<typename std::remove_cvref_t<F>::Fields, 1>::sbeRefName() == std::string_view{"exponent"};

/// Checks V is value of SBE type set
template <typename V>
concept SBETypeIsSet = requires { V::sbeBitNames; };

/// Checks V is value of SBE type enum
template <typename V>
concept SBETypeIsEnum = std::is_enum_v<V>;

/// Checks V is value of SBE type integral or floating point number
template <typename V>
concept SBETypeIsNumber = std::is_integral_v<V> || std::is_floating_point_v<V>;

/// Iterate over fields of SBE message, group or composite type
template <typename T, typename F>
constexpr void sbeForEachField(T entry, F&& f) {
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        // (f(std::integral_constant<std::size_t, I>{}), ...)
        (f(entry.template get<I>()), ...);
    }(std::make_index_sequence<mp::Size<typename T::Fields>::value>{});
}

namespace detail {

template <typename M>
using MessageHeaderOf = typename M::Header;

/// Uniform header across the list L: every message of L carries one and the same generated MessageHeader --
/// as all messages of one schema do. The gate of `SBEMessageHeader` and both `decodeSBE` overloads.
template <typename L>
concept UniformHeader = mp::IsList<L>::value && (mp::Size<mp::Unique<mp::Transform<MessageHeaderOf, L>>>::value == 1);

} // namespace detail

/// The generated MessageHeader shared by every message of the list L (all types of one schema carry one; md and
/// service mix legally because both include the very same common_types header). A list element without a `Header`
/// alias is a hard compile error, not a concept failure -- SBE lists are written by hand.
template <typename L>
    requires detail::UniformHeader<L>
using SBEMessageHeader = typename mp::Front<L>::Header;

namespace detail {

/// `Fields` of the generated message M: the named per-field ref types in schema order
template <typename M>
using MessageFields = typename M::Fields;

/// The field ref named N: matches `sbeRefName()`. Well-formed for every Fields element; group and data refs are
/// named too, they simply carry no ValueType.
template <CtString N>
struct FieldNamed {
    template <typename Ref>
    struct fn : std::bool_constant<std::string_view{Ref::sbeRefName()} == static_cast<std::string_view>(N)> {};
};

} // namespace detail

/// The field ref of the generated SBE message M named N: `SBEFieldOf<turbofw::schema::service::Heartbeat, "serviceId">`
/// is `turbofw::schema::service::Heartbeat::ServiceIdRef`. The search runs over `M::Fields` in schema order; the name
/// argument is a `turbofw::CtString` string literal. An unknown name is a hard compile error -- use `SBEHasField` for
/// the soft check.
template <typename M, CtString N>
using SBEFieldOf = typename mp::At<detail::MessageFields<M>,
    typename mp::FindIf<detail::MessageFields<M>, detail::FieldNamed<N>::template fn>::type>::type;

/// The value type of the field N of the generated SBE message M: a primitive encoded type for plain fields, the
/// enum/set class for enum and set fields, the composite class for composite fields. Only value-carrying fields:
/// for group and data fields `SBEHasField` is false and the alias is a compile error.
template <typename M, CtString N>
using SBEFieldValueOf = typename SBEFieldOf<M, N>::ValueType;

/// Checks M is a generated SBE message with a value-carrying field named N: group and data fields count as
/// absent, so does M without a `Fields` list. The soft form of `SBEFieldOf` -- fold it in via `if constexpr` /
/// `requires` instead of instantiating the alias directly on an unverified name.
template <typename M, CtString N>
concept SBEHasField = SBEMessage<M> && requires { typename detail::MessageFields<M>; } &&
                      (mp::FindIf<detail::MessageFields<M>, detail::FieldNamed<N>::template fn>::value <
                          mp::Size<detail::MessageFields<M>>::value) &&
                      requires { typename SBEFieldOf<M, N>::ValueType; };

/// The schema null value of the field N of the generated SBE message M, a constexpr variable: the exact constant
/// the ref compares against in `present()` -- a raw sentinel for scalars, the enum `SBE_NULL` constant for enums,
/// a default constructed composite for optional composites. Formed only for presence="optional" fields; use
/// `SBEHasFieldNull` for the soft check.
template <typename M, CtString N>
inline constexpr auto SBEFieldNullValueOf = SBEFieldOf<M, N>::NullValue;

/// Checks M is a generated SBE message with a nullable (presence="optional") field named N; group, data,
/// required and constant fields count as absent. The soft form of `SBEFieldNullValueOf`.
template <typename M, CtString N>
concept SBEHasFieldNull = SBEHasField<M, N> && requires { SBEFieldOf<M, N>::NullValue; };

} // namespace turbofw
