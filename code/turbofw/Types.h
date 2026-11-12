// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <turbofw/CtString.h>
#include <turbofw/SBE.h>
#include <turbofw/StrongType.h>
#include <turbofw/schema/service/schema.h>

namespace turbofw {
namespace detail {

template <typename M, CtString N>
struct SBEFieldAutoTag;

// clang-format off
template <typename M, CtString N, typename Tag = SBEFieldAutoTag<M, N>>
    requires SBEMessage<M>
using StrongTypeFor = std::conditional_t<SBEHasFieldNull<M, N>,
    StrongType<SBEFieldValueOf<M, N>, Tag, SBEFieldNullValueOf<M, N>>,
    StrongType<SBEFieldValueOf<M, N>, Tag>
>;
// clang-format on

} // namespace detail

/// The id of a service, assigned by Metadata (never reused); `0xFFFF` is reserved for null
using ServiceId = detail::StrongTypeFor<service::Heartbeat, "serviceId">;

/// The id of a client application, chosen randomly by the client; `0xFFFFFFFF` is reserved for null
using AppId = detail::StrongTypeFor<service::SetLogLevel, "appId">;

/// The id of a client request, chosen by the client; `null` means unsolicited/spontaneous, `0` is a valid id
using RequestId = detail::StrongTypeFor<service::SetLogLevel, "requestId">;

} // namespace turbofw
