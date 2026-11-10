// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

/// A minimal service that publishes svc::Heartbeat on its multicast queue.
///
/// Usage:
///     heartbeat <environment-config.json>
///
/// The environment config (see the sample heartbeat.json next to this file) defines the service
/// scope and the two queues every service owns: a multicast queue for everything published
/// (heartbeats here) and an mpsc queue for requests (this example opens one but serves none; a
/// real service would answer e.g. SetLogLevel requests here).
///
/// Run as many instances as you like: the scope differs per config, so the queue names and
/// working directories don't collide.

#include <chrono>
#include <csignal>
#include <span>
#include <thread>
#include <utility>

#include <print>

#include <turbofw/Clock.h>
#include <turbofw/Environment.h>
#include <turbofw/mixin/ReceiveMixin.h>
#include <turbofw/mixin/SBEKeepAliveMixin.h>
#include <turbofw/mixin/SBESendMixin.h>
#include <turbofw/mp/List.h>
#include <turbofw/svc/schema.h>

namespace {

std::sig_atomic_t volatile stopRequested = 0;

extern "C" auto onStop(int) -> void {
    stopRequested = 1;
}

constexpr auto kHeartbeatInterval = std::chrono::duration<std::uint32_t, std::milli>{500};

/// Multicast producer composed with the SBE send mixin and the keep-alive mixin
struct HeartbeatProducer : public turboq::MulticastMessageQueue::Producer,
                           public turbofw::mixin::SBESendMixin,
                           public turbofw::mixin::SBEKeepAliveMixin {
    explicit HeartbeatProducer(turboq::MulticastMessageQueue::Producer producer) noexcept
        : turboq::MulticastMessageQueue::Producer{std::move(producer)} {}

    using Mixins = turbofw::mp::List<turbofw::mixin::SBEKeepAliveMixin>;

    /// Stamp serviceId into every published heartbeat; the keep-alive mixin handles Start/Stop and periodic ones
    template <typename M>
    auto updateBody(M msg) noexcept -> void {
        if constexpr (requires { msg.template get<"serviceId">(); }) {
            msg.template get<"serviceId">().reset(); // no id: the example is not registered in Metadata
        }
    }
};

/// mpsc consumer composed with the receive mixin
struct HeartbeatConsumer : public turboq::MPSCMessageQueue::Consumer, public turbofw::mixin::ReceiveMixin {
    explicit HeartbeatConsumer(turboq::MPSCMessageQueue::Consumer consumer) noexcept
        : turboq::MPSCMessageQueue::Consumer{std::move(consumer)} {}
};

} // namespace

auto main(int argc, char* argv[]) -> int {
    if (argc < 2) {
        std::println(stderr, "usage: {} <environment-config.json>", argv[0]);
        return 1;
    }
    std::signal(SIGINT, &onStop);
    std::signal(SIGTERM, &onStop);

    // The service name is only known at startup, pass it as a ${NAME} variable; the expanded
    // scope becomes ${SCOPE} for the rest of the config.
    auto vars = turbofw::Environment::Vars{{"NAME", "heartbeat"}};

    // The config has no queue names to discover here: the service owns a fixed set of queues,
    // a production service would resolve its peers' queues through Metadata instead.
    auto env = turbofw::Environment::loadFromConfig(argv[1], std::move(vars));
    if (!env) [[unlikely]] {
        std::println(stderr, "can't load \"{}\": {}", argv[1], env.error());
        return 1;
    }
    std::println("scope: {}, working directory: {}", env->scope(), env->workingDirectory().string());

    // Open the service's own queues; they are created with options from the config if missing
    auto producer = env->createProducer<HeartbeatProducer>("heartbeat@events", false);
    if (!producer) [[unlikely]] {
        std::println(stderr, "can't create producer: {}", producer.error());
        return 1;
    }
    auto consumer = env->createConsumer<HeartbeatConsumer>("heartbeat@cmd", false);
    if (!consumer) [[unlikely]] {
        std::println(stderr, "can't create consumer: {}", consumer.error());
        return 1;
    }

    // Start event: consumers treat everything of the previous service instance as invalid;
    // afterwards the keep-alive mixin publishes one heartbeat per interval of wire silence
    producer->startHeartbeating(std::chrono::duration_cast<std::chrono::nanoseconds>(kHeartbeatInterval));

    while (!stopRequested) {
        // Drain the mpsc queue: this example serves no requests; messages are dispatched by
        // (schemaId, templateId), a real service would answer e.g. SetLogLevel here.
        consumer->receive([&](std::span<std::byte const>) {});

        // Periodic heartbeat when nothing else has been published for the configured interval
        producer->keepAlive();

        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    // Stop event: the service stops publishing, consumers clean up their state for it
    producer->stopHeartbeating();
    return 0;
}
