// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <turbofw/SBESchemaUtils.h>

namespace turbofw::detail::sbe {

template <typename W>
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
            writer.objectBegin();
            sbeForEachField(group, [&]<typename S>(S sub) {
                this->operator()(sub);
            });
            writer.objectEnd();
        }
        writer.arrayEnd();
    }

    template <typename D>
        requires SBEFieldIsData<D>
    void operator()(D data) {
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
        if constexpr (SBEFieldIsCompositeDecimal<C> && false) { // TODO
            auto const mantissaValue = static_cast<double>(composite.template get<"mantissa">().value());
            auto const exponentValue = static_cast<double>(composite.template get<"exponent">().value());
            writer.valueNumber(exponentValue <= 0.0 ? (mantissaValue / std::pow(10.0, -exponentValue))
                                                    : (mantissaValue * std::pow(10.0, exponentValue)));

        } else {
            writer.objectBegin();
            sbeForEachField(composite, [&]<typename S>(S sub) {
                this->operator()(sub);
            });
            writer.objectEnd();
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
            writer.valueStr("<SHIT>");
            // writer.valueShit();
        }
    }
};

template <typename W>
MessageVisitor(W&) -> MessageVisitor<W>;

} // namespace turbofw::detail::sbe
