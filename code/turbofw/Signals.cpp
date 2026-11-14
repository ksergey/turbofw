// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include "Signals.h"

#include <expected>
#include <mutex>
#include <unordered_map>

#include "Error.h"
#include "Logging.h"

namespace turbofw {
namespace {

/// The async-signal-safe handler: records the signal, nothing else
extern "C" inline void signalHandler(int sigNo) {
    detail::gSignalPending.fetch_or(std::uint64_t{1} << (sigNo - 1), std::memory_order_relaxed);
}

/// Signals registered with installSignalHandler(); protects install/remove, never taken on drain
std::mutex gSignalMutex;
std::unordered_map<int, struct sigaction> gSignalInstalled;

constexpr auto kMaxSignal = std::uint64_t{64};

[[nodiscard]] inline auto checkSignal(int sigNo) -> std::expected<void, std::error_code> {
    if (sigNo < 1 || static_cast<std::uint64_t>(sigNo) > kMaxSignal || sigNo == SIGKILL || sigNo == SIGSTOP) {
        return std::unexpected{makePosixErrorCode(EINVAL)};
    }
    return {};
}

} // namespace

void installSignalHandler(int sigNo) noexcept {
    if (auto const valid = checkSignal(sigNo); !valid) {
        tq_warn("failed to install signal {} handler: {}", sigNo, valid.error().message());
        return;
    }

    auto lock = std::lock_guard{gSignalMutex};
    if (gSignalInstalled.contains(sigNo)) {
        return;
    }

    struct sigaction action = {};
    action.sa_handler = signalHandler;
    action.sa_flags = SA_RESTART; // don't disturb blocking syscalls of the caller
    ::sigemptyset(&action.sa_mask);

    struct sigaction previous = {};
    if (::sigaction(sigNo, &action, &previous) != 0) {
        tq_warn("failed to install signal {} handler: {}", sigNo, ::strerror(errno));
        return;
    }
    gSignalInstalled.emplace(sigNo, previous);

    // forget anything that fired before the installation (there was no handler for it)
    detail::gSignalPending.fetch_and(~(std::uint64_t{1} << (sigNo - 1)), std::memory_order_relaxed);
}

void removeSignalHandler(int sigNo) noexcept {
    if (auto const valid = checkSignal(sigNo); !valid) {
        tq_warn("failed to install signal {} handler: {}", sigNo, valid.error().message());
        return;
    }

    auto lock = std::lock_guard{gSignalMutex};
    auto const node = gSignalInstalled.find(sigNo);
    if (node == gSignalInstalled.end()) {
        return;
    }

    if (::sigaction(sigNo, &node->second, nullptr) != 0) {
        tq_warn("failed to install signal {} handler: {}", sigNo, ::strerror(errno));
    }
    gSignalInstalled.erase(node);

    detail::gSignalPending.fetch_and(~(std::uint64_t{1} << (sigNo - 1)), std::memory_order_relaxed);
}

} // namespace turbofw
