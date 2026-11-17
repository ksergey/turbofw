// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include <cmath>
#include <concepts>
#include <format>
#include <string>

#include "CtString.h"
#include "mp/Algorithm.h"
#include "mp/List.h"

namespace turbofw {

/// List of SBE messages
template <typename... Ms>
using SBEMessages = mp::List<Ms...>;

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

namespace detail {

// ---- generic JSON serialization of decoded SBE messages (debug/logging path: allocates) ----

/// Detects the generated TypeList element count
template <typename L>
consteval auto sbeTypeListCount() -> std::size_t {
    return []<template <typename...> typename List, typename... Ts>(List<Ts...>*) {
        return sizeof...(Ts);
    }(static_cast<L*>(nullptr));
}

/// TypeList element at index I
template <std::size_t I, typename L>
struct SBETypeListAt;
template <template <typename...> typename List, typename T, typename... Ts>
struct SBETypeListAt<0, List<T, Ts...>> {
    using type = T;
};
template <std::size_t I, template <typename...> typename List, typename T, typename... Ts>
struct SBETypeListAt<I, List<T, Ts...>> : SBETypeListAt<I - 1, List<Ts...>> {};

/// Field ref kinds of the generated codecs, by introspection
template <typename R>
concept SBEFieldWithValueType = requires { typename R::ValueType; };

template <typename R>
concept SBEFieldIsGroup = !SBEFieldWithValueType<R> && requires(R r) {
    typename R::Fields;
    { r.count() } -> std::convertible_to<std::uint16_t>;
    { r.hasNext() } -> std::convertible_to<bool>;
    r.next();
};

template <typename R>
concept SBEFieldIsData = !SBEFieldWithValueType<R> && requires(R r) {
    { r.value() } -> std::convertible_to<std::string_view>;
};

template <typename R>
concept SBEFieldIsComposite = SBEFieldWithValueType<R> && requires { typename R::Fields; };

template <typename V>
concept SBEHasBitNames = requires { V::sbeBitNames; };

/// A decimal-like composite: exactly two fields, named "mantissa" and "exponent" (SBE decimal convention,
/// e.g. Price9/Price9NULL). The double value is mantissa * 10^exponent.
template <typename R>
consteval auto sbeFieldIsDecimal() -> bool {
    if constexpr (!SBEFieldIsComposite<R>) {
        return false;
    } else if constexpr (sbeTypeListCount<typename R::Fields>() != 2) {
        return false;
    } else {
        using MantissaRef = typename SBETypeListAt<0, typename R::Fields>::type;
        using ExponentRef = typename SBETypeListAt<1, typename R::Fields>::type;
        return std::string_view{MantissaRef::sbeRefName()} == "mantissa" &&
               std::string_view{ExponentRef::sbeRefName()} == "exponent";
    }
}

/// JSON output state: the output string, serialization options and the "no comma yet" flag of the current object
struct SBEJsonWriter {
    std::string& out;
    bool decimalsAsDouble;
    bool first{true};

    void key(std::string_view name) {
        if (!first) {
            out.push_back(',');
        }
        first = false;
        out.push_back('"');
        out.append(name);
        out.append("\":");
    }

    void string(std::string_view value) {
        out.push_back('"');
        for (char const c : value) {
            switch (c) {
            case '"': out.append("\\\""); break;
            case '\\': out.append("\\\\"); break;
            case '\n': out.append("\\n"); break;
            case '\r': out.append("\\r"); break;
            case '\t': out.append("\\t"); break;
            default:
                if (static_cast<unsigned char>(c) < 0x20u) {
                    static constexpr char kHex[] = "0123456789abcdef";
                    unsigned char const uc = static_cast<unsigned char>(c);
                    out.append("\\u00");
                    out.push_back(kHex[(uc >> 4) & 0xFu]);
                    out.push_back(kHex[uc & 0xFu]);
                } else {
                    out.push_back(c);
                }
            }
        }
        out.push_back('"');
    }

    void null() {
        out.append("null");
    }

    template <typename V>
        requires std::is_integral_v<V> || std::is_floating_point_v<V>
    void number(V value) {
        if constexpr (std::is_floating_point_v<V>) {
            out += std::format("{}", value);
        } else if constexpr (std::is_signed_v<V>) {
            out += std::format("{}", static_cast<std::int64_t>(value));
        } else {
            out += std::format("{}", static_cast<std::uint64_t>(value));
        }
    }
};

/// Writes one "name": value pair
template <typename R>
void serializeSBEField(SBEJsonWriter& writer, R r);

/// Writes the fields of obj (a message, a group entry or a composite ref) as a JSON object body
template <typename O>
    requires requires { typename O::Fields; }
void serializeSBEFields(SBEJsonWriter& writer, O& obj) {
    bool const outerFirst = writer.first;
    writer.first = true;
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        (serializeSBEField(writer, obj.template get<Is>()), ...);
    }(std::make_index_sequence<sbeTypeListCount<typename O::Fields>()>{});
    writer.out.push_back('}');
    writer.first = outerFirst;
}

/// Writes the value of one field ref (key is written by the caller via SBEJsonWriter::key)
template <typename R>
void serializeSBEFieldValue(SBEJsonWriter& writer, R r) {
    // value-carrying refs always carry present(); group and data refs do not
    if constexpr (requires { r.present(); }) {
        if (!r.present()) {
            writer.null();
            return;
        }
    }
    if constexpr (SBEFieldIsGroup<R>) {
        writer.out.push_back('[');
        bool entryFirst = true;
        while (r.hasNext()) {
            r.next();
            if (!entryFirst) {
                writer.out.push_back(',');
            }
            entryFirst = false;
            writer.out.push_back('{');
            serializeSBEFields(writer, r);
        }
        writer.out.push_back(']');
    } else if constexpr (SBEFieldIsData<R>) {
        writer.string(r.value());
    } else if constexpr (sbeFieldIsDecimal<R>()) {
        auto const mantissa = r.template get<0>();
        auto const exponent = r.template get<1>();
        static_assert(std::string_view{decltype(mantissa)::sbeRefName()} == "mantissa" &&
                          std::string_view{decltype(exponent)::sbeRefName()} == "exponent",
            "unexpected decimal-like composite shape");
        if (!mantissa.present() || !exponent.present()) {
            writer.null();
            return;
        }
        if (writer.decimalsAsDouble) {
            // mantissa * 10^exponent; for the common negative exponents the division by an exact integer
            // power of ten is correctly rounded, so e.g. 101250000000 / 1e9 is exactly 101.25 (a
            // multiplication by the double approximation of 1e-9 would not be)
            double const exponentValue = static_cast<double>(exponent.value());
            writer.number(exponentValue <= 0.0 ? static_cast<double>(mantissa.value()) / std::pow(10.0, -exponentValue)
                                               : static_cast<double>(mantissa.value()) * std::pow(10.0, exponentValue));
            return;
        }
        writer.out.push_back('{');
        serializeSBEFields(writer, r);
    } else if constexpr (SBEFieldWithValueType<R> && SBEHasBitNames<typename R::ValueType>) {
        using ValueType = typename R::ValueType;
        auto const bits = static_cast<std::uint64_t>(r.value());
        writer.out.push_back('[');
        bool entryFirst = true;
        for (auto const& [mask, name] : ValueType::sbeBitNames) {
            if ((mask & bits) == mask) {
                if (!entryFirst) {
                    writer.out.push_back(',');
                }
                entryFirst = false;
                writer.string(name);
            }
        }
        writer.out.push_back(']');
    } else if constexpr (SBEFieldIsComposite<R>) {
        writer.out.push_back('{');
        serializeSBEFields(writer, r);
    } else if constexpr (SBEFieldWithValueType<R> && std::is_enum_v<typename R::ValueType>) {
        using ValueType = typename R::ValueType;
        auto const value = r.value();
        if constexpr (requires { sbeEnumNames(value); }) {
            for (auto const& [enumValue, name] : sbeEnumNames(value)) {
                if (enumValue == value) {
                    writer.string(name);
                    return;
                }
            }
        }
        writer.number(static_cast<std::underlying_type_t<ValueType>>(value));
    } else if constexpr (SBEFieldWithValueType<R>) {
        static_assert(std::is_arithmetic_v<typename R::ValueType>, "unsupported SBE field value type");
        writer.number(r.value());
    } else {
        static_assert(sizeof(R) == 0, "unsupported SBE field ref kind");
    }
}

/// Writes one "name": value pair
template <typename R>
void serializeSBEField(SBEJsonWriter& writer, R r) {
    writer.key(R::sbeRefName());
    serializeSBEFieldValue(writer, r);
}

} // namespace detail

/// Options of sbeToJson
struct SbeJsonOptions {
    /// Serialize decimal-like composites (mantissa + exponent, e.g. Price9) as a double number instead of a
    /// nested object {"mantissa": ..., "exponent": ...}
    bool decimalsAsDouble = true;
};

/// Serializes a decoded SBE message to a JSON object string: {"message":"Name", ...fields in schema order...}.
/// Works generically over the generated codec introspection (Fields, get<I>, sbeRefName, present, value): plain
/// and optional (absent -> null) scalars become numbers, enums become their value names (unknown values fall
/// back to the underlying number), sets become arrays of the set bit names, var-length data fields become
/// strings, composites become nested objects and repeating groups become arrays of entry objects.
/// Decimal-like composites (mantissa + exponent) are emitted as doubles per SbeJsonOptions::decimalsAsDouble.
/// A debug/logging convenience: it allocates; keep it off hot paths.
template <SBEMessage M>
[[nodiscard]] auto sbeToJson(M msg, SbeJsonOptions options = {}) -> std::string {
    std::string out;
    out.reserve(256);
    out.push_back('{');
    auto writer = detail::SBEJsonWriter{out, options.decimalsAsDouble, true};
    writer.key("message");
    writer.string(M::sbeMessageName());
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        (detail::serializeSBEField(writer, msg.template get<Is>()), ...);
    }(std::make_index_sequence<detail::sbeTypeListCount<typename M::Fields>()>{});
    out.push_back('}');
    return out;
}

} // namespace turbofw
