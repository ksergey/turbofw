// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <strong_type/strong_type.hpp>

#include <turbofw/SBE.h>
#include <turbofw/svc/Heartbeat.h>

namespace turbofw {

// Semantic alias over the wire type the schema assigns to the field: the raw type is pulled from the schema
// itself (`SBEFieldValueOf`), so schema changes keep the alias in sync instead of drifting from it.
struct ServiceIdTag {};
using ServiceId = strong::type<SBEFieldValueOf<svc::Heartbeat, "serviceId">, ServiceIdTag, strong::regular>;

} // namespace turbofw
