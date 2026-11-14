// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "Logging.h"

#include <turboq-logger/DailyFileSink.h>
#include <turboq-logger/StdOutSink.h>

namespace turbofw {

void initLoggerStdOut() {
    turboq::logger::BackendThread::setSink(std::make_shared<turboq::logger::StdOutSink>());
}

void shutdownLogger() {
    turboq::logger::BackendThread::flush();
    turboq::logger::BackendThread::stop();
}

} // namespace turbofw
