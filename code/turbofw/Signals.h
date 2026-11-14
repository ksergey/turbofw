// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <bit>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <functional>

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

} // namespace detail

/// Register interest in a signal: it stops being fatal, processSignal() will report it.
/// Repeated installation is a no-op (the previous disposition is kept).
/// The pending record of the signal is cleared: something fired before installation
/// is not reported
void installSignalHandler(int sigNo) noexcept;

/// Register SIGINT, SIGTERM handlers
inline void installDefaultSignalHandlers() noexcept {
    for (auto sigNo : {SIGINT, SIGTERM}) {
        installSignalHandler(sigNo);
    }
}

/// Unregister a signal: restores the disposition the signal had before the installation and
/// clears its pending record (an unregistered signal is not reported). Not installed: a no-op.
void removeSignalHandler(int sigNo) noexcept;

/// Unregister SIGINT, SIGTERM handlers
inline void removeDefaultSignalHandlers() noexcept {
    for (auto sigNo : {SIGINT, SIGTERM}) {
        removeSignalHandler(sigNo);
    }
}

/// Drain the fired signals: fn(sigNo) once per pending signal, in signal-number order.
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
