// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <turbofw/SBE.h>
#include <turbofw/StrongType.h>
#include <turbofw/svc/schema.h>

namespace turbofw {

/// The id of a service, assigned by Metadata (never reused); `0xFFFF` is reserved for null
using ServiceId = StrongType<SBEFieldValueOf<svc::Heartbeat, "serviceId">, struct ServiceId_Tag,
    SBEFieldNullValueOf<svc::Heartbeat, "serviceId">>;

/// The id of a client application, chosen randomly by the client; `0xFFFFFFFF` is reserved for null
using AppId = StrongType<SBEFieldValueOf<svc::SetLogLevel, "appId">, struct AppId_Tag,
    SBEFieldNullValueOf<svc::SetLogLevel, "appId">>;

/// The id of a client request, chosen by the client; `null` means unsolicited/spontaneous, `0` is a valid id
using RequestId = StrongType<SBEFieldValueOf<svc::SetLogLevel, "requestId">, struct RequestId_Tag,
    SBEFieldNullValueOf<svc::SetLogLevel, "requestId">>;

} // namespace turbofw
