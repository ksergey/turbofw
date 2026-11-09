// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <strong_type/strong_type.hpp>

#include <turbofw/svc/Heartbeat.h>

namespace turbofw {

// FIXME: how to obtain type from schema (i.e. heartbeat message)?
using ServiceId = strong::type<std::uint32_t, struct ServiceId, strong::regular>;

} // namespace turbofw
