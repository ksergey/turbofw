// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "store.h"

#include <functional>
#include <limits>
#include <utility>

#include <sqlite3.h>

#include <fmt/format.h>

// the TRY macro below relies on a GNU statement expression
#pragma clang diagnostic ignored "-Wgnu-statement-expression-from-macro-expansion"

namespace motomoto {
namespace {

/// Early-return the error of an expected-returning call: `auto x = TRY(f());`
/// (GNU statement expression, compiled by GCC and Clang alike)
#define TRY(expr)                                                                                                      \
    ({                                                                                                                 \
        auto&& turbofwStoreResult = (expr);                                                                            \
        if (!turbofwStoreResult) {                                                                                     \
            return std::unexpected{std::move(turbofwStoreResult).error()};                                             \
        }                                                                                                              \
        std::move(turbofwStoreResult).value();                                                                         \
    })

[[nodiscard]] auto errorOf(sqlite3* db, std::string_view context) -> std::string {
    return fmt::format("{}: {}", context, sqlite3_errmsg(db));
}

/// RAII prepared statement
class Statement {
public:
    Statement() = default;

    Statement(Statement&& other) noexcept : stmt_{std::exchange(other.stmt_, nullptr)} {}

    auto operator=(Statement&& other) noexcept -> Statement& {
        if (stmt_) {
            sqlite3_finalize(stmt_);
        }
        stmt_ = std::exchange(other.stmt_, nullptr);
        return *this;
    }

    ~Statement() {
        if (stmt_) {
            sqlite3_finalize(stmt_);
        }
    }

    Statement(Statement const&) = delete;
    auto operator=(Statement const&) = delete;

    [[nodiscard]] static auto prepare(sqlite3* db, char const* sql) -> std::expected<Statement, std::string> {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return std::unexpected{errorOf(db, "can't prepare statement")};
        }
        auto statement = Statement{};
        statement.stmt_ = stmt;
        return statement;
    }

    /// SQLITE_ROW or SQLITE_DONE, error otherwise
    [[nodiscard]] auto step() -> std::expected<int, std::string> {
        auto const rc = sqlite3_step(stmt_);
        if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
            return std::unexpected{errorOf(sqlite3_db_handle(stmt_), "can't step statement")};
        }
        return rc;
    }

    [[nodiscard]] auto reset() -> std::expected<void, std::string> {
        if (sqlite3_reset(stmt_) != SQLITE_OK || sqlite3_clear_bindings(stmt_) != SQLITE_OK) {
            return std::unexpected{errorOf(sqlite3_db_handle(stmt_), "can't reset statement")};
        }
        return {};
    }

    [[nodiscard]] auto bindInt64(int index, std::int64_t value) -> std::expected<void, std::string> {
        if (sqlite3_bind_int64(stmt_, index, value) != SQLITE_OK) {
            return std::unexpected{errorOf(sqlite3_db_handle(stmt_), "can't bind")};
        }
        return {};
    }

    [[nodiscard]] auto bindInt64OrNull(int index, std::optional<std::int64_t> value)
        -> std::expected<void, std::string> {
        if (!value.has_value()) {
            if (sqlite3_bind_null(stmt_, index) != SQLITE_OK) {
                return std::unexpected{errorOf(sqlite3_db_handle(stmt_), "can't bind")};
            }
            return {};
        }
        return bindInt64(index, *value);
    }

    [[nodiscard]] auto bindText(int index, std::string_view value) -> std::expected<void, std::string> {
        // SQLITE_TRANSIENT: sqlite makes its own copy, the view may die before the step
        if (sqlite3_bind_text(stmt_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT) !=
            SQLITE_OK) {
            return std::unexpected{errorOf(sqlite3_db_handle(stmt_), "can't bind")};
        }
        return {};
    }

    [[nodiscard]] auto columnInt64(int column) const -> std::int64_t {
        return sqlite3_column_int64(stmt_, column);
    }

    [[nodiscard]] auto columnInt64OrNull(int column) const -> std::optional<std::int64_t> {
        if (sqlite3_column_type(stmt_, column) == SQLITE_NULL) {
            return std::nullopt;
        }
        return sqlite3_column_int64(stmt_, column);
    }

    [[nodiscard]] auto columnText(int column) const -> std::string {
        auto const* text = reinterpret_cast<char const*>(sqlite3_column_text(stmt_, column));
        auto const size = static_cast<std::size_t>(sqlite3_column_bytes(stmt_, column));
        return std::string{text != nullptr ? text : "", size};
    }

private:
    sqlite3_stmt* stmt_ = nullptr;
};

/// RAII transaction: rolls back unless commit() ran
class Transaction {
public:
    [[nodiscard]] static auto begin(sqlite3* db) -> std::expected<Transaction, std::string> {
        char* errorMessage = nullptr;
        if (sqlite3_exec(db, "BEGIN IMMEDIATE", nullptr, nullptr, &errorMessage) != SQLITE_OK) {
            auto const message = std::string{errorMessage != nullptr ? errorMessage : sqlite3_errmsg(db)};
            sqlite3_free(errorMessage);
            return std::unexpected{fmt::format("can't begin transaction: {}", message)};
        }
        return Transaction{db};
    }

    Transaction(Transaction&& other) noexcept
        : db_{std::exchange(other.db_, nullptr)}, done_{std::exchange(other.done_, true)} {}

    auto operator=(Transaction&&) = delete;

    ~Transaction() {
        if (db_ != nullptr && !done_) {
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        }
    }

    Transaction(Transaction const&) = delete;
    auto operator=(Transaction const&) = delete;

    [[nodiscard]] auto commit() -> std::expected<void, std::string> {
        char* errorMessage = nullptr;
        if (sqlite3_exec(db_, "COMMIT", nullptr, nullptr, &errorMessage) != SQLITE_OK) {
            auto const message = std::string{errorMessage != nullptr ? errorMessage : sqlite3_errmsg(db_)};
            sqlite3_free(errorMessage);
            return std::unexpected{fmt::format("can't commit transaction: {}", message)};
        }
        done_ = true;
        return {};
    }

    void rollback() noexcept {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        done_ = true;
    }

private:
    explicit Transaction(sqlite3* db) noexcept : db_{db} {}

    sqlite3* db_ = nullptr;
    bool done_ = false;
};

/// Persistent monotonic counter, one row per key; ids are never reused
[[nodiscard]] auto nextId(sqlite3* db, std::string_view key) -> std::expected<std::int64_t, std::string> {
    // clang-format off
    constexpr auto kSql =
        "INSERT INTO meta(key, value) VALUES(?1, 1) "
        "ON CONFLICT(key) DO UPDATE SET value = value + 1 "
        "RETURNING value";
    // clang-format on

    auto statement = TRY(Statement::prepare(db, kSql));
    TRY(statement.bindText(1, key));
    auto const stepped = TRY(statement.step());
    if (stepped != SQLITE_ROW) {
        return std::unexpected{fmt::format("counter \"{}\" returned no value", key)};
    }
    return statement.columnInt64(0);
}

constexpr auto kSchemaSql = "CREATE TABLE IF NOT EXISTS meta (\n"
                            "    key   TEXT PRIMARY KEY,\n"
                            "    value INTEGER NOT NULL) WITHOUT ROWID;\n"
                            "CREATE TABLE IF NOT EXISTS services (\n"
                            "    name          TEXT PRIMARY KEY,\n"
                            "    service_id    INTEGER NOT NULL UNIQUE,\n"
                            "    version       INTEGER NOT NULL,\n"
                            "    state         INTEGER NOT NULL,\n"
                            "    reported_ts   INTEGER NOT NULL,\n"
                            "    data_queue    TEXT NOT NULL,\n"
                            "    request_queue TEXT NOT NULL) WITHOUT ROWID;\n"
                            "CREATE TABLE IF NOT EXISTS instruments (\n"
                            "    service_id      INTEGER NOT NULL,\n"
                            "    venue_id        INTEGER NOT NULL,\n"
                            "    exchange_symbol TEXT NOT NULL,\n"
                            "    instrument_id   INTEGER NOT NULL UNIQUE,\n"
                            "    symbol          TEXT NOT NULL,\n"
                            "    instrument_type INTEGER NOT NULL,\n"
                            "    state           INTEGER NOT NULL,\n"
                            "    tick_size       INTEGER,\n"
                            "    lot_size        INTEGER,\n"
                            "    multiplier      INTEGER,\n"
                            "    expiry          INTEGER,\n"
                            "    currency        INTEGER,\n"
                            "    capabilities    INTEGER NOT NULL,\n"
                            "    version         INTEGER NOT NULL,\n"
                            "    reported_ts     INTEGER NOT NULL,\n"
                            "    PRIMARY KEY (service_id, venue_id, exchange_symbol)) WITHOUT ROWID;\n";

} // namespace

auto Store::open(std::filesystem::path const& path) -> std::expected<Store, std::string> {
    sqlite3* db = nullptr;
    auto const rc = sqlite3_open_v2(path.string().c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) {
        auto const message = db != nullptr ? std::string{sqlite3_errmsg(db)} : std::string{"unknown error"};
        if (db != nullptr) {
            sqlite3_close(db);
        }
        return std::unexpected{fmt::format("can't open \"{}\": {}", path.string(), message)};
    }
    auto store = Store{db};

    // WAL: concurrent readers (inspection with the sqlite3 cli) don't block the writer;
    // NORMAL: durability on process crash without a per-transaction fsync
    for (auto const* pragma :
        {"PRAGMA journal_mode = WAL", "PRAGMA synchronous = NORMAL", "PRAGMA busy_timeout = 5000"}) {
        auto statement = TRY(Statement::prepare(db, pragma));
        auto const stepped = TRY(statement.step());
        if (stepped != SQLITE_ROW && stepped != SQLITE_DONE) {
            return std::unexpected{fmt::format("can't apply \"{}\"", pragma)};
        }
    }

    char* errorMessage = nullptr;
    if (sqlite3_exec(db, kSchemaSql, nullptr, nullptr, &errorMessage) != SQLITE_OK) {
        auto const message = std::string{errorMessage != nullptr ? errorMessage : sqlite3_errmsg(db)};
        sqlite3_free(errorMessage);
        return std::unexpected{fmt::format("can't ensure schema: {}", message)};
    }

    return store;
}

Store::Store(sqlite3* db) noexcept : db_{db} {}

Store::Store(Store&& other) noexcept : db_{std::exchange(other.db_, nullptr)} {}

auto Store::operator=(Store&& other) noexcept -> Store& {
    if (db_ != nullptr) {
        sqlite3_close(db_);
    }
    db_ = std::exchange(other.db_, nullptr);
    return *this;
}

Store::~Store() {
    if (db_ != nullptr) {
        sqlite3_close(db_);
    }
}

auto Store::upsertService(ServiceUpsertRequest const& request) -> std::expected<ServiceUpsert, std::string> {
    auto transaction = TRY(Transaction::begin(db_));

    auto select = TRY(Statement::prepare(
        db_, "SELECT service_id, version, state, data_queue, request_queue FROM services WHERE name = ?1"));
    TRY(select.bindText(1, request.name));
    auto result = ServiceUpsert{};
    if (TRY(select.step()) == SQLITE_ROW) {
        auto const storedId = static_cast<std::uint16_t>(select.columnInt64(0));
        auto const storedVersion = static_cast<std::uint32_t>(select.columnInt64(1));
        auto const storedState = static_cast<ServiceState>(select.columnInt64(2));
        auto const storedDataQueue = select.columnText(3);
        auto const storedRequestQueue = select.columnText(4);

        if (storedState == request.state && storedDataQueue == request.dataQueue &&
            storedRequestQueue == request.requestQueue) {
            auto update = TRY(Statement::prepare(db_, "UPDATE services SET reported_ts = ?1 WHERE name = ?2"));
            TRY(update.bindInt64(1, request.reportedTs));
            TRY(update.bindText(2, request.name));
            TRY(update.step());
            result =
                ServiceUpsert{.status = RegisterStatus::Unchanged, .serviceId = storedId, .version = storedVersion};
        } else {
            auto update = TRY(Statement::prepare(db_,
                "UPDATE services SET data_queue = ?1, request_queue = ?2, state = ?3, reported_ts = ?4, "
                "version = version + 1 WHERE name = ?5"));
            TRY(update.bindText(1, request.dataQueue));
            TRY(update.bindText(2, request.requestQueue));
            TRY(update.bindInt64(3, static_cast<std::int64_t>(request.state)));
            TRY(update.bindInt64(4, request.reportedTs));
            TRY(update.bindText(5, request.name));
            TRY(update.step());
            result =
                ServiceUpsert{.status = RegisterStatus::Updated, .serviceId = storedId, .version = storedVersion + 1};
        }
    } else {
        auto const id = TRY(nextId(db_, "next_service_id"));
        if (id < 1 || id > std::numeric_limits<std::uint16_t>::max()) {
            return std::unexpected{fmt::format("service id {} is out of range", id)};
        }
        auto insert = TRY(Statement::prepare(db_,
            "INSERT INTO services(name, service_id, version, state, reported_ts, data_queue, request_queue) "
            "VALUES(?1, ?2, 1, ?3, ?4, ?5, ?6)"));
        TRY(insert.bindText(1, request.name));
        TRY(insert.bindInt64(2, id));
        TRY(insert.bindInt64(3, static_cast<std::int64_t>(request.state)));
        TRY(insert.bindInt64(4, request.reportedTs));
        TRY(insert.bindText(5, request.dataQueue));
        TRY(insert.bindText(6, request.requestQueue));
        TRY(insert.step());
        result =
            ServiceUpsert{.status = RegisterStatus::Created, .serviceId = static_cast<std::uint16_t>(id), .version = 1};
    }

    TRY(transaction.commit());
    return result;
}

auto Store::upsertInstrument(InstrumentUpsertRequest const& request) -> std::expected<InstrumentUpsert, std::string> {
    auto transaction = TRY(Transaction::begin(db_));

    { // the instrument belongs to a registered service
        auto select = TRY(Statement::prepare(db_, "SELECT 1 FROM services WHERE service_id = ?1"));
        TRY(select.bindInt64(1, request.serviceId));
        if (TRY(select.step()) != SQLITE_ROW) {
            transaction.rollback();
            return InstrumentUpsert{
                .status = RegisterStatus::UnknownService, .instrumentId = std::nullopt, .version = 0};
        }
    }

    // clang-format off
    auto select = TRY(Statement::prepare(db_,
        "SELECT instrument_id, symbol, instrument_type, state, tick_size, lot_size, multiplier, expiry, "
        "       currency, capabilities, version "
        "FROM instruments WHERE service_id = ?1 AND venue_id = ?2 AND exchange_symbol = ?3"));
    // clang-format on
    TRY(select.bindInt64(1, request.serviceId));
    TRY(select.bindInt64(2, request.venueId));
    TRY(select.bindText(3, request.exchangeSymbol));

    auto result = InstrumentUpsert{};
    if (TRY(select.step()) == SQLITE_ROW) {
        auto const storedId = static_cast<std::uint32_t>(select.columnInt64(0));
        auto const storedType = static_cast<InstrumentType>(select.columnInt64(2));
        auto const storedState = static_cast<InstrumentState>(select.columnInt64(3));
        auto const storedTickSize = select.columnInt64OrNull(4);
        auto const storedLotSize = select.columnInt64OrNull(5);
        auto const storedMultiplier = select.columnInt64OrNull(6);
        auto const storedExpiry = select.columnInt64OrNull(7);
        auto const storedCurrency = select.columnInt64OrNull(8);
        auto const storedCapabilities = static_cast<std::uint16_t>(select.columnInt64(9));
        auto const storedVersion = static_cast<std::uint32_t>(select.columnInt64(10));

        if (storedType != request.instrumentType) { // immutable for a natural key
            transaction.rollback();
            return InstrumentUpsert{
                .status = RegisterStatus::Rejected, .instrumentId = storedId, .version = storedVersion};
        }

        auto const unchanged = storedState == request.state && storedTickSize == request.tickSize &&
                               storedLotSize == request.lotSize && storedMultiplier == request.multiplier &&
                               storedExpiry == request.expiry && storedCurrency == request.currency &&
                               storedCapabilities == request.capabilities;
        if (unchanged) {
            auto update =
                TRY(Statement::prepare(db_, "UPDATE instruments SET reported_ts = ?1 WHERE instrument_id = ?2"));
            TRY(update.bindInt64(1, request.reportedTs));
            TRY(update.bindInt64(2, storedId));
            TRY(update.step());
            result = InstrumentUpsert{
                .status = RegisterStatus::Unchanged, .instrumentId = storedId, .version = storedVersion};
        } else {
            // clang-format off
            auto update = TRY(Statement::prepare(db_,
                "UPDATE instruments SET state = ?1, tick_size = ?2, lot_size = ?3, multiplier = ?4, expiry = ?5, "
                "       currency = ?6, capabilities = ?7, reported_ts = ?8, version = version + 1 "
                "WHERE instrument_id = ?9"));
            // clang-format on
            TRY(update.bindInt64(1, static_cast<std::int64_t>(request.state)));
            TRY(update.bindInt64OrNull(2, request.tickSize));
            TRY(update.bindInt64OrNull(3, request.lotSize));
            TRY(update.bindInt64OrNull(4, request.multiplier));
            TRY(update.bindInt64OrNull(5, request.expiry));
            TRY(update.bindInt64OrNull(6, request.currency));
            TRY(update.bindInt64(7, request.capabilities));
            TRY(update.bindInt64(8, request.reportedTs));
            TRY(update.bindInt64(9, storedId));
            TRY(update.step());
            result = InstrumentUpsert{
                .status = RegisterStatus::Updated, .instrumentId = storedId, .version = storedVersion + 1};
        }
    } else {
        auto const id = TRY(nextId(db_, "next_instrument_id"));
        if (id < 1 || id > std::numeric_limits<std::uint32_t>::max()) {
            return std::unexpected{fmt::format("instrument id {} is out of range", id)};
        }
        auto const symbol = !request.symbol.empty()
                                ? std::string{request.symbol}
                                : fmt::format("{}:{}:{}", request.serviceId, request.venueId, request.exchangeSymbol);
        // clang-format off
        auto insert = TRY(Statement::prepare(db_,
            "INSERT INTO instruments(service_id, venue_id, exchange_symbol, instrument_id, symbol, instrument_type, "
            "    state, tick_size, lot_size, multiplier, expiry, currency, capabilities, version, reported_ts) "
            "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, 1, ?14)"));
        // clang-format on
        TRY(insert.bindInt64(1, request.serviceId));
        TRY(insert.bindInt64(2, request.venueId));
        TRY(insert.bindText(3, request.exchangeSymbol));
        TRY(insert.bindInt64(4, id));
        TRY(insert.bindText(5, symbol));
        TRY(insert.bindInt64(6, static_cast<std::int64_t>(request.instrumentType)));
        TRY(insert.bindInt64(7, static_cast<std::int64_t>(request.state)));
        TRY(insert.bindInt64OrNull(8, request.tickSize));
        TRY(insert.bindInt64OrNull(9, request.lotSize));
        TRY(insert.bindInt64OrNull(10, request.multiplier));
        TRY(insert.bindInt64OrNull(11, request.expiry));
        TRY(insert.bindInt64OrNull(12, request.currency));
        TRY(insert.bindInt64(13, request.capabilities));
        TRY(insert.bindInt64(14, request.reportedTs));
        TRY(insert.step());
        result = InstrumentUpsert{
            .status = RegisterStatus::Created, .instrumentId = static_cast<std::uint32_t>(id), .version = 1};
    }

    TRY(transaction.commit());
    return result;
}

auto Store::queryServices(ServiceQuery const& query) -> std::expected<std::vector<ServiceRow>, std::string> {
    std::string where;
    auto binders = std::vector<std::function<std::expected<void, std::string>(Statement&, int)>>{};
    if (query.serviceId.has_value()) {
        where += (where.empty() ? " WHERE" : " AND") + std::string{" service_id = ?"};
        binders.push_back([value = *query.serviceId](Statement& s, int i) {
            return s.bindInt64(i, value);
        });
    }
    if (query.state.has_value()) {
        where += (where.empty() ? " WHERE" : " AND") + std::string{" state = ?"};
        binders.push_back([value = static_cast<std::int64_t>(*query.state)](Statement& s, int i) {
            return s.bindInt64(i, value);
        });
    }
    if (!query.name.empty()) {
        where += (where.empty() ? " WHERE" : " AND") + std::string{" name = ?"};
        binders.push_back([value = std::string{query.name}](Statement& s, int i) {
            return s.bindText(i, value);
        });
    }

    auto const sql = fmt::format(
        "SELECT name, service_id, version, reported_ts, state, data_queue, request_queue FROM services{}", where);
    auto statement = TRY(Statement::prepare(db_, sql.c_str()));
    for (auto index = 0; auto const& binder : binders) {
        if (auto bound = binder(statement, ++index); !bound) {
            return std::unexpected{std::move(bound).error()};
        }
    }

    auto rows = std::vector<ServiceRow>{};
    while (TRY(statement.step()) == SQLITE_ROW) {
        rows.push_back(ServiceRow{
            .name = statement.columnText(0),
            .serviceId = static_cast<std::uint16_t>(statement.columnInt64(1)),
            .version = static_cast<std::uint32_t>(statement.columnInt64(2)),
            .reportedTs = statement.columnInt64(3),
            .state = static_cast<ServiceState>(statement.columnInt64(4)),
            .dataQueue = statement.columnText(5),
            .requestQueue = statement.columnText(6),
        });
    }
    return rows;
}

auto Store::queryInstruments(InstrumentQuery const& query) -> std::expected<std::vector<InstrumentRow>, std::string> {
    if (!query.exchangeSymbol.empty() && !query.venueId.has_value()) {
        return std::unexpected{"a non-empty exchangeSymbol requires venueId"};
    }

    std::string where;
    auto binders = std::vector<std::function<std::expected<void, std::string>(Statement&, int)>>{};
    if (query.instrumentId.has_value()) {
        where += (where.empty() ? " WHERE" : " AND") + std::string{" instrument_id = ?"};
        binders.push_back([value = *query.instrumentId](Statement& s, int i) {
            return s.bindInt64(i, value);
        });
    }
    if (query.venueId.has_value()) {
        where += (where.empty() ? " WHERE" : " AND") + std::string{" venue_id = ?"};
        binders.push_back([value = *query.venueId](Statement& s, int i) {
            return s.bindInt64(i, value);
        });
    }
    if (query.serviceId.has_value()) {
        where += (where.empty() ? " WHERE" : " AND") + std::string{" service_id = ?"};
        binders.push_back([value = *query.serviceId](Statement& s, int i) {
            return s.bindInt64(i, value);
        });
    }
    if (query.instrumentType.has_value()) {
        where += (where.empty() ? " WHERE" : " AND") + std::string{" instrument_type = ?"};
        binders.push_back([value = static_cast<std::int64_t>(*query.instrumentType)](Statement& s, int i) {
            return s.bindInt64(i, value);
        });
    }
    if (query.state.has_value()) {
        where += (where.empty() ? " WHERE" : " AND") + std::string{" state = ?"};
        binders.push_back([value = static_cast<std::int64_t>(*query.state)](Statement& s, int i) {
            return s.bindInt64(i, value);
        });
    }
    if (!query.exchangeSymbol.empty()) {
        where += (where.empty() ? " WHERE" : " AND") + std::string{" exchange_symbol = ?"};
        binders.push_back([value = std::string{query.exchangeSymbol}](Statement& s, int i) {
            return s.bindText(i, value);
        });
    }

    // clang-format off
    auto const sql = fmt::format(
        "SELECT instrument_id, exchange_symbol, symbol, venue_id, service_id, instrument_type, state, tick_size, "
        "       lot_size, multiplier, expiry, currency, capabilities, version, reported_ts "
        "FROM instruments{}", where);
    // clang-format on
    auto statement = TRY(Statement::prepare(db_, sql.c_str()));
    for (auto index = 0; auto const& binder : binders) {
        if (auto bound = binder(statement, ++index); !bound) {
            return std::unexpected{std::move(bound).error()};
        }
    }

    auto rows = std::vector<InstrumentRow>{};
    while (TRY(statement.step()) == SQLITE_ROW) {
        rows.push_back(InstrumentRow{
            .instrumentId = static_cast<std::uint32_t>(statement.columnInt64(0)),
            .exchangeSymbol = statement.columnText(1),
            .symbol = statement.columnText(2),
            .venueId = static_cast<std::uint16_t>(statement.columnInt64(3)),
            .serviceId = static_cast<std::uint16_t>(statement.columnInt64(4)),
            .instrumentType = static_cast<InstrumentType>(statement.columnInt64(5)),
            .state = static_cast<InstrumentState>(statement.columnInt64(6)),
            .tickSize = statement.columnInt64OrNull(7),
            .lotSize = statement.columnInt64OrNull(8),
            .multiplier = statement.columnInt64OrNull(9),
            .expiry = statement.columnInt64OrNull(10),
            .currency = statement.columnInt64OrNull(11),
            .capabilities = static_cast<std::uint16_t>(statement.columnInt64(12)),
            .version = static_cast<std::uint32_t>(statement.columnInt64(13)),
            .reportedTs = statement.columnInt64(14),
        });
    }
    return rows;
}

} // namespace motomoto
