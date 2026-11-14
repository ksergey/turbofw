// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <turboq-logger/Logging.h>

namespace turbofw {

/// \see turboq::logger::LogLevel
using turboq::logger::LogLevel;

using turboq::logger::logLevelFromString;
using turboq::logger::logLevelToString;
using turboq::logger::setLogLevel;

/// Init logger with logging to stdout
void initLoggerStdOut();

/// Shutdown logger
void shutdownLogger();

} // namespace turbofw
