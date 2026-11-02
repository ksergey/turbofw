// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

/// A minimal service that publishes svc::Heartbeat on its multicast queue and answers
/// svc::HeartbeatRequest on its mpsc queue.
///
/// Usage:
///     heartbeat <environment-config.json>
///
/// The environment config (see the sample heartbeat.json next to this file) defines the service
/// scope and the two queues every service owns: a multicast queue for everything published
/// (heartbeats here) and an mpsc queue for requests (HeartbeatRequest here).
///
/// Run as many instances as you like: the scope differs per config, so the queue names and
/// working directories don't collide.

#include <array>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <optional>
#include <span>
#include <thread>
#include <utility>

#include <print>

#include <turbofw/Clock.h>
#include <turbofw/Environment.h>
#include <turbofw/svc/schema.h>

namespace {

std::sig_atomic_t volatile stopRequested = 0;

extern "C" auto onStop(int) -> void {
    stopRequested = 1;
}

constexpr auto kIntervalMs = std::chrono::duration<std::uint32_t, std::milli>{500};

/// Publish a heartbeat on the multicast queue.
/// @param request responding to a HeartbeatRequest when set (echoes clientId/requestId), spontaneous otherwise
auto publishHeartbeat(turboq::MulticastMessageQueue::Producer& producer, svc::ServiceFlags flags,
    std::optional<std::pair<std::uint32_t, std::uint64_t>> request) -> bool {
    auto buffer = producer.prepare(64);
    if (buffer.empty()) [[unlikely]] {
        return false;
    }

    auto msg = svc::Heartbeat::wrapAndApplyHeader(buffer);
    if (request) {
        msg.get<"clientId">().value(request->first); // response: echoes the request's ids
        msg.get<"requestId">().value(request->second);
    } else {
        msg.get<"clientId">().reset(); // spontaneous heartbeat: no addressee
        msg.get<"requestId">().value(0);
    }
    msg.get<"serviceId">().reset(); // no id: the example is not registered in Metadata
    msg.get<"startTs">().value(turbofw::realtimeNanos());
    msg.get<"ts">().value(turbofw::realtimeNanos());
    msg.get<"intervalMs">().value(kIntervalMs.count());
    msg.get<"flags">().value(flags);
    producer.commit(8 + msg.encodedSize()); // encoded message header + body
    return true;
}

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
    auto producer = env->createProducer<turboq::MulticastMessageQueue::Producer>("heartbeat@events", false);
    if (!producer) [[unlikely]] {
        std::println(stderr, "can't create producer: {}", producer.error());
        return 1;
    }
    auto consumer = env->createConsumer<turboq::MPSCMessageQueue::Consumer>("heartbeat@cmd", false);
    if (!consumer) [[unlikely]] {
        std::println(stderr, "can't create consumer: {}", consumer.error());
        return 1;
    }

    // Start event: consumers treat everything of the previous service instance as invalid
    publishHeartbeat(*producer, svc::ServiceFlags{}.start(true), std::nullopt);

    auto nextHeartbeat = turbofw::monotonicNanos();
    while (!stopRequested) {
        // Serve requests from the mpsc queue; messages are dispatched by (schemaId, templateId)
        for (;;) {
            auto const buffer = consumer->fetch();
            if (buffer.empty()) {
                break;
            }
            auto const header = svc::MessageHeader{const_cast<std::byte*>(buffer.data())};
            if (header.get<"schemaId">().value() == svc::HeartbeatRequest::sbeSchemaId() &&
                header.get<"templateId">().value() == svc::HeartbeatRequest::sbeTemplateId()) {
                auto const bytes = std::span<std::byte>{const_cast<std::byte*>(buffer.data()), buffer.size()};
                auto request = svc::HeartbeatRequest{
                    bytes, header.sbeEncodedLength(), header.get<"blockLength">().value(),
                    header.get<"version">().value()};
                std::println("heartbeat request from client {}", request.get<"clientId">().value());
                publishHeartbeat(*producer, svc::ServiceFlags{},
                    std::make_pair(request.get<"clientId">().value(), request.get<"requestId">().value()));
            }
            consumer->consume();
        }

        // Periodic heartbeat when nothing else has been published for intervalMs
        auto const now = turbofw::monotonicNanos();
        if (now >= nextHeartbeat) {
            nextHeartbeat = now + kIntervalMs.count() * 1'000'000ull;
            publishHeartbeat(*producer, svc::ServiceFlags{}, std::nullopt);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    // Stop event: the service stops publishing, consumers clean up their state for it
    publishHeartbeat(*producer, svc::ServiceFlags{}.stop(true), std::nullopt);
    return 0;
}
