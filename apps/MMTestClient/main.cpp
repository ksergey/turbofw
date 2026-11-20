// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include <cxxopts.hpp>

#include <turbofw/SystemDefinition.h>
#include <turbofw/Types.h>

#include "Client.h"

namespace {

using namespace turbofw::schema;

enum class SendKind {
    None,
    All,
    RegisterService,
    RegisterInstrument,
    ServiceRequest,
    InstrumentRequest,
    SetLogLevel,
};

auto iequals(std::string_view lhs, std::string_view rhs) noexcept -> bool {
    return std::ranges::equal(lhs, rhs, [](char a, char b) noexcept {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
}

auto parseSendKind(std::string_view value) -> SendKind {
    if (iequals(value, "none")) {
        return SendKind::None;
    }
    if (iequals(value, "all")) {
        return SendKind::All;
    }
    if (iequals(value, "register-service")) {
        return SendKind::RegisterService;
    }
    if (iequals(value, "register-instrument")) {
        return SendKind::RegisterInstrument;
    }
    if (iequals(value, "service-request")) {
        return SendKind::ServiceRequest;
    }
    if (iequals(value, "instrument-request")) {
        return SendKind::InstrumentRequest;
    }
    if (iequals(value, "set-log-level")) {
        return SendKind::SetLogLevel;
    }
    throw std::runtime_error{std::format("unknown --send kind: {}", value)};
}

auto parseInstrumentType(std::string_view value) -> metadata::InstrumentType {
    if (iequals(value, "spot")) {
        return metadata::InstrumentType::Spot;
    }
    if (iequals(value, "future")) {
        return metadata::InstrumentType::Future;
    }
    if (iequals(value, "option")) {
        return metadata::InstrumentType::Option;
    }
    if (iequals(value, "spread")) {
        return metadata::InstrumentType::Spread;
    }
    if (iequals(value, "swap")) {
        return metadata::InstrumentType::Swap;
    }
    throw std::runtime_error{std::format("unknown instrument type: {}", value)};
}

auto parseInstrumentState(std::string_view value) -> metadata::InstrumentState {
    if (iequals(value, "active")) {
        return metadata::InstrumentState::Active;
    }
    if (iequals(value, "suspended")) {
        return metadata::InstrumentState::Suspended;
    }
    if (iequals(value, "expired")) {
        return metadata::InstrumentState::Expired;
    }
    if (iequals(value, "delisted")) {
        return metadata::InstrumentState::Delisted;
    }
    throw std::runtime_error{std::format("unknown instrument state: {}", value)};
}

auto parseLogLevel(std::string_view value) -> service::LogLevel {
    if (iequals(value, "trace")) {
        return service::LogLevel::Trace;
    }
    if (iequals(value, "debug")) {
        return service::LogLevel::Debug;
    }
    if (iequals(value, "info")) {
        return service::LogLevel::Info;
    }
    if (iequals(value, "warn")) {
        return service::LogLevel::Warn;
    }
    if (iequals(value, "error")) {
        return service::LogLevel::Error;
    }
    if (iequals(value, "off")) {
        return service::LogLevel::Off;
    }
    throw std::runtime_error{std::format("unknown log level: {}", value)};
}

auto parseFilterInstrumentType(cxxopts::ParseResult const& parseResult, bool present)
    -> std::optional<metadata::InstrumentType> {
    if (!present) {
        return std::nullopt;
    }
    return parseInstrumentType(parseResult["filter-instrument-type"].as<std::string>());
}

auto parseFilterInstrumentState(cxxopts::ParseResult const& parseResult, bool present)
    -> std::optional<metadata::InstrumentState> {
    if (!present) {
        return std::nullopt;
    }
    return parseInstrumentState(parseResult["filter-instrument-state"].as<std::string>());
}

auto report(std::expected<void, std::error_code> const& result) -> bool {
    if (!result) {
        std::println(stderr, "send failed: {}", result.error().message());
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        auto options = cxxopts::Options{"MMTestClient", "Sends requests to MotoMoto and prints the responses"};

        // clang-format off
        options.add_options()
            ("s,system", "Path to system config", cxxopts::value<std::filesystem::path>(), "PATH")
            ("n,service", "Metadata service name in the system config", cxxopts::value<std::string>()->default_value("MotoMoto"), "NAME")
            ("send", "What to send: none, all, register-service, register-instrument, service-request, instrument-request, set-log-level", cxxopts::value<std::string>()->default_value("all"), "KIND")
            ("app-id", "Pin the appId stamped into requests (random by default)", cxxopts::value<std::uint32_t>(), "N")
            ("request-id", "First requestId", cxxopts::value<std::uint64_t>()->default_value("1"), "N")
            ("listen", "Seconds to listen for responses", cxxopts::value<std::uint32_t>()->default_value("5"), "SEC")
            ("name", "Service name for register-service/service-request", cxxopts::value<std::string>()->default_value("md-demo"), "NAME")
            ("data-queue", "Data queue for register-service", cxxopts::value<std::string>()->default_value("dev/md-demo@data"), "QUEUE")
            ("request-queue", "Request queue for register-service", cxxopts::value<std::string>()->default_value("dev/md-demo@cmd"), "QUEUE")
            ("venue-id", "venueId for register-instrument", cxxopts::value<std::uint16_t>()->default_value("1"), "N")
            ("attrs-service-id", "serviceId for register-instrument", cxxopts::value<std::uint16_t>()->default_value("2"), "N")
            ("instrument-type", "InstrumentType for register-instrument", cxxopts::value<std::string>()->default_value("future"), "TYPE")
            ("instrument-state", "InstrumentState for register-instrument", cxxopts::value<std::string>()->default_value("active"), "STATE")
            ("tick-size", "tickSize mantissa (exponent -9)", cxxopts::value<std::int64_t>()->default_value("1000"), "N")
            ("lot-size", "lotSize", cxxopts::value<std::int64_t>()->default_value("1"), "N")
            ("multiplier", "multiplier", cxxopts::value<std::int64_t>()->default_value("100"), "N")
            ("currency", "currency (ISO numeric code)", cxxopts::value<std::uint32_t>()->default_value("840"), "N")
            ("exchange-symbol", "exchangeSymbol", cxxopts::value<std::string>()->default_value("BTCUSDT"), "SYMBOL")
            ("symbol", "symbol (empty: generated by Metadata)", cxxopts::value<std::string>()->default_value(""), "SYMBOL")
            ("filter-instrument-id", "instrumentId filter for instrument-request", cxxopts::value<std::uint32_t>(), "N")
            ("filter-service-id", "serviceId filter for service-request/instrument-request", cxxopts::value<std::uint16_t>(), "N")
            ("filter-venue-id", "venueId filter for instrument-request", cxxopts::value<std::uint16_t>(), "N")
            ("filter-instrument-type", "instrumentType filter for instrument-request", cxxopts::value<std::string>(), "TYPE")
            ("filter-instrument-state", "instrumentState filter for instrument-request", cxxopts::value<std::string>(), "STATE")
            ("log-level", "LogLevel for set-log-level", cxxopts::value<std::string>()->default_value("info"), "LEVEL")
            ("h,help", "Print this help and exit")
        ;
        // clang-format on

        auto const parseResult = options.parse(argc, argv);
        if (parseResult.count("help")) {
            std::print(stderr, "{}\n", options.help({"", "Group"}));
            return EXIT_SUCCESS;
        }

        if (!parseResult.count("system")) {
            throw std::runtime_error{"option '--system' is mandatory"};
        }

        auto const serviceName = parseResult["service"].as<std::string>();

        auto env = turbofw::SystemDefinition::loadFromFile(parseResult["system"].as<std::filesystem::path>())
                       .and_then([&](auto const& sys) { return sys.environmentFor(serviceName); });
        if (!env) {
            throw std::runtime_error{env.error()};
        }

        app::RegisterServiceParams serviceParams;
        serviceParams.name = parseResult["name"].as<std::string>();
        serviceParams.dataQueue = parseResult["data-queue"].as<std::string>();
        serviceParams.requestQueue = parseResult["request-queue"].as<std::string>();

        app::RegisterInstrumentParams instrumentParams;
        instrumentParams.venueId = parseResult["venue-id"].as<std::uint16_t>();
        instrumentParams.serviceId = parseResult["attrs-service-id"].as<std::uint16_t>();
        instrumentParams.instrumentType = parseInstrumentType(parseResult["instrument-type"].as<std::string>());
        instrumentParams.state = parseInstrumentState(parseResult["instrument-state"].as<std::string>());
        instrumentParams.tickSize = parseResult["tick-size"].as<std::int64_t>();
        instrumentParams.lotSize = parseResult["lot-size"].as<std::int64_t>();
        instrumentParams.multiplier = parseResult["multiplier"].as<std::int64_t>();
        instrumentParams.currency = parseResult["currency"].as<std::uint32_t>();
        instrumentParams.capabilities.mbp(true).trades(true);
        instrumentParams.exchangeSymbol = parseResult["exchange-symbol"].as<std::string>();
        instrumentParams.symbol = parseResult["symbol"].as<std::string>();

        app::ServiceRequestParams serviceRequestParams;
        if (parseResult.count("filter-service-id")) {
            serviceRequestParams.serviceId = parseResult["filter-service-id"].as<std::uint16_t>();
        }
        serviceRequestParams.name = parseResult["name"].as<std::string>();

        app::InstrumentRequestParams instrumentRequestParams;
        if (parseResult.count("filter-instrument-id")) {
            instrumentRequestParams.instrumentId = parseResult["filter-instrument-id"].as<std::uint32_t>();
        }
        if (parseResult.count("filter-venue-id")) {
            instrumentRequestParams.venueId = parseResult["filter-venue-id"].as<std::uint16_t>();
        }
        instrumentRequestParams.serviceId = serviceRequestParams.serviceId;
        instrumentRequestParams.instrumentType =
            parseFilterInstrumentType(parseResult, parseResult.count("filter-instrument-type") != 0);
        instrumentRequestParams.state =
            parseFilterInstrumentState(parseResult, parseResult.count("filter-instrument-state") != 0);
        instrumentRequestParams.exchangeSymbol = instrumentParams.exchangeSymbol;

        auto const sendKind = parseSendKind(parseResult["send"].as<std::string>());
        auto const logLevel = parseLogLevel(parseResult["log-level"].as<std::string>());
        auto const firstRequestId = parseResult["request-id"].as<std::uint64_t>();
        auto const listenSeconds = parseResult["listen"].as<std::uint32_t>();
        auto const appId = parseResult.count("app-id") ? std::optional{parseResult["app-id"].as<std::uint32_t>()}
                                                       : std::nullopt;

        app::Client client{*env, appId, firstRequestId};
        std::println("client appId={} service={} send={}", value_of(client.appId()), serviceName,
            parseResult["send"].as<std::string>());

        auto const sendAll = sendKind == SendKind::All;
        bool ok = true;
        if (sendAll || sendKind == SendKind::RegisterService) {
            ok = report(client.sendRegisterService(serviceParams)) && ok;
        }
        if (sendAll || sendKind == SendKind::RegisterInstrument) {
            ok = report(client.sendRegisterInstrument(instrumentParams)) && ok;
        }
        if (sendAll || sendKind == SendKind::ServiceRequest) {
            ok = report(client.sendServiceRequest(serviceRequestParams)) && ok;
        }
        if (sendAll || sendKind == SendKind::InstrumentRequest) {
            ok = report(client.sendInstrumentRequest(instrumentRequestParams)) && ok;
        }
        if (sendAll || sendKind == SendKind::SetLogLevel) {
            ok = report(client.sendSetLogLevel(logLevel)) && ok;
        }
        if (!ok) {
            return EXIT_FAILURE;
        }

        std::println("listening for {}s", listenSeconds);
        client.listen(std::chrono::seconds{listenSeconds});

        auto const summary = client.summary();
        std::println(
            "summary: sent={} received={} handled={} dropped={} foreign={} completed={} timeout={}", summary.sent,
            summary.received, summary.handled, summary.dropped, summary.foreign, summary.completed, summary.timeout);

        if (summary.sent > 0 && summary.completed == 0) {
            std::println(
                stderr, "no RequestComplete received: MotoMoto handlers are stubs, only Heartbeat is expected");
        }

    } catch (std::exception const& e) {
        std::println(stderr, "ERROR: {}", e.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
