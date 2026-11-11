// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

/// A minimal service that publishes svc::Heartbeat on its multicast queue, showcasing SBEKeepAliveMixin:
/// a Start heartbeat on startHeartbeating(), periodic ones while the wire is silent, a Stop one on
/// stopHeartbeating(). The service's mpsc queue is opened but serves nothing (a real service would
/// answer e.g. SetLogLevel requests there).
///
/// Usage:
///     heartbeat -e env.json
///     heartbeat -e env.json -i 1000

#include <chrono>
#include <csignal>
#include <filesystem>
#include <print>
#include <span>
#include <thread>

#include <cxxopts.hpp>

#include <turbofw/Environment.h>
#include <turbofw/ProducerConsumerTraits.h>
#include <turbofw/mixin/ReceiveMixin.h>
#include <turbofw/mixin/SBEKeepAliveMixin.h>
#include <turbofw/mixin/SBESendMixin.h>
#include <turbofw/mixin/SBEServiceIdStampMixin.h>
#include <turboq/MPSCMessageQueue.h>
#include <turboq/MulticastMessageQueue.h>

// clang-format off
using Producer = turbofw::MakeProducer<turboq::MulticastMessageQueue::Producer,
    turbofw::Mixins<
        turbofw::mixin::SBESendMixin,
        turbofw::mixin::SBEKeepAliveMixin,
        turbofw::mixin::SBEServiceIdStampMixin
    >
>;
using Consumer = turbofw::MakeConsumer<turboq::MPSCMessageQueue::Consumer,
    turbofw::Mixins<
        turbofw::mixin::ReceiveMixin
    >
>;
// clang-format on

namespace {

std::sig_atomic_t volatile stopRequested = 0;

extern "C" auto onStop(int) -> void {
    stopRequested = 1;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        auto options = cxxopts::Options{"heartbeat", "Heartbeat service example (SBEKeepAliveMixin)"};

        // clang-format off
        options.add_options()
            ("e,env", "Path to environment config", cxxopts::value<std::filesystem::path>(), "PATH")
            ("i,interval", "Heartbeat interval, ms", cxxopts::value<std::uint32_t>()->default_value("500"), "MS")
            ("h,help", "Print this help and exit")
        ;
        // clang-format on

        auto const parseResult = options.parse(argc, argv);
        if (parseResult.count("help")) {
            std::print(stderr, "{}\n", options.help({"", "Group"}));
            return EXIT_FAILURE;
        }

        if (!parseResult.count("env")) {
            throw std::runtime_error{"option '-env' is mandatory"};
        }

        std::signal(SIGINT, &onStop);
        std::signal(SIGTERM, &onStop);

        auto const env = turbofw::Environment::loadFromConfig(parseResult["env"].as<std::filesystem::path>());
        if (!env) {
            throw std::runtime_error{env.error()};
        }

        auto producer = env->createProducer<Producer>("heartbeat@data", false);
        if (!producer) {
            throw std::runtime_error{producer.error()};
        }
        auto consumer = env->createConsumer<Consumer>("heartbeat@cmd", false);
        if (!consumer) {
            throw std::runtime_error{consumer.error()};
        }

        // Start event: consumers treat everything of the previous service instance as invalid;
        // afterwards the keep-alive mixin publishes one heartbeat per interval of wire silence.
        // No serviceId: the example is not registered in Metadata, heartbeats carry a null one.
        auto const interval = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::milliseconds{parseResult["interval"].as<std::uint32_t>()});
        producer->startHeartbeating(interval);

        while (!stopRequested) {
            // Drain the mpsc queue: this example serves no requests
            consumer->receive([](std::span<std::byte const>) {});

            // Periodic heartbeat when nothing else has been published for the configured interval
            producer->keepAlive();

            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }

        // Stop event: the service stops publishing, consumers clean up their state for it
        producer->stopHeartbeating();

    } catch (std::exception const& e) {
        std::print(stderr, "ERROR: {}\n", e.what());
    }
    return EXIT_SUCCESS;
}
