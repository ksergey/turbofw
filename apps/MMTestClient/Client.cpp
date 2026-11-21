// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "Client.h"

#include <algorithm>
#include <concepts>
#include <format>
#include <functional>
#include <print>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>

#include <turbofw/Clock.h>
#include <turbofw/SBE.h>
#include <turbofw/SBESchemaUtils.h>

namespace app {
namespace {

using namespace turbofw::schema;

template <typename E>
auto enumName(E value) noexcept -> std::string_view {
    for (auto const& [candidate, name] : sbeEnumNames(value)) {
        if (candidate == value) {
            return name;
        }
    }
    return "<unknown>";
}

template <typename M>
auto messageAppId([[maybe_unused]] M msg) noexcept -> std::optional<std::uint32_t> {
    if constexpr (!turbofw::SBEHasField<M, "appId">) {
        return std::nullopt;
    } else if constexpr (turbofw::SBEHasFieldNull<M, "appId">) {
        auto const ref = msg.template get<"appId">();
        if (ref.present()) {
            return ref.value();
        }
        return std::nullopt;
    } else {
        return msg.template get<"appId">().value();
    }
}

template <typename M>
auto messageRequestId([[maybe_unused]] M msg) noexcept -> std::optional<std::uint64_t> {
    if constexpr (!turbofw::SBEHasField<M, "requestId">) {
        return std::nullopt;
    } else if constexpr (turbofw::SBEHasFieldNull<M, "requestId">) {
        auto const ref = msg.template get<"requestId">();
        if (ref.present()) {
            return ref.value();
        }
        return std::nullopt;
    } else {
        return msg.template get<"requestId">().value();
    }
}

} // namespace

auto Client::makeProducer(Environment const& env, std::optional<std::uint32_t> appId) -> Producer {
    auto producer = env.createProducer<Client::Producer>("cmd");
    if (!producer) {
        throw std::runtime_error{producer.error()};
    }
    if (appId) {
        producer->setAppId(turbofw::AppId{*appId});
    }
    return std::move(*producer);
}

Client::Client(Environment const& env, std::optional<std::uint32_t> appId, std::uint64_t firstRequestId)
    : producer_{makeProducer(env, appId)}, consumer_{[&env] {
          auto consumer = env.createConsumer<Consumer>("data");
          if (!consumer) {
              throw std::runtime_error{consumer.error()};
          }
          return std::move(*consumer);
      }()},
      nextRequestId_{firstRequestId} {}

template <typename M, typename Fn>
auto Client::sendVars(typename M::SizeHint hint, Fn&& fn) -> std::expected<void, std::error_code> {
    auto const requestId = nextRequestId_++;
    auto const bufferSize = M::Header::sbeEncodedLength() + M::sbeComputeSize(hint);

    auto const result = producer_.template send<M>(std::move(hint), [&](auto msg) {
        if constexpr (turbofw::SBEHasField<M, "requestId">) {
            msg.template get<"requestId">().value(requestId);
        }
        std::invoke(fn, msg);
    });
    if (!result) {
        return result;
    }

    awaiting_.push_back(requestId);
    ++summary_.sent;
    std::println("sent {} appId={} requestId={} bytes={}", M::sbeMessageName(), value_of(producer_.appId()), requestId,
        bufferSize);
    return {};
}

template <typename M, typename Fn>
auto Client::sendFixed(Fn&& fn) -> std::expected<void, std::error_code> {
    auto const requestId = nextRequestId_++;
    constexpr auto bufferSize = M::Header::sbeEncodedLength() + M::sbeComputeSize();

    auto const result = producer_.template send<M>([&](auto msg) {
        if constexpr (turbofw::SBEHasField<M, "requestId">) {
            msg.template get<"requestId">().value(requestId);
        }
        std::invoke(fn, msg);
    });
    if (!result) {
        return result;
    }

    awaiting_.push_back(requestId);
    ++summary_.sent;
    std::println("sent {} appId={} requestId={} bytes={}", M::sbeMessageName(), value_of(producer_.appId()), requestId,
        bufferSize);
    return {};
}

auto Client::sendRegisterService(RegisterServiceParams const& params) -> std::expected<void, std::error_code> {
    return sendVars<metadata::RegisterService>(
        {params.name.size(), params.dataQueue.size(), params.requestQueue.size()}, [&params](auto msg) {
            msg.template get<"reportedTs">().value(static_cast<std::int64_t>(turbofw::realtimeNanos()));
            msg.template get<"state">().value(metadata::ServiceState::Active);
            msg.template get<"name">().value(params.name);
            msg.template get<"dataQueue">().value(params.dataQueue);
            msg.template get<"requestQueue">().value(params.requestQueue);
        });
}

auto Client::sendRegisterInstrument(RegisterInstrumentParams const& params) -> std::expected<void, std::error_code> {
    return sendVars<metadata::RegisterInstrument>(
        {params.exchangeSymbol.size(), params.symbol.size()}, [&params](auto msg) {
            msg.template get<"reportedTs">().value(static_cast<std::int64_t>(turbofw::realtimeNanos()));
            auto attrs = msg.template get<"attrs">();
            attrs.template get<"venueId">().value(params.venueId);
            attrs.template get<"serviceId">().value(params.serviceId);
            attrs.template get<"instrumentType">().value(params.instrumentType);
            attrs.template get<"state">().value(params.state);
            attrs.template get<"tickSize">().template get<"mantissa">().value(params.tickSize);
            attrs.template get<"lotSize">().value(params.lotSize);
            attrs.template get<"multiplier">().value(params.multiplier);
            attrs.template get<"expiry">().reset();
            attrs.template get<"currency">().value(params.currency);
            attrs.template get<"capabilities">().value(params.capabilities);
            msg.template get<"exchangeSymbol">().value(params.exchangeSymbol);
            msg.template get<"symbol">().value(params.symbol);
        });
}

auto Client::sendServiceRequest(ServiceRequestParams const& params) -> std::expected<void, std::error_code> {
    return sendVars<metadata::ServiceRequest>({params.name.size()}, [&params](auto msg) {
        if (params.serviceId) {
            msg.template get<"serviceId">().value(*params.serviceId);
        } else {
            msg.template get<"serviceId">().reset();
        }
        msg.template get<"state">().reset();
        msg.template get<"name">().value(params.name);
    });
}

auto Client::sendInstrumentRequest(InstrumentRequestParams const& params) -> std::expected<void, std::error_code> {
    return sendVars<metadata::InstrumentRequest>({params.exchangeSymbol.size()}, [&params](auto msg) {
        if (params.instrumentId) {
            msg.template get<"instrumentId">().value(*params.instrumentId);
        } else {
            msg.template get<"instrumentId">().reset();
        }
        if (params.venueId) {
            msg.template get<"venueId">().value(*params.venueId);
        } else {
            msg.template get<"venueId">().reset();
        }
        if (params.serviceId) {
            msg.template get<"serviceId">().value(*params.serviceId);
        } else {
            msg.template get<"serviceId">().reset();
        }
        if (params.instrumentType) {
            msg.template get<"instrumentType">().value(*params.instrumentType);
        } else {
            msg.template get<"instrumentType">().reset();
        }
        if (params.state) {
            msg.template get<"state">().value(*params.state);
        } else {
            msg.template get<"state">().reset();
        }
        msg.template get<"exchangeSymbol">().value(params.exchangeSymbol);
    });
}

auto Client::sendSetLogLevel(service::LogLevel level) -> std::expected<void, std::error_code> {
    return sendFixed<service::SetLogLevel>([level](auto msg) {
        msg.template get<"level">().value(level);
    });
}

void Client::listen(std::chrono::milliseconds timeout) {
    auto const deadline = std::chrono::steady_clock::now() + timeout;
    auto const hasRequests = !awaiting_.empty();

    while (true) {
        auto const counters = consumer_.receive<Responses>([this]<typename M>(M msg) {
            onMessage(msg);
        });

        summary_.received += counters.received;
        summary_.handled += counters.handled;
        summary_.dropped += counters.dropped;

        if (hasRequests && awaiting_.empty()) {
            return;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            summary_.timeout = true;
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
}

template <typename M>
void Client::onMessage(M msg) {
    auto const name = std::string_view{M::sbeMessageName()};
    auto const ownAppId = value_of(producer_.appId());
    auto const appId = messageAppId(msg);
    auto const requestId = messageRequestId(msg);
    auto const isForeign = appId.has_value() && *appId != ownAppId;

    if (isForeign) {
        ++summary_.foreign;
    }

    std::string json;
    try {
        json = turbofw::toJsonString(msg);
    } catch (std::exception const& e) {
        json = std::format("<json error: {}>", e.what());
    }

    if (isForeign) {
        std::println("recv {} [foreign appId={}] {}", name, *appId, json);
    } else if (!appId) {
        std::println("recv {} [spontaneous] {}", name, json);
    } else if (requestId) {
        std::println("recv {} [requestId={}] {}", name, *requestId, json);
    } else {
        std::println("recv {} {}", name, json);
    }

    if constexpr (std::same_as<M, metadata::RequestComplete>) {
        if (isForeign) {
            return;
        }
        printCompletion(msg);
        if (requestId) {
            markComplete(*requestId);
        }
    }
}

void Client::markComplete(std::uint64_t requestId) {
    auto const it = std::find(awaiting_.begin(), awaiting_.end(), requestId);
    if (it == awaiting_.end()) {
        std::println("requestId={} completed (not pending)", requestId);
        return;
    }
    awaiting_.erase(it);
    ++summary_.completed;
}

void Client::printCompletion(metadata::RequestComplete msg) {
    std::println("complete status={} count={}", enumName(msg.get<"status">().value()), msg.get<"count">().value());
}

} // namespace app
