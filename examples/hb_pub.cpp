// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <chrono>
#include <filesystem>
#include <print>
#include <thread>

#include <cxxopts.hpp>

#include <turbofw/Clock.h>
#include <turbofw/Environment.h>
#include <turbofw/ProducerConsumerTraits.h>
#include <turbofw/mixin/SBESendMixin.h>
#include <turbofw/mixin/SBEServiceIdStampMixin.h>
#include <turbofw/schema/service/Heartbeat.h>
#include <turboq/MulticastMessageQueue.h>

// clang-format off
using Producer = turbofw::MakeProducer<turboq::MulticastMessageQueue::Producer,
    turbofw::Mixins<
        turbofw::mixin::SBESendMixin,
        turbofw::mixin::SBEServiceIdStampMixin
    >
>;
// clang-format on
int main(int argc, char* argv[]) {
    try {
        auto options = cxxopts::Options{"hb_pub", "Heartbeat publish example"};

        // clang-format off
        options.add_options()
            ("e,env", "Path to environment config", cxxopts::value<std::filesystem::path>(), "PATH")
            ("service-id", "Heartbeater serviceId", cxxopts::value<std::uint16_t>(), "NUMBER")
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

        auto const env = turbofw::Environment::loadFromConfig(parseResult["env"].as<std::filesystem::path>());
        if (!env) {
            throw std::runtime_error{env.error()};
        }

        auto producer = env->createProducer<Producer>("heartbeat@data", false);
        if (!producer) {
            throw std::runtime_error{producer.error()};
        }

        if (parseResult.count("service-id")) {
            auto const serviceId = turbofw::ServiceId{parseResult["service-id"].as<std::uint16_t>()};
            producer->setServiceId(serviceId);
        }

        while (true) {
            auto const sent = producer->send<service::Heartbeat>([](auto body) {
                body.template get<"ts">().value(turbofw::realtimeNanos());
                body.template get<"flags">().value(service::ServiceFlags{});
            });
            if (sent) {
                std::print(stdout, "heartbeat sent\n");
            }
            std::this_thread::sleep_for(std::chrono::seconds{1});
        }

    } catch (std::exception const& e) {
        std::print(stderr, "ERROR: {}\n", e.what());
    }
    return EXIT_SUCCESS;
}
