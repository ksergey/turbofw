// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "store.cpp"

#include <filesystem>

#include <doctest/doctest.h>

namespace turbofw::testing {

namespace {

auto nextDbPath() -> std::filesystem::path {
    static auto counter = int{0};
    auto const dir = std::filesystem::temp_directory_path() / "turbofw-motomoto-store-test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir / fmt::format("store-{}.db", ++counter);
}

constexpr auto kAnyReportedTs = std::int64_t{1'700'000'000'000};

/// ServiceUpsertRequest carries string_views: owning strings + a request built
/// in the call full-expression (temporaries from fmt::format die on return)
struct ServiceFixture {
    std::string name;
    std::string dataQueue;    ///< empty: "dev/<name>@data"
    std::string requestQueue; ///< empty: "dev/<name>@cmd"
    motomoto::ServiceState state = motomoto::ServiceState::Active;
    std::int64_t reportedTs = kAnyReportedTs;

    explicit ServiceFixture(std::string_view serviceName) : name{serviceName} {
        if (dataQueue.empty()) {
            dataQueue = fmt::format("dev/{}@data", name);
        }
        if (requestQueue.empty()) {
            requestQueue = fmt::format("dev/{}@cmd", name);
        }
    }

    /// view-form request; the strings live in the fixture, the views are consumed in the call
    [[nodiscard]] auto asRequest() const -> motomoto::ServiceUpsertRequest {
        return {.name = name,
            .dataQueue = dataQueue,
            .requestQueue = requestQueue,
            .state = state,
            .reportedTs = reportedTs};
    }
};

// helper making the request IN the call expression (see ServiceFixture)
#define SERVICE_REQUEST(fix)                                                                                           \
    motomoto::ServiceUpsertRequest {                                                                                   \
        .name = (fix).name, .dataQueue = (fix).dataQueue, .requestQueue = (fix).requestQueue, .state = (fix).state,    \
        .reportedTs = (fix).reportedTs                                                                                 \
    }

struct InstrumentFixture {
    motomoto::InstrumentUpsertRequest request{};

    explicit InstrumentFixture(std::uint16_t serviceId = 1) {
        request.serviceId = serviceId;
        request.venueId = 7;
        request.exchangeSymbol = "AAPL";
        request.instrumentType = motomoto::InstrumentType::Spot;
        request.state = motomoto::InstrumentState::Active;
        request.capabilities = 0x1 | 0x2;
        request.reportedTs = kAnyReportedTs;
    }
};

} // namespace

TEST_SUITE("motomoto::Store") {
    TEST_CASE("open creates the schema and reopens idempotently") {
        auto const path = nextDbPath();
        auto store = motomoto::Store::open(path);
        REQUIRE_MESSAGE(store, store.error());

        auto const service = store->upsertService(SERVICE_REQUEST(ServiceFixture{"md"}));
        REQUIRE_MESSAGE(service, service.error());
        CHECK_EQ(service->status, motomoto::RegisterStatus::Created);
        CHECK_EQ(service->serviceId, 1);
        CHECK_EQ(service->version, 1);
        store = motomoto::Store::open(path); // replaces the handle (tests the move), reopen keeps everything
        REQUIRE_MESSAGE(store, store.error());

        // reopen: schema exists, the data is still there, ids keep counting up
        auto reopened = motomoto::Store::open(path);
        REQUIRE_MESSAGE(reopened, reopened.error());
        auto const rows = reopened->queryServices({.serviceId = std::nullopt, .state = std::nullopt, .name = ""});
        REQUIRE(rows);
        REQUIRE_EQ(rows->size(), 1);
        CHECK_EQ(rows->front().serviceId, 1);

        auto const second = reopened->upsertService(SERVICE_REQUEST(ServiceFixture{"md2"}));
        REQUIRE_MESSAGE(second, second.error());
        CHECK_EQ(second->status, motomoto::RegisterStatus::Created);
        CHECK_EQ(second->serviceId, 2);
    }

    TEST_CASE("service upsert: Created, Unchanged, Updated") {
        auto store = motomoto::Store::open(nextDbPath());
        REQUIRE_MESSAGE(store, store.error());

        auto const created = store->upsertService(SERVICE_REQUEST(ServiceFixture{"md"}));
        REQUIRE_MESSAGE(created, created.error());
        CHECK_EQ(created->status, motomoto::RegisterStatus::Created);
        CHECK_EQ(created->version, 1);

        // same attributes, only reportedTs refreshes
        auto unchanged = ServiceFixture{"md"};
        unchanged.reportedTs = kAnyReportedTs + 1;
        auto const same = store->upsertService(SERVICE_REQUEST(unchanged));
        REQUIRE_MESSAGE(same, same.error());
        CHECK_EQ(same->status, motomoto::RegisterStatus::Unchanged);
        CHECK_EQ(same->serviceId, created->serviceId);
        CHECK_EQ(same->version, 1);

        // a state change is an update
        auto stateChange = ServiceFixture{"md"};
        stateChange.state = motomoto::ServiceState::Inactive;
        stateChange.reportedTs = kAnyReportedTs + 2;
        auto const changed = store->upsertService(SERVICE_REQUEST(stateChange));
        REQUIRE_MESSAGE(changed, changed.error());
        CHECK_EQ(changed->status, motomoto::RegisterStatus::Updated);
        CHECK_EQ(changed->version, 2);

        auto const rows = store->queryServices({.serviceId = std::nullopt, .state = std::nullopt, .name = ""});
        REQUIRE(rows);
        REQUIRE_EQ(rows->size(), 1);
        CHECK_EQ(rows->front().dataQueue, "dev/md@data");
        CHECK_EQ(rows->front().state, motomoto::ServiceState::Inactive);
        CHECK_EQ(rows->front().reportedTs, kAnyReportedTs + 2);
    }

    TEST_CASE("service query filters combine with AND") {
        auto store = motomoto::Store::open(nextDbPath());
        REQUIRE_MESSAGE(store, store.error());

        REQUIRE_MESSAGE(store->upsertService(SERVICE_REQUEST(ServiceFixture{"a"})), "upsert failed");
        auto b = ServiceFixture{"b"};
        b.state = motomoto::ServiceState::Inactive;
        REQUIRE_MESSAGE(store->upsertService(SERVICE_REQUEST(b)), "upsert failed");

        CHECK_EQ(store->queryServices({.serviceId = 2, .state = std::nullopt, .name = ""}).value().size(), 1);
        CHECK_EQ(
            store->queryServices({.serviceId = std::nullopt, .state = motomoto::ServiceState::Inactive, .name = ""})
                .value()
                .size(),
            1);
        CHECK_EQ(
            store->queryServices({.serviceId = std::nullopt, .state = std::nullopt, .name = "a"}).value().size(), 1);
        CHECK_EQ(store->queryServices({.serviceId = 1, .state = std::nullopt, .name = "b"}).value().size(), 0);
        CHECK_EQ(store->queryServices({.serviceId = 2, .state = motomoto::ServiceState::Inactive, .name = ""})
                     .value()
                     .size(),
            1);
    }

    TEST_CASE("instrument upsert requires a registered service") {
        auto store = motomoto::Store::open(nextDbPath());
        REQUIRE_MESSAGE(store, store.error());

        auto fixture = InstrumentFixture{42}; // no service 42 registered
        auto const result = store->upsertInstrument(fixture.request);
        CHECK_EQ(result->status, motomoto::RegisterStatus::UnknownService);
        CHECK_FALSE(result->instrumentId.has_value());
    }

    TEST_CASE("instrument upsert: Created, Unchanged, Updated, Rejected") {
        auto store = motomoto::Store::open(nextDbPath());
        REQUIRE_MESSAGE(store, store.error());
        REQUIRE_MESSAGE(store->upsertService(SERVICE_REQUEST(ServiceFixture{"md"})), "upsert failed");

        auto fixture = InstrumentFixture{};
        fixture.request.symbol = ""; // empty: the store generates the symbol
        auto const created = store->upsertInstrument(fixture.request);
        REQUIRE_MESSAGE(created, created.error());
        CHECK_EQ(created->status, motomoto::RegisterStatus::Created);
        CHECK_EQ(created->instrumentId, 1);
        CHECK_EQ(created->version, 1);

        auto const rows = store->queryInstruments({.instrumentId = std::nullopt,
            .venueId = 7,
            .serviceId = 1,
            .instrumentType = std::nullopt,
            .state = std::nullopt,
            .exchangeSymbol = ""});
        REQUIRE(rows);
        REQUIRE_EQ(rows->size(), 1);
        CHECK_EQ(rows->front().symbol, "1:7:AAPL"); // "<serviceId>:<venueId>:<exchangeSymbol>"
        CHECK_EQ(rows->front().instrumentId, 1);

        // same attributes: only reportedTs refreshes
        auto unchanged = fixture.request;
        unchanged.reportedTs = kAnyReportedTs + 1;
        auto const same = store->upsertInstrument(unchanged);
        CHECK_EQ(same->status, motomoto::RegisterStatus::Unchanged);
        CHECK_EQ(same->version, 1);

        // a state change is an update; the stored symbol stays even if the request carries another
        auto updated = unchanged;
        updated.state = motomoto::InstrumentState::Suspended;
        updated.symbol = "renamed";
        auto const changed = store->upsertInstrument(updated);
        CHECK_EQ(changed->status, motomoto::RegisterStatus::Updated);
        CHECK_EQ(changed->version, 2);

        auto const rows2 = store->queryInstruments({.instrumentId = std::nullopt,
            .venueId = 7,
            .serviceId = 1,
            .instrumentType = std::nullopt,
            .state = std::nullopt,
            .exchangeSymbol = ""});
        REQUIRE(rows2);
        CHECK_EQ(rows2->front().symbol, "1:7:AAPL");
        CHECK_EQ(rows2->front().state, motomoto::InstrumentState::Suspended);
        CHECK_EQ(rows2->front().reportedTs, kAnyReportedTs + 1);

        // instrumentType is immutable for a natural key
        auto rejected = unchanged;
        rejected.instrumentType = motomoto::InstrumentType::Future;
        auto const conflict = store->upsertInstrument(rejected);
        CHECK_EQ(conflict->status, motomoto::RegisterStatus::Rejected);
        CHECK(conflict->instrumentId.has_value());
        CHECK_EQ(conflict->version, 2); // unchanged since the update
    }

    TEST_CASE("the same venue instrument through two services are two instruments") {
        auto store = motomoto::Store::open(nextDbPath());
        REQUIRE_MESSAGE(store, store.error());
        REQUIRE_MESSAGE(store->upsertService(SERVICE_REQUEST(ServiceFixture{"md1"})), "upsert failed");
        REQUIRE_MESSAGE(store->upsertService(SERVICE_REQUEST(ServiceFixture{"md2"})), "upsert failed");

        auto viaFirst = InstrumentFixture{1};
        auto const first = store->upsertInstrument(viaFirst.request);
        CHECK_EQ(first->status, motomoto::RegisterStatus::Created);

        auto viaSecond = InstrumentFixture{2};
        auto const second = store->upsertInstrument(viaSecond.request);
        CHECK_EQ(second->status, motomoto::RegisterStatus::Created);
        CHECK_NE(*first->instrumentId, *second->instrumentId);
    }

    TEST_CASE("instrument query filters combine with AND") {
        auto store = motomoto::Store::open(nextDbPath());
        REQUIRE_MESSAGE(store, store.error());
        REQUIRE_MESSAGE(store->upsertService(SERVICE_REQUEST(ServiceFixture{"md"})), "upsert failed");

        auto spot = InstrumentFixture{};
        spot.request.exchangeSymbol = "AAPL";
        spot.request.instrumentType = motomoto::InstrumentType::Spot;
        REQUIRE_MESSAGE(store->upsertInstrument(spot.request), "upsert failed");

        auto future = spot.request;
        future.exchangeSymbol = "ESZ6";
        future.instrumentType = motomoto::InstrumentType::Future;
        REQUIRE_MESSAGE(store->upsertInstrument(future), "upsert failed");

        CHECK_EQ(store
                     ->queryInstruments({.instrumentId = std::nullopt,
                         .venueId = 7,
                         .serviceId = std::nullopt,
                         .instrumentType = std::nullopt,
                         .state = std::nullopt,
                         .exchangeSymbol = ""})
                     .value()
                     .size(),
            2);
        CHECK_EQ(store
                     ->queryInstruments({.instrumentId = std::nullopt,
                         .venueId = 7,
                         .serviceId = 1,
                         .instrumentType = std::nullopt,
                         .state = std::nullopt,
                         .exchangeSymbol = ""})
                     .value()
                     .size(),
            2);
        CHECK_EQ(store
                     ->queryInstruments({.instrumentId = std::nullopt,
                         .venueId = 7,
                         .serviceId = 1,
                         .instrumentType = std::nullopt,
                         .state = std::nullopt,
                         .exchangeSymbol = "AAPL"})
                     .value()
                     .size(),
            1);
        CHECK_EQ(store
                     ->queryInstruments({.instrumentId = std::nullopt,
                         .venueId = 7,
                         .serviceId = 1,
                         .instrumentType = motomoto::InstrumentType::Future,
                         .state = std::nullopt,
                         .exchangeSymbol = ""})
                     .value()
                     .size(),
            1);
        CHECK_EQ(store
                     ->queryInstruments({.instrumentId = 2,
                         .venueId = std::nullopt,
                         .serviceId = std::nullopt,
                         .instrumentType = std::nullopt,
                         .state = std::nullopt,
                         .exchangeSymbol = ""})
                     .value()
                     .size(),
            1);
        CHECK_EQ(store
                     ->queryInstruments({.instrumentId = std::nullopt,
                         .venueId = 8,
                         .serviceId = std::nullopt,
                         .instrumentType = std::nullopt,
                         .state = std::nullopt,
                         .exchangeSymbol = ""})
                     .value()
                     .size(),
            0);

        // a non-empty exchangeSymbol requires venueId
        auto const error = store->queryInstruments({.instrumentId = std::nullopt,
            .venueId = std::nullopt,
            .serviceId = std::nullopt,
            .instrumentType = std::nullopt,
            .state = std::nullopt,
            .exchangeSymbol = "AAPL"});
        REQUIRE_FALSE(error);
        CHECK_NE(error.error().find("exchangeSymbol requires venueId"), std::string::npos);
    }

    TEST_CASE("instrument counters and rows survive a reopen") {
        auto const path = nextDbPath();
        {
            auto store = motomoto::Store::open(path);
            REQUIRE_MESSAGE(store, store.error());
            REQUIRE_MESSAGE(store->upsertService(SERVICE_REQUEST(ServiceFixture{"md"})), "upsert failed");
            REQUIRE_MESSAGE(store->upsertInstrument(InstrumentFixture{}.request), "upsert failed");
        }
        {
            auto store = motomoto::Store::open(path);
            REQUIRE_MESSAGE(store, store.error());
            auto const second = store->upsertInstrument(InstrumentFixture{}.request);
            // same natural key: Unchanged, not a new id
            CHECK_EQ(second->status, motomoto::RegisterStatus::Unchanged);
            CHECK_EQ(second->instrumentId, 1);

            auto other = InstrumentFixture{};
            other.request.exchangeSymbol = "MSFT";
            auto const created = store->upsertInstrument(other.request);
            CHECK_EQ(created->status, motomoto::RegisterStatus::Created);
            CHECK_EQ(created->instrumentId, 2); // the counter kept counting across the restart
        }
    }
}

} // namespace turbofw::testing
