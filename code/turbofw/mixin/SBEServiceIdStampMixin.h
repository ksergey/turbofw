// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <limits>

#include <turbofw/SBE.h>
#include <turbofw/Types.h>

#include "detail/Random.h"

namespace turbofw::mixin {

class SBEServiceIdStampMixin {
private:
    ServiceId serviceId_;

public:
    SBEServiceIdStampMixin() noexcept {
        serviceId_.reset();
    }

    auto setServiceId(ServiceId serviceId) noexcept -> void {
        serviceId_ = serviceId;
    }

    [[nodiscard]] auto serviceId() const noexcept -> ServiceId {
        return serviceId_;
    }

    /// \ref SBESendMixin hook: stamps the mixin's serviceId into every message carrying the field
    template <typename M>
    auto updateBody([[maybe_unused]] M msg) noexcept -> void {
        if constexpr (SBEHasField<M, "serviceId">) {
            msg.template get<"serviceId">().value(value_of(serviceId_));
        }
    }
};

} // namespace turbofw::mixin
