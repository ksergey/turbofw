// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "Process.h"

#include <format>
#include <stdexcept>

#include <turbofw/Logging.h>

namespace app {

Process::Process(Config const& config, Environment const& env) {
    auto const service = env.findService(config.service.name);
    if (!service) {
        throw std::runtime_error{
            std::format("service \"{}\" is not configured in the environment", config.service.name)};
    }

    auto producer = env.createProducer<Producer>(service->queues.at("data"), false);
    if (!producer) {
        throw std::runtime_error{producer.error()};
    }
    producer_ = std::move(producer).value();
    producer_.setServiceId(ServiceId{service->serviceId});

    auto consumer = env.createConsumer<Consumer>(service->queues.at("cmd"), false);
    if (!consumer) {
        throw std::runtime_error{consumer.error()};
    }
    consumer_ = std::move(consumer).value();
}

void Process::run() {
    // TODO: install signal handler
    // TODO: process signals
    // TODO: remove signal handler
}

void Process::shutdown() {}

} // namespace app
