// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <strong_type/strong_type.hpp>

#include <turbofw/SBE.h>
#include <turbofw/svc/schema.h>

namespace turbofw {

/// The id of a service, assigned by Metadata (never reused); `0xFFFF` is reserved for null
using ServiceId = strong::type<SBEFieldValueOf<svc::Heartbeat, "serviceId">, struct ServiceId_Tag, strong::regular>;

/// The id of a client application, chosen randomly by the client; `0xFFFFFFFF` is reserved for null
using AppId = strong::type<SBEFieldValueOf<svc::SetLogLevel, "appId">, struct AppId_Tag, strong::regular>;

/// The id of a client request, chosen by the client; `0` means unsolicited/spontaneous
using RequestId = strong::type<SBEFieldValueOf<svc::SetLogLevel, "requestId">, struct RequestId_Tag, strong::regular>;

} // namespace turbofw
