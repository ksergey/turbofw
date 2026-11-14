// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <chrono>
#include <filesystem>
#include <print>
#include <thread>

#include <cxxopts.hpp>

#include <turbofw/Environment.h>
#include <turbofw/ProducerConsumerTraits.h>
#include <turbofw/Types.h>
#include <turbofw/mixin/SBEReceiveMixin.h>
#include <turbofw/schema/service/Heartbeat.h>
#include <turboq/MulticastMessageQueue.h>

// clang-format off
using Consumer = turbofw::MakeConsumer<turboq::MulticastMessageQueue::Consumer,
    turbofw::Mixins<
        turbofw::mixin::SBEReceiveMixin
    >
>;
// clang-format on
int main(int argc, char* argv[]) {
    try {
        auto options = cxxopts::Options{"hb_sub", "Heartbeat consumer example"};

        // clang-format off
        options.add_options()
            ("e,env", "Path to environment config", cxxopts::value<std::filesystem::path>(), "PATH")
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

        auto const env = turbofw::Environment::loadFromFile(parseResult["env"].as<std::filesystem::path>());
        if (!env) {
            throw std::runtime_error{env.error()};
        }

        auto consumer = env->createConsumer<Consumer>("heartbeat@data");
        if (!consumer) {
            throw std::runtime_error{consumer.error()};
        }

        while (true) {
            consumer->receive<turbofw::schema::service::Heartbeat>([](auto msg) {
                auto const serviceId = turbofw::ServiceId{msg.template get<"serviceId">().value()};
                auto const ts = msg.template get<"ts">().value();

                std::print("ts: {}, serviceId: {}\n", ts, serviceId);
            });
            std::this_thread::sleep_for(std::chrono::microseconds{10});
        }

    } catch (std::exception const& e) {
        std::print(stderr, "ERROR: {}\n", e.what());
    }
    return EXIT_SUCCESS;
}
