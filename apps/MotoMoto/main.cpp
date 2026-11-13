// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <print>
#include <thread>
#include <utility>

#include <cxxopts.hpp>

#include <turbofw/Environment.h>
#include <turbofw/ProducerConsumerTraits.h>
#include <turbofw/Types.h>
#include <turbofw/mixin/SBEKeepAliveMixin.h>
#include <turbofw/mixin/SBEReceiveMixin.h>
#include <turbofw/mixin/SBESendMixin.h>
#include <turbofw/mixin/SBEServiceIdStampMixin.h>
#include <turbofw/mp/List.h>
#include <turbofw/schema/metadata/schema.h>
#include <turbofw/schema/service/schema.h>
#include <turboq/MPSCMessageQueue.h>
#include <turboq/MulticastMessageQueue.h>

#include "store.h"

// clang-format off
using OutProducer = turbofw::MakeProducer<turboq::MulticastMessageQueue::Producer,
    turbofw::Mixins<
        turbofw::mixin::SBESendMixin,
        turbofw::mixin::SBEServiceIdStampMixin,
        turbofw::mixin::SBEKeepAliveMixin
    >
>;
using InConsumer = turbofw::MakeConsumer<turboq::MPSCMessageQueue::Consumer,
    turbofw::Mixins<
        turbofw::mixin::SBEReceiveMixin
    >
>;
// clang-format on

namespace {

/// Requests MotoMoto serves: its own protocol plus the service-wide control message
using Requests = turbofw::mp::List<turbofw::schema::metadata::RegisterService,
    turbofw::schema::metadata::RegisterInstrument, turbofw::schema::metadata::ServiceRequest,
    turbofw::schema::metadata::InstrumentRequest, turbofw::schema::service::SetLogLevel>;

/// MotoMoto's name in the "services" section of the environment config
constexpr auto kServiceName = "metadata";

/// Loop exit flag; written from signal handlers, hence the atomic
std::atomic<bool> stopRequested{false};

using MetadataServiceState = turbofw::schema::metadata::ServiceState;
using MetadataInstrumentType = turbofw::schema::metadata::InstrumentType;
using MetadataInstrumentState = turbofw::schema::metadata::InstrumentState;
using MetadataCapabilities = turbofw::schema::metadata::Capabilities;
using MetadataRegisterStatus = turbofw::schema::metadata::RegisterStatus;
using ServiceRequestStatus = turbofw::schema::service::RequestStatus;
using MetadataRequestStatus = turbofw::schema::metadata::RequestStatus;

/// The metadata protocol: translates schema messages to/from the store.
/// Responses echo (appId, requestId); spontaneous messages carry both null.
/// Store failures are printed and the message is dropped: there is no retry yet (TODO).
class Dispatcher {
public:
    Dispatcher(OutProducer& producer, motomoto::Store& store) noexcept : producer_{producer}, store_{store} {}

    void operator()(turbofw::schema::metadata::RegisterService msg) {
        auto const name = std::string{msg.get<"name">().value()};
        auto const dataQueue = std::string{msg.get<"dataQueue">().value()};
        auto const requestQueue = std::string{msg.get<"requestQueue">().value()};

        auto const result = store_.upsertService({
            .name = name,
            .dataQueue = dataQueue,
            .requestQueue = requestQueue,
            .state = toStore(msg.get<"state">().value()),
            .reportedTs = msg.get<"reportedTs">().value(),
        });
        if (!result) {
            std::println(stderr, "upsertService(\"{}\") failed: {}", name, result.error());
            return;
        }

        auto const sent = producer_.send<turbofw::schema::metadata::RegisterServiceResult>([&](auto body) {
            echoRequestIds(body, msg.get<"appId">().value(), requestIdOf(msg));
            body.template get<"status">().value(toSchema(result->status));
            if (result->status == motomoto::RegisterStatus::Rejected) {
                body.template get<"serviceId">().reset();
            } else {
                body.template get<"serviceId">().value(result->serviceId);
            }
            body.template get<"version">().value(result->version);
        });
        if (!sent) {
            std::println(stderr, "can't send RegisterServiceResult: {}", sent.error().message());
        }

        if (result->status == motomoto::RegisterStatus::Created ||
            result->status == motomoto::RegisterStatus::Updated) {
            motomoto::ServiceRow const row{
                .name = name,
                .serviceId = result->serviceId,
                .version = result->version,
                .reportedTs = msg.get<"reportedTs">().value(),
                .state = toStore(msg.get<"state">().value()),
                .dataQueue = dataQueue,
                .requestQueue = requestQueue,
            };
            static_cast<void>(sendServiceDefinition(row, std::nullopt, std::nullopt));
        }
    }

    void operator()(turbofw::schema::metadata::RegisterInstrument msg) {
        auto const exchangeSymbol = std::string{msg.get<"exchangeSymbol">().value()};
        auto const attrs = msg.get<"attrs">();

        auto const result = store_.upsertInstrument({
            .serviceId = attrs.template get<"serviceId">().value(),
            .venueId = attrs.template get<"venueId">().value(),
            .exchangeSymbol = exchangeSymbol,
            .symbol = std::string{msg.get<"symbol">().value()},
            .instrumentType = toStore(attrs.template get<"instrumentType">().value()),
            .state = toStore(attrs.template get<"state">().value()),
            .tickSize = optionalOf(attrs.template get<"tickSize">().template get<"mantissa">()),
            .lotSize = optionalOf(attrs.template get<"lotSize">()),
            .multiplier = optionalOf(attrs.template get<"multiplier">()),
            .expiry = optionalOf(attrs.template get<"expiry">()),
            .currency = optionalOf(attrs.template get<"currency">()),
            .capabilities = static_cast<std::uint16_t>(attrs.template get<"capabilities">().value()),
            .reportedTs = msg.get<"reportedTs">().value(),
        });
        if (!result) {
            std::println(stderr, "upsertInstrument(\"{}\") failed: {}", exchangeSymbol, result.error());
            return;
        }

        auto const sent = producer_.send<turbofw::schema::metadata::RegisterInstrumentResult>([&](auto body) {
            echoRequestIds(body, msg.get<"appId">().value(), requestIdOf(msg));
            body.template get<"status">().value(toSchema(result->status));
            if (result->instrumentId.has_value()) {
                body.template get<"instrumentId">().value(*result->instrumentId);
            } else {
                body.template get<"instrumentId">().reset();
            }
            body.template get<"version">().value(result->version);
        });
        if (!sent) {
            std::println(stderr, "can't send RegisterInstrumentResult: {}", sent.error().message());
        }

        if (result->status == motomoto::RegisterStatus::Created ||
            result->status == motomoto::RegisterStatus::Updated) {
            // re-read the stored row: the symbol may have been generated, the version incremented
            auto const rows = store_.queryInstruments({
                .instrumentId = std::nullopt,
                .venueId = attrs.template get<"venueId">().value(),
                .serviceId = attrs.template get<"serviceId">().value(),
                .instrumentType = std::nullopt,
                .state = std::nullopt,
                .exchangeSymbol = exchangeSymbol,
            });
            if (rows && !rows->empty()) {
                static_cast<void>(sendInstrumentDefinition(rows->front(), std::nullopt, std::nullopt));
            }
        }
    }

    void operator()(turbofw::schema::metadata::ServiceRequest msg) {
        auto const query = motomoto::ServiceQuery{
            .serviceId = optionalField<std::uint16_t>(msg.get<"serviceId">()),
            .state = optionalField<motomoto::ServiceState>(msg.get<"state">()),
            .name = msg.get<"name">().value(),
        };
        auto const rows = store_.queryServices(query);
        if (!rows) {
            std::println(stderr, "queryServices failed: {}", rows.error());
            sendRequestComplete(msg, MetadataRequestStatus::Rejected, 0);
            return;
        }

        for (auto const& row : *rows) {
            if (!sendServiceDefinition(row, msg.get<"appId">().value(), requestIdOf(msg))) {
                return;
            }
        }
        sendRequestComplete(msg, MetadataRequestStatus::Ok, rows->size());
    }

    void operator()(turbofw::schema::metadata::InstrumentRequest msg) {
        if (!msg.get<"exchangeSymbol">().value().empty() && !msg.get<"venueId">().present()) {
            // a non-empty exchangeSymbol requires venueId: refuse instead of returning everything
            sendRequestComplete(msg, MetadataRequestStatus::InvalidRequest, 0);
            return;
        }

        auto const query = motomoto::InstrumentQuery{
            .instrumentId = optionalField<std::uint32_t>(msg.get<"instrumentId">()),
            .venueId = optionalField<std::uint16_t>(msg.get<"venueId">()),
            .serviceId = optionalField<std::uint16_t>(msg.get<"serviceId">()),
            .instrumentType = optionalField<motomoto::InstrumentType>(msg.get<"instrumentType">()),
            .state = optionalField<motomoto::InstrumentState>(msg.get<"state">()),
            .exchangeSymbol = msg.get<"exchangeSymbol">().value(),
        };
        auto const rows = store_.queryInstruments(query);
        if (!rows) {
            std::println(stderr, "queryInstruments failed: {}", rows.error());
            sendRequestComplete(msg, MetadataRequestStatus::Rejected, 0);
            return;
        }

        for (auto const& row : *rows) {
            if (!sendInstrumentDefinition(row, msg.get<"appId">().value(), requestIdOf(msg))) {
                return;
            }
        }
        sendRequestComplete(msg, MetadataRequestStatus::Ok, rows->size());
    }

    void operator()(turbofw::schema::service::SetLogLevel msg) {
        // TODO: the runtime logger is not wired yet: acknowledge with the requested level
        auto const appId = msg.get<"appId">();
        auto const sent = producer_.send<turbofw::schema::service::SetLogLevelResult>([&](auto body) {
            if (appId.present()) {
                body.template get<"appId">().value(appId.value());
            } else {
                body.template get<"appId">().reset();
            }
            echoRequestId(body, requestIdOf(msg));
            body.template get<"status">().value(ServiceRequestStatus::Ok);
            body.template get<"level">().value(msg.get<"level">().value());
        });
        if (!sent) {
            std::println(stderr, "can't send SetLogLevelResult: {}", sent.error().message());
        }
    }

private:
    template <typename Body>
    static void echoRequestIds(Body& body, std::uint32_t appId, std::optional<std::uint64_t> requestId) {
        body.template get<"appId">().value(appId);
        echoRequestId(body, requestId);
    }

    template <typename Body>
    static void echoRequestId(Body& body, std::optional<std::uint64_t> requestId) {
        if (requestId.has_value()) {
            body.template get<"requestId">().value(*requestId);
        } else {
            body.template get<"requestId">().reset();
        }
    }

    template <typename Msg>
    static auto requestIdOf(Msg msg) -> std::optional<std::uint64_t> {
        auto const ref = msg.template get<"requestId">();
        return ref.present() ? std::optional{ref.value()} : std::nullopt;
    }

    template <typename Ref>
    static auto optionalOf(Ref const& ref) -> std::optional<std::int64_t> {
        return ref.present() ? std::optional{ref.value()} : std::nullopt;
    }

    template <typename T>
    static auto optionalField(auto const& ref) -> std::optional<T> {
        if (!ref.present()) {
            return std::nullopt;
        }
        if constexpr (std::is_enum_v<T>) {
            return static_cast<T>(std::to_underlying(ref.value()));
        } else {
            return static_cast<T>(ref.value());
        }
    }

    static auto toStore(MetadataServiceState value) noexcept -> motomoto::ServiceState {
        return static_cast<motomoto::ServiceState>(std::to_underlying(value));
    }

    static auto toStore(MetadataInstrumentType value) noexcept -> motomoto::InstrumentType {
        return static_cast<motomoto::InstrumentType>(std::to_underlying(value));
    }

    static auto toStore(MetadataInstrumentState value) noexcept -> motomoto::InstrumentState {
        return static_cast<motomoto::InstrumentState>(std::to_underlying(value));
    }

    static auto toSchema(motomoto::RegisterStatus value) noexcept -> MetadataRegisterStatus {
        return static_cast<MetadataRegisterStatus>(std::to_underlying(value));
    }

    static auto toSchema(motomoto::ServiceState value) noexcept -> MetadataServiceState {
        return static_cast<MetadataServiceState>(std::to_underlying(value));
    }

    static auto toSchema(motomoto::InstrumentType value) noexcept -> MetadataInstrumentType {
        return static_cast<MetadataInstrumentType>(std::to_underlying(value));
    }

    static auto toSchema(motomoto::InstrumentState value) noexcept -> MetadataInstrumentState {
        return static_cast<MetadataInstrumentState>(std::to_underlying(value));
    }

    [[nodiscard]] auto sendServiceDefinition(motomoto::ServiceRow const& row, std::optional<std::uint32_t> appId,
        std::optional<std::uint64_t> requestId) -> bool {
        // data lengths of the var fields; send computes the exact encoded size from them
        auto const hint = turbofw::schema::metadata::ServiceDefinition::SizeHint{
            row.name.size(), row.dataQueue.size(), row.requestQueue.size()};
        auto const sent = producer_.send<turbofw::schema::metadata::ServiceDefinition>(hint, [&](auto body) {
            if (appId.has_value()) {
                body.template get<"appId">().value(*appId);
            } else {
                body.template get<"appId">().reset();
            }
            echoRequestId(body, requestId);
            body.template get<"serviceId">().value(row.serviceId);
            body.template get<"version">().value(row.version);
            body.template get<"reportedTs">().value(row.reportedTs);
            body.template get<"state">().value(toSchema(row.state));
            body.template get<"name">().value(row.name);
            body.template get<"dataQueue">().value(row.dataQueue);
            body.template get<"requestQueue">().value(row.requestQueue);
        });
        if (!sent) {
            std::println(stderr, "can't send ServiceDefinition: {}", sent.error().message());
            return false;
        }
        return true;
    }

    [[nodiscard]] auto sendInstrumentDefinition(motomoto::InstrumentRow const& row, std::optional<std::uint32_t> appId,
        std::optional<std::uint64_t> requestId) -> bool {
        auto const hint =
            turbofw::schema::metadata::InstrumentDefinition::SizeHint{row.exchangeSymbol.size(), row.symbol.size()};
        auto const sent = producer_.send<turbofw::schema::metadata::InstrumentDefinition>(hint, [&](auto body) {
            if (appId.has_value()) {
                body.template get<"appId">().value(*appId);
            } else {
                body.template get<"appId">().reset();
            }
            echoRequestId(body, requestId);
            body.template get<"instrumentId">().value(row.instrumentId);
            body.template get<"version">().value(row.version);
            body.template get<"reportedTs">().value(row.reportedTs);
            auto const attrs = body.template get<"attrs">();
            attrs.template get<"venueId">().value(row.venueId);
            attrs.template get<"serviceId">().value(row.serviceId);
            attrs.template get<"instrumentType">().value(toSchema(row.instrumentType));
            attrs.template get<"state">().value(toSchema(row.state));
            if (row.tickSize.has_value()) {
                attrs.template get<"tickSize">().template get<"mantissa">().value(*row.tickSize);
            } else {
                attrs.template get<"tickSize">().template get<"mantissa">().reset();
            }
            if (row.lotSize.has_value()) {
                attrs.template get<"lotSize">().value(*row.lotSize);
            } else {
                attrs.template get<"lotSize">().reset();
            }
            if (row.multiplier.has_value()) {
                attrs.template get<"multiplier">().value(*row.multiplier);
            } else {
                attrs.template get<"multiplier">().reset();
            }
            if (row.expiry.has_value()) {
                attrs.template get<"expiry">().value(*row.expiry);
            } else {
                attrs.template get<"expiry">().reset();
            }
            if (row.currency.has_value()) {
                attrs.template get<"currency">().value(*row.currency);
            } else {
                attrs.template get<"currency">().reset();
            }
            attrs.template get<"capabilities">().value(MetadataCapabilities{row.capabilities});
            body.template get<"exchangeSymbol">().value(row.exchangeSymbol);
            body.template get<"symbol">().value(row.symbol);
        });
        if (!sent) {
            std::println(stderr, "can't send InstrumentDefinition: {}", sent.error().message());
            return false;
        }
        return true;
    }

    template <typename Msg>
    void sendRequestComplete(Msg msg, MetadataRequestStatus status, std::size_t count) {
        auto const sent = producer_.send<turbofw::schema::metadata::RequestComplete>([&](auto body) {
            echoRequestIds(body, msg.template get<"appId">().value(), requestIdOf(msg));
            body.template get<"status">().value(status);
            body.template get<"count">().value(static_cast<std::uint32_t>(count));
        });
        if (!sent) {
            std::println(stderr, "can't send RequestComplete: {}", sent.error().message());
        }
    }

    OutProducer& producer_;
    motomoto::Store& store_;
};

} // namespace

int main(int argc, char* argv[]) {
    try {
        auto options = cxxopts::Options{"MotoMoto", "The Metadata service"};
        // clang-format off
        options.add_options()
            ("e,env", "Path to environment config", cxxopts::value<std::filesystem::path>(), "PATH")
            ("h,help", "Print this help and exit")
        ;
        // clang-format on

        auto const parseResult = options.parse(argc, argv);
        if (parseResult.count("help")) {
            std::print(stderr, "{}\n", options.help({"", "Group"}));
            return EXIT_SUCCESS;
        }
        if (!parseResult.count("env")) {
            throw std::runtime_error{"option '--env' is mandatory"};
        }

        auto const env = turbofw::Environment::loadFromConfig(parseResult["env"].as<std::filesystem::path>());
        if (!env) {
            throw std::runtime_error{env.error()};
        }

        auto const* service = env->findService(kServiceName);
        if (service == nullptr) {
            throw std::runtime_error{std::format("service \"{}\" is not configured in the environment", kServiceName)};
        }

        auto store = [&] {
            auto const dbPath = env->workingDirectory().empty() ? std::filesystem::path{"MotoMoto.db"}
                                                                : env->workingDirectory() / "MotoMoto.db";
            auto result = motomoto::Store::open(dbPath);
            if (!result) {
                throw std::runtime_error{result.error()};
            }
            return std::move(result).value();
        }();

        auto producer = env->createProducer<OutProducer>(service->queues.at("data"), false);
        if (!producer) {
            throw std::runtime_error{producer.error()};
        }
        auto consumer = env->createConsumer<InConsumer>(service->queues.at("cmd"), false);
        if (!consumer) {
            throw std::runtime_error{consumer.error()};
        }

        producer->setServiceId(turbofw::ServiceId{service->serviceId});

        std::signal(SIGINT, [](int) {
            stopRequested.store(true, std::memory_order_relaxed);
        });
        std::signal(SIGTERM, [](int) {
            stopRequested.store(true, std::memory_order_relaxed);
        });

        auto dispatcher = Dispatcher{*producer, store};
        producer->startHeartbeating();
        std::println(
            stdout, "MotoMoto started: data=\"{}\" cmd=\"{}\"", service->queues.at("data"), service->queues.at("cmd"));

        while (!stopRequested.load(std::memory_order_relaxed)) {
            auto const counters = consumer->receive<Requests>(dispatcher);
            producer->keepAlive();
            if (counters.received == 0) {
                // TODO: a blocking wait (or an event source) instead of the poll
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
        }

        producer->stopHeartbeating();
        std::println(stdout, "MotoMoto stopped");

    } catch (std::exception const& e) {
        std::println(stderr, "ERROR: {}", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
