// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <bit>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <mutex>

#include <turbofw/Error.h>

namespace turbofw {

/// Process-wide OS signal manager.
///
/// `installSignalHandler` marks a signal as "interesting": it stops being fatal and every time it
/// fires, the handler records it with an async-signal-safe atomic operation. `processSignal` is
/// called on a regular thread (typically in the main loop) and runs the given lambda once per
/// recorded signal.
///
/// Contract:
/// - signals coalesce: two firings of the same signal before a drain are reported as one
///   (the same as POSIX queueing behavior for regular signals)
/// - the order of delivery is not preserved: the drain order is by signal number
/// - a signal fired during the drain is reported on the next `processSignal` call
/// - the lambda runs on the calling thread, may be called several times per call
///   and must not throw
///
/// Only signals 1..64 are supported (everything a 64-bit mask can hold);
/// SIGKILL/SIGSTOP cannot be caught and are rejected with EINVAL.

namespace detail {

inline std::atomic<std::uint64_t> gSignalPending{0};

static_assert(std::atomic<std::uint64_t>::is_always_lock_free, // async-signal-safe
    "the signal mask must be a lock-free atomic");

/// The async-signal-safe handler: records the signal, nothing else
extern "C" inline void signalHandler(int signum) {
    gSignalPending.fetch_or(std::uint64_t{1} << (signum - 1), std::memory_order_relaxed);
}

/// Signals registered with installSignalHandler(); protects install/remove, never taken on drain
inline std::mutex gSignalMutex;
inline std::map<int, struct sigaction> gSignalInstalled;

constexpr auto kMaxSignal = std::uint64_t{64};

[[nodiscard]] inline auto checkSignal(int signum) -> std::expected<void, std::error_code> {
    if (signum < 1 || static_cast<std::uint64_t>(signum) > kMaxSignal || signum == SIGKILL || signum == SIGSTOP) {
        return std::unexpected{makePosixErrorCode(EINVAL)};
    }
    return {};
}

} // namespace detail

/// Register interest in a signal: it stops being fatal, processSignal() will report it.
/// Repeated installation is a no-op (the previous disposition is kept).
/// The pending record of the signal is cleared: something fired before installation
/// is not reported
[[nodiscard]] inline auto installSignalHandler(int signum) -> std::expected<void, std::error_code> {
    if (auto const valid = detail::checkSignal(signum); !valid) {
        return std::unexpected{valid.error()};
    }

    auto lock = std::lock_guard{detail::gSignalMutex};
    if (detail::gSignalInstalled.contains(signum)) {
        return {};
    }

    struct sigaction action = {};
    action.sa_handler = detail::signalHandler;
    action.sa_flags = SA_RESTART; // don't disturb blocking syscalls of the caller
    ::sigemptyset(&action.sa_mask);

    struct sigaction previous = {};
    if (::sigaction(signum, &action, &previous) != 0) {
        return std::unexpected{makePosixErrorCode(errno)};
    }
    detail::gSignalInstalled.emplace(signum, previous);

    // forget anything that fired before the installation (there was no handler for it)
    detail::gSignalPending.fetch_and(~(std::uint64_t{1} << (signum - 1)), std::memory_order_relaxed);
    return {};
}

/// Unregister a signal: restores the disposition the signal had before the installation and
/// clears its pending record (an unregistered signal is not reported). Not installed: a no-op.
[[nodiscard]] inline auto removeSignalHandler(int signum) -> std::expected<void, std::error_code> {
    if (auto const valid = detail::checkSignal(signum); !valid) {
        return std::unexpected{valid.error()};
    }

    auto lock = std::lock_guard{detail::gSignalMutex};
    auto const node = detail::gSignalInstalled.find(signum);
    if (node == detail::gSignalInstalled.end()) {
        return {};
    }

    if (::sigaction(signum, &node->second, nullptr) != 0) {
        return std::unexpected{makePosixErrorCode(errno)};
    }
    detail::gSignalInstalled.erase(node);

    detail::gSignalPending.fetch_and(~(std::uint64_t{1} << (signum - 1)), std::memory_order_relaxed);
    return {};
}

/// Drain the fired signals: fn(signum) once per pending signal, in signal-number order.
/// Non-blocking; returns how many signals were processed (0 when nothing fired).
template <typename Fn>
    requires std::invocable<Fn&, int>
auto processSignal(Fn&& fn) -> std::size_t {
    auto const fired = detail::gSignalPending.exchange(0, std::memory_order_relaxed);
    if (fired == 0) [[likely]] {
        return 0;
    }

    auto count = std::size_t{0};
    auto bits = fired;
    while (bits != 0) {
        auto const bit = std::countr_zero(bits);
        bits &= bits - 1;
        std::invoke(fn, static_cast<int>(bit + 1));
        ++count;
    }
    return count;
}

} // namespace turbofw
