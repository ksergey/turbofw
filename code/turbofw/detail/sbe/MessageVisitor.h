// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cmath>
#include <concepts>
#include <string_view>
#include <type_traits>

#include <turbofw/SBESchemaUtils.h>

namespace turbofw::detail::sbe {

/// How decimal-like composites (a `mantissa` + an `exponent`, e.g. Price9) are written
enum class DecimalFormat {
    AsDouble, ///< a JSON number: mantissa scaled by 10^exponent
    AsObject, ///< the nested `{"mantissa": ..., "exponent": ...}` object
};

/// Default visitor options: a compile-time policy, the `Opts` parameter of `MessageVisitor`. `Opts` need not
/// derive from it -- it only has to provide the same members (currently `decimalFormat`). Future knobs (field
/// renames, readable timestamps, ...) land here as new members/hooks, without changing the visitor and
/// `toJsonString` signatures; a custom `Opts` then has to provide them too.
struct MessageVisitorDefaultOpts {
    static constexpr auto decimalFormat = DecimalFormat::AsDouble;
};

namespace impl {

template <typename>
inline constexpr bool kAlwaysFalse = false;

} // namespace impl

/// Walks a decoded SBE message and feeds it to the writer W: `objectBegin/key/valueStr/valueNumber/...`.
/// W is any type with that interface -- `JsonWriter` is the JSON one.
template <typename W, typename Opts = MessageVisitorDefaultOpts>
struct MessageVisitor {
    W& writer;

    explicit MessageVisitor(W& w) noexcept : writer{w} {}

    template <typename M>
        requires SBEMessage<M>
    void operator()(M msg) {
        writer.objectBegin();
        writer.key("message");
        writer.valueStr(msg.sbeMessageName());
        writer.key("templateId");
        writer.valueNumber(msg.sbeTemplateId());
        sbeForEachField(msg, [&]<typename F>(F field) {
            this->operator()(field);
        });
        writer.objectEnd();
    }

    template <typename G>
        requires SBEFieldIsGroup<G>
    void operator()(G group) {
        writer.key(group.sbeRefName());
        writer.arrayBegin();
        while (group.hasNext()) {
            group.next();
            writeFields(group);
        }
        writer.arrayEnd();
    }

    template <typename D>
        requires SBEFieldIsData<D>
    void operator()(D data) {
        static_assert(std::convertible_to<decltype(data.value()), std::string_view>,
            "MessageVisitor: only text SBE data fields (varStringEncoding) can be serialized");
        writer.key(data.sbeRefName());
        writer.valueStr(data.value());
    }

    template <typename C>
        requires SBEFieldIsComposite<C>
    void operator()(C composite) {
        writer.key(composite.sbeRefName());
        if (!composite.present()) {
            return writer.valueNull();
        }
        if constexpr (SBEFieldIsCompositeDecimal<C> && Opts::decimalFormat == DecimalFormat::AsDouble) {
            auto const mantissaValue = static_cast<double>(composite.template get<"mantissa">().value());
            auto const exponentValue = static_cast<int>(composite.template get<"exponent">().value());
            writer.valueNumber(exponentValue <= 0 ? mantissaValue / std::pow(10.0, -exponentValue)
                                                  : mantissaValue * std::pow(10.0, exponentValue));
        } else {
            writeFields(composite);
        }
    }

    template <typename F>
    void operator()(F field) {
        writer.key(field.sbeRefName());
        if (!field.present()) {
            return writer.valueNull();
        }

        if constexpr (SBETypeIsSet<typename F::ValueType>) {
            auto const bits = field.value();
            writer.arrayBegin();
            for (auto const& [bit, name] : F::ValueType::sbeBitNames) {
                if ((bit & bits) == bit) {
                    writer.valueStr(name);
                }
            }
            writer.arrayEnd();
        } else if constexpr (SBETypeIsEnum<typename F::ValueType>) {
            auto const value = field.value();
            if constexpr (requires { sbeEnumNames(value); }) {
                for (auto const& [en, name] : sbeEnumNames(value)) {
                    if (value == en) {
                        return writer.valueStr(name);
                    }
                }
            }
            return writer.valueNumber(std::to_underlying(value));
        } else if constexpr (SBETypeIsNumber<typename F::ValueType>) {
            writer.valueNumber(field.value());
        } else {
            static_assert(impl::kAlwaysFalse<F>, "MessageVisitor: unsupported SBE field value type");
        }
    }

private:
    /// The named fields of a message, a group entry or a composite, as a JSON object
    template <typename T>
    void writeFields(T entry) {
        writer.objectBegin();
        sbeForEachField(entry, [&]<typename F>(F field) {
            this->operator()(field);
        });
        writer.objectEnd();
    }
};

template <typename W>
MessageVisitor(W&) -> MessageVisitor<W>;

} // namespace turbofw::detail::sbe
