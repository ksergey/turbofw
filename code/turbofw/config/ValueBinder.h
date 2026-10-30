// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cassert>
#include <concepts>
#include <string_view>

#include <turbofw/CtString.h>

#include "Exception.h"
#include "Validator.h"

namespace turbofw::config {
namespace detail {

struct None {};

template <typename T>
struct Value {
    T value;
};

} // namespace detail

/// Binder to a value
/// @tparam Name is parameter value name
/// @tparam T is parameter value type
/// @tparam DefaultValueT is storage for holding default value (if configured)
/// @tparam ValidatorT is storage for holding a value validator
template <CtString Name, typename T, typename DefaultValueT = detail::None, typename ValidatorT = detail::None>
class ValueBinder {
private:
    T* valuePtr_;
    [[no_unique_address]] DefaultValueT defaultValue_;
    [[no_unique_address]] ValidatorT validator_;

public:
    ValueBinder(ValueBinder const&) = delete;
    ValueBinder& operator=(ValueBinder const&) = delete;
    ValueBinder(ValueBinder&&) = delete;
    ValueBinder& operator=(ValueBinder&&) = delete;

    constexpr ValueBinder(T* valuePtr, DefaultValueT defaultValue = {}, ValidatorT validator = {}) noexcept
        : valuePtr_{valuePtr}, defaultValue_{defaultValue}, validator_{validator} {
        assert(valuePtr_);
    }

    /// Parameter name
    [[nodiscard]] static constexpr auto name() noexcept -> std::string_view {
        return Name;
    }

    template <typename T2>
        requires std::convertible_to<T2, T>
    [[nodiscard]] constexpr auto defaultValue(T2 value) && noexcept -> ValueBinder<Name, T, detail::Value<T2>, ValidatorT> {
        return {valuePtr_, detail::Value<T2>{value}, std::move(validator_)};
    }

    template <typename ValidatorT2>
        requires Validator<ValidatorT2>
    [[nodiscard]] constexpr auto validate(ValidatorT2 validator) && noexcept
        -> ValueBinder<Name, T, DefaultValueT, ValidatorT2> {
        return {valuePtr_, std::move(defaultValue_), std::move(validator)};
    }

    [[nodiscard]] constexpr auto valuePtr() const noexcept -> T* {
        return valuePtr_;
    }

    /// Set default value, throws on no default value available
    constexpr void setDefaultValue() const {
        if constexpr (requires { defaultValue_.value; }) {
            *valuePtr_ = defaultValue_.value;
        } else {
            throw Exception{"no default value available for \"{}\"", this->name()};
        }
    }

    /// Validate a value, throws on error
    /// do nothing on validator not available
    constexpr void validate() const {
        if constexpr (requires { validator_(*valuePtr_); }) {
            if (auto const result = validator_(*valuePtr_); !result) {
                throw ValidateError{"value for \"{}\" not pass validation", this->name()};
            }
        }
    }
};

/// Create value binder inside @c serialize
///
/// value<"param1">(&param1)
///  .defaultValue(777)
///  .validate(gt(15) && lt(30))
template <CtString Name, typename T>
[[nodiscard]] constexpr auto value(T* valuePtr) noexcept -> ValueBinder<Name, T> {
    return ValueBinder<Name, T>(valuePtr);
}

} // namespace turbofw::config
