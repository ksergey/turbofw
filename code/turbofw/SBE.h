// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

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

namespace detail {

template <typename M>
using MessageHeaderOf = typename M::Header;

/// Uniform header across the list L: every message of L carries one and the same generated MessageHeader --
/// as all messages of one schema do. The gate of `SBEMessageHeader` and both `tryDecodeSBE` overloads.
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

/// The field ref of the generated SBE message M named N: `SBEFieldOf<service::Heartbeat, "serviceId">` is
/// `service::Heartbeat::ServiceIdRef`. The search runs over `M::Fields` in schema order; the name argument is a
/// `turbofw::CtString` string literal. An unknown name is a hard compile error -- use `SBEHasField` for the
/// soft check.
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

/// Demultiplexes an SBE message buffer: tries every message type of the list L against the header, the first
/// one whose (schemaId, templateId) pair matches is decoded and passed to `fn`; returns whether any did.
/// Comparisons happen against the acting values read from the buffer, not against `M::sbeSchemaId()` alone: two
/// list entries with the same pair are both reachable, the first wins.
///
/// The buffer must carry a generated MessageHeader and `fn` must accept every message type of L as a by-copy
/// argument (SBE message objects are lightweight non-owning views; decode never mutates the buffer -- the
/// const_cast inside lets a `std::span<std::byte const>` through, since the generated wrap needs a mutable
/// pointer). This overload assumes the header is already decoded from this very buffer (`buffer.size() >=
/// header.sbeEncodedLength()`; the `SBEReceiveMixin::receive` fast path). A body shorter than the acting block
/// length makes the generated decode throw: per the hot-path exception rule, treat it as fatal.
template <typename L, typename Fn>
    requires detail::UniformHeader<L>
constexpr auto tryDecodeSBE(std::span<std::byte const> buffer, SBEMessageHeader<L> const& header, Fn&& fn) -> bool {
    return [&]<template <typename...> typename List, typename... Ts>(List<Ts...>)
        requires(SBEMessage<Ts> && ...) && (std::invocable<Fn&, Ts> && ...)
    {
        // The generated header/wrap needs a mutable pointer, but decode never writes to the buffer.
        auto const bufferMutable = std::span{const_cast<std::byte*>(buffer.data()), buffer.size()};

        return ([&]() -> bool {
            if (header.template get<"schemaId">().value() != Ts::sbeSchemaId() ||
                header.template get<"templateId">().value() != Ts::sbeTemplateId()) {
                return false;
            }

            auto const msg = Ts{bufferMutable, header.sbeEncodedLength(), header.template get<"blockLength">().value(),
                header.template get<"version">().value()};
            std::invoke(std::forward<Fn>(fn), msg);

            return true;
        }() || ...);
    }(L{});
}

/// The convenience form: decodes the MessageHeader itself -- once, not per candidate -- then dispatches as the
/// header-taking overload above. Buffers too short to carry the MessageHeader yield false without a callback
/// invocation (guard per call, a short-buffer body is not fatal by itself: nothing beyond the header is read).
template <typename L, typename Fn>
    requires detail::UniformHeader<L>
constexpr auto tryDecodeSBE(std::span<std::byte const> buffer, Fn&& fn) -> bool {
    // Messages in list L have the same MessageHeader, so decode header once
    using Header = SBEMessageHeader<L>;

    if (buffer.size() < Header::sbeEncodedLength()) [[unlikely]] {
        return false;
    }

    auto const bufferMutable = std::span{const_cast<std::byte*>(buffer.data()), buffer.size()};
    auto const header = Header{bufferMutable.data()};
    return tryDecodeSBE<L, Fn>(buffer, header, std::forward<Fn>(fn));
}

} // namespace turbofw
