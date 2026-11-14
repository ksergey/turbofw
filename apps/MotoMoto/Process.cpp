// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "Process.h"

#include <format>
#include <stdexcept>
#include <utility>

#include <chrono>
#include <thread>

#include <turbofw/Logging.h>
#include <turbofw/Signals.h>

namespace app {

using turbofw::installDefaultSignalHandlers;
using turbofw::processSignal;
using turbofw::removeDefaultSignalHandlers;

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
    if (std::exchange(running_, true)) {
        return;
    }

    installDefaultSignalHandlers();

    producer_.startHeartbeating();

    while (running_) {
        tq_info("running...");

        consumer_.receive<Requests>([&]<typename M>(M req) {
            this->processRequest(req);
        });

        producer_.keepAlive();

        std::this_thread::sleep_for(std::chrono::seconds{1});

        processSignal([&]([[maybe_unused]] int sigNo) {
            this->shutdown();
        });
    }

    producer_.startHeartbeating();

    removeDefaultSignalHandlers();
}

void Process::shutdown() {
    tq_notice("shutdown");
    running_ = false;
}

void Process::processRequest(turbofw::schema::metadata::RegisterInstrument req) {
    (void)req;
}

void Process::processRequest(turbofw::schema::metadata::ServiceRequest req) {
    (void)req;
}

} // namespace app
