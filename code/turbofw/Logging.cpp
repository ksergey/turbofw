// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "Logging.h"

#include <turboq-logger/DailyFileSink.h>
#include <turboq-logger/StdOutSink.h>

namespace turbofw {

void initLoggerStdOut() {
    turboq::logger::BackendThread::setSink(std::make_shared<turboq::logger::StdOutSink>());
}

void initLoggerDailyFile(std::filesystem::path const& prefix, LogLevel level) {
    turboq::logger::setLogLevel(level);
    turboq::logger::BackendThread::setSink(std::make_shared<turboq::logger::DailyFileSink>(prefix));
}

void shutdownLogger() {
    turboq::logger::BackendThread::flush();
    turboq::logger::BackendThread::stop();
}

} // namespace turbofw
