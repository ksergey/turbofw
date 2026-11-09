// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <strong_type/strong_type.hpp>

#include <turbofw/SBE.h>
#include <turbofw/svc/Heartbeat.h>

namespace turbofw {

using ServiceId = strong::type<SBEFieldValueOf<svc::Heartbeat, "serviceId">, struct ServiceId_Tag, strong::regular>;
using ClientId = strong::type<SBEFieldValueOf<svc::Heartbeat, "clientId">, struct ClientId_Tag, strong::regular>;
using RequestId = strong::type<SBEFieldValueOf<svc::Heartbeat, "requestId">, struct RequestId_Tag, strong::regular>;

} // namespace turbofw
