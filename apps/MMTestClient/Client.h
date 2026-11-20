// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <turbofw/Environment.h>
#include <turbofw/ProducerConsumerTraits.h>
#include <turbofw/Types.h>
#include <turbofw/mixin/SBEAppIdStampMixin.h>
#include <turbofw/mixin/SBEReceiveMixin.h>
#include <turbofw/mixin/SBESendMixin.h>
#include <turbofw/mp/TypeList.h>
#include <turbofw/schema/metadata/schema.h>
#include <turbofw/schema/service/schema.h>

namespace app {

using turbofw::Environment;

struct RegisterServiceParams {
    std::string name;
    std::string dataQueue;
    std::string requestQueue;
};

struct RegisterInstrumentParams {
    std::uint16_t venueId{1};
    std::uint16_t serviceId{2};
    turbofw::schema::metadata::InstrumentType instrumentType{turbofw::schema::metadata::InstrumentType::Future};
    turbofw::schema::metadata::InstrumentState state{turbofw::schema::metadata::InstrumentState::Active};
    std::int64_t tickSize{1'000};
    std::int64_t lotSize{1};
    std::int64_t multiplier{100};
    std::uint32_t currency{840};
    turbofw::schema::metadata::Capabilities capabilities;
    std::string exchangeSymbol;
    std::string symbol;
};

struct ServiceRequestParams {
    std::optional<std::uint16_t> serviceId;
    std::string name;
};

struct InstrumentRequestParams {
    std::optional<std::uint32_t> instrumentId;
    std::optional<std::uint16_t> venueId;
    std::optional<std::uint16_t> serviceId;
    std::optional<turbofw::schema::metadata::InstrumentType> instrumentType;
    std::optional<turbofw::schema::metadata::InstrumentState> state;
    std::string exchangeSymbol;
};

struct ListenSummary {
    std::size_t sent{0};
    std::size_t received{0};
    std::size_t handled{0};
    std::size_t dropped{0};
    std::size_t foreign{0};
    std::size_t completed{0};
    bool timeout{false};
};

/// Sends requests to the Metadata service (MotoMoto) over its queues and prints every response as JSON.
/// Not a service: the system definition is only used to open MotoMoto's `cmd` (mpsc) and `data` (multicast)
/// queues; the app needs no service-id and no queue roles of its own.
///
/// The environment must outlive the client (it keeps the queues open).
class Client {
private:
    // clang-format off
    using Producer = turbofw::MakeProducer<turboq::MPSCMessageQueue::Producer,
        turbofw::Mixins<
            turbofw::mixin::SBESendMixin,
            turbofw::mixin::SBEAppIdStampMixin
        >
    >;
    using Consumer = turbofw::MakeConsumer<turboq::MulticastMessageQueue::Consumer,
        turbofw::Mixins<
            turbofw::mixin::SBEReceiveMixin
        >
    >;
    using Responses = turbofw::mp::TypeList<
        turbofw::schema::metadata::RegisterServiceResult,
        turbofw::schema::metadata::RegisterInstrumentResult,
        turbofw::schema::metadata::ServiceDefinition,
        turbofw::schema::metadata::InstrumentDefinition,
        turbofw::schema::metadata::RequestComplete,
        turbofw::schema::service::Heartbeat,
        turbofw::schema::service::SetLogLevelResult
    >;
    // clang-format on

    Producer producer_;
    Consumer consumer_;
    std::uint64_t nextRequestId_;
    std::vector<std::uint64_t> awaiting_;
    ListenSummary summary_;

public:
    Client(Client const&) = delete;
    Client& operator=(Client const&) = delete;

    Client(Environment const& env, std::optional<std::uint32_t> appId, std::uint64_t firstRequestId);

    [[nodiscard]] auto appId() const noexcept -> turbofw::AppId {
        return producer_.appId();
    }

    [[nodiscard]] auto summary() const noexcept -> ListenSummary const& {
        return summary_;
    }

    auto sendRegisterService(RegisterServiceParams const& params) -> std::expected<void, std::error_code>;
    auto sendRegisterInstrument(RegisterInstrumentParams const& params) -> std::expected<void, std::error_code>;
    auto sendServiceRequest(ServiceRequestParams const& params) -> std::expected<void, std::error_code>;
    auto sendInstrumentRequest(InstrumentRequestParams const& params) -> std::expected<void, std::error_code>;
    auto sendSetLogLevel(turbofw::schema::service::LogLevel level) -> std::expected<void, std::error_code>;

    /// Drains responses until the timeout expires or every sent request has been completed
    void listen(std::chrono::milliseconds timeout);

private:
    static auto makeProducer(Environment const& env, std::optional<std::uint32_t> appId) -> Producer;

    template <typename M, typename Fn>
    auto sendVars(typename M::SizeHint hint, Fn&& fn) -> std::expected<void, std::error_code>;

    template <typename M, typename Fn>
    auto sendFixed(Fn&& fn) -> std::expected<void, std::error_code>;

    template <typename M>
    void onMessage(M msg);

    void markComplete(std::uint64_t requestId);
    static void printCompletion(turbofw::schema::metadata::RequestComplete msg);
};

} // namespace app
