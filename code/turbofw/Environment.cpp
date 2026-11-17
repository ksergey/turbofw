// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "Environment.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>

#include <fmt/format.h>

#include <turboq-logger/DailyFileSink.h>
#include <turboq-logger/StdOutSink.h>

namespace turbofw {

namespace {

// A service name: non-empty, [A-Za-z0-9@._-]
[[nodiscard]] auto isServiceName(std::string_view value) noexcept -> bool {
    auto const isAllowed = [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '@' || c == '.' ||
               c == '_' || c == '-';
    };
    return !value.empty() && std::ranges::all_of(value, isAllowed);
}

// A queue name: non-empty, no leading/trailing '/', [A-Za-z0-9@._-/], and each '/'-separated part
// is a plain component: no "." / ".." (traversal), no "//"
[[nodiscard]] auto isQueueName(std::string_view name) noexcept -> bool {
    auto const isAllowed = [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '@' || c == '.' ||
               c == '_' || c == '-' || c == '/';
    };
    if (name.empty() || name.front() == '/' || name.back() == '/' || !std::ranges::all_of(name, isAllowed)) {
        return false;
    }
    for (std::size_t pos = 0; pos <= name.size();) {
        auto const slash = name.find('/', pos);
        auto const part = name.substr(pos, (slash == std::string_view::npos ? name.size() : slash) - pos);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        if (slash == std::string_view::npos) {
            break;
        }
        pos = slash + 1;
    }
    return true;
}

// The creation options sanity of a queue definition. Throws std::runtime_error
void validateCreationOptions(turbofw::QueueDefinition const& definition) {
    auto const checkHint = [&](std::size_t hint, std::string_view what) {
        if (hint == 0) {
            throw std::runtime_error{
                fmt::format("queue \"{}\": \"{}\" must be greater than zero", definition.name, what)};
        }
    };
    switch (definition.type) {
    case turbofw::QueueDefinition::QueueType::Multicast:
        checkHint(
            std::get_if<turboq::MulticastMessageQueue::CreationOptions>(&definition.creationOptions)->capacityHint,
            "capacity");
        break;
    case turbofw::QueueDefinition::QueueType::MPSC: {
        auto const* options = std::get_if<turboq::MPSCMessageQueue::CreationOptions>(&definition.creationOptions);
        checkHint(options->slotSizeHint, "slot-size");
        checkHint(options->lengthHint, "length");
        break;
    }
    case turbofw::QueueDefinition::QueueType::SPSC:
        checkHint(std::get_if<turboq::SPSCMessageQueue::CreationOptions>(&definition.creationOptions)->capacityHint,
            "capacity");
        break;
    }
}

// The memory source options sanity of a queue definition. Throws std::runtime_error
void validateMemorySourceOptions(turbofw::QueueDefinition const& definition) {
    if (definition.memorySourceOptions.path.empty()) {
        throw std::runtime_error{
            fmt::format("queue \"{}\": \"memory-source\" \"path\" must not be empty", definition.name)};
    }
    if (definition.memorySourceOptions.pageSize == 0) {
        throw std::runtime_error{
            fmt::format("queue \"{}\": \"memory-source\" \"page-size\" must be greater than zero", definition.name)};
    }
}

} // namespace

Environment::Environment(turbofw::ServiceDefinition definition) noexcept : definition_{std::move(definition)} {}

Environment::~Environment() noexcept {
    this->shutdownLogger();
}

auto Environment::create(turbofw::ServiceDefinition definition) noexcept -> std::expected<Environment, std::string> {
    try {
        if (!isServiceName(definition.name)) {
            throw std::runtime_error{fmt::format(
                "service name \"{}\" must not be empty and may contain [A-Za-z0-9@._-] only", definition.name)};
        }
        if (!definition.serviceId.hasValue()) {
            throw std::runtime_error{
                fmt::format("service \"{}\": \"service-id\" must not be null (65535)", definition.name)};
        }
        if (definition.queues.empty()) {
            throw std::runtime_error{fmt::format("service \"{}\": \"queues\" must not be empty", definition.name)};
        }

        auto seenQueueNames = std::map<std::string, std::string>{}; // queue name -> role
        for (auto const& [role, queueDefinition] : definition.queues) {
            if (role.empty()) {
                throw std::runtime_error{fmt::format("service \"{}\": queue role must not be empty", definition.name)};
            }
            if (!isQueueName(queueDefinition.name)) {
                throw std::runtime_error{
                    fmt::format("queue name \"{}\" (role \"{}\") is not valid: must not be empty, may contain "
                                "[A-Za-z0-9@._-/] only and must not escape the memory source mount",
                        queueDefinition.name, role)};
            }
            if (auto const [it, inserted] = seenQueueNames.emplace(queueDefinition.name, role); !inserted) {
                throw std::runtime_error{fmt::format("service \"{}\": queue \"{}\" is used by roles \"{}\" and \"{}\"",
                    definition.name, queueDefinition.name, it->second, role)};
            }
            validateCreationOptions(queueDefinition);
            validateMemorySourceOptions(queueDefinition);
        }

        return Environment{std::move(definition)};
    } catch (std::exception const& e) {
        return std::unexpected{e.what()};
    }
}

void Environment::initLoggerStdOut() {
    turboq::logger::BackendThread::setSink(std::make_shared<turboq::logger::StdOutSink>());
}

void Environment::initLoggerDailyFile() {
    auto const prefix = this->workingDirectory() / "logs" / this->name();
    turboq::logger::BackendThread::setSink(std::make_shared<turboq::logger::DailyFileSink>(prefix));
}

void Environment::shutdownLogger() {
    turboq::logger::BackendThread::flush();
    turboq::logger::BackendThread::stop();
}

auto Environment::findRoleQueue(std::string_view role) const noexcept -> turbofw::QueueDefinition const* {
    auto const found = definition_.queues.find(std::string{role});
    return found == definition_.queues.end() ? nullptr : &found->second;
}

auto Environment::makeMemorySource(turbofw::QueueDefinition const& definition,
    bool createDirectories) const -> std::expected<turboq::DefaultMemorySource, std::string> {
    try {
        if (createDirectories) {
            // the queue name may contain '/' ("<scope>/<service>@<role>"): create its parent
            // directories too, the default turboq source does not create them
            auto const filePath = definition.memorySourceOptions.path / definition.name;
            std::error_code ec;
            std::filesystem::create_directories(filePath.parent_path(), ec);
            if (ec) {
                return std::unexpected{
                    fmt::format("can't create \"{}\": {}", filePath.parent_path().string(), ec.message())};
            }
        }
        return turboq::DefaultMemorySource{
            definition.memorySourceOptions.path, definition.memorySourceOptions.pageSize};
    } catch (std::exception const& e) {
        return std::unexpected{e.what()};
    }
}

} // namespace turbofw
