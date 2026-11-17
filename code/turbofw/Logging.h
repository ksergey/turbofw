// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <filesystem>

#include <turboq-logger/Logging.h>
#include <turboq/Platform.h>

namespace turbofw {

using turboq::logger::LogLevel;
using turboq::logger::logLevelFromString;
using turboq::logger::logLevelToString;
using turboq::logger::setLogLevel;

/// Return true on logger backend running
TURBOQ_FORCE_INLINE auto isLoggerBackendRunning() noexcept -> bool {
    return turboq::logger::BackendThread::isRunning();
}

} // namespace turbofw
