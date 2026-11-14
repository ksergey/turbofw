// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <filesystem>

#include <turboq-logger/Logging.h>

namespace turbofw {

/// \see turboq::logger::LogLevel
using turboq::logger::LogLevel;

using turboq::logger::logLevelFromString;
using turboq::logger::logLevelToString;
using turboq::logger::setLogLevel;

/// Init logger with logging to stdout
void initLoggerStdOut();

/// Init logger with logging to the daily file "<prefix>_YYYYMMDD.log" of the day a message was
/// logged on (see turboq::logger::DailyFileSink): missing parent directories are created, files
/// are opened for appending. Messages below `level` are dropped.
///
/// Throws std::system_error if today's file can't be opened (a bad path, permissions, ...).
/// The backend thread starts itself on the first logged message; call shutdownLogger() at exit
/// to flush. Call it before the first log call: messages logged before are discarded (no sink).
void initLoggerDailyFile(std::filesystem::path const& prefix, LogLevel level);

/// Shutdown logger
void shutdownLogger();

} // namespace turbofw
