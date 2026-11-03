// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <cstddef>
#include <list>
#include <optional>
#include <span>
#include <vector>

#include <turboq/Concepts.h>

namespace turbofw::testing {

/// An in-memory message queue for unit tests of producers/consumers and their mixins.
///
/// concept-compatible with the real turboq queues, but with test-oriented semantics: queue state lives in the
/// process, there is no shared memory, no file system access, no sizegeometric alignment and no multi-process rules.
/// Not thread-safe and single-producer/single-consumer by convention (like every turboq queue).
///
/// The `maxSize` knob is the test-time replacement for queue capacity: `prepare()` returns an empty buffer once
/// `maxSize` uncommitted-or-committed messages are queued, which is how the ENOBUFS path of send operations is
/// exercised without real ring geometry.
struct Queue {
    struct Buffer {
        std::vector<std::byte> data;
        /// The size from the commit operation, when it was called with a size; committed spans never exceed it
        std::optional<std::size_t> commitedSize;
    };

    std::list<Buffer> buffers;
    std::size_t maxSize{std::size_t(-1)};
};

/// concept-compatible producer for \ref Queue: messages are prepared and committed like on a real queue
///
/// - `prepare(size)` appends a zeroed buffer of `size` bytes at the back of the queue, or an empty one when the
///   queue already holds `maxSize` messages (the analog of a full ring);
/// - `commit()` publishes that buffer with its full prepared size, `commit(size)` publishes at most `size` bytes
///   of it;
/// - spans previously handed out stay valid for the queue lifetime (`std::list` gives stable addresses).
///
/// It commits the most recently prepared buffer: interleaving prepare/commit calls from two producers would
/// misbehave, but that is out of contract for a single-producer queue anyway.
struct Producer {
    Queue* queue{nullptr};

    explicit Producer(Queue* q) noexcept : queue{q} {}

    auto prepare(std::size_t size) -> std::span<std::byte> {
        if (!queue) {
            return {};
        }
        if (queue->buffers.size() == queue->maxSize) {
            return {};
        }
        auto& buffer = queue->buffers.emplace_back();
        buffer.data.resize(size);
        return std::span{buffer.data};
    }

    void commit() {
        if (!queue) {
            return;
        }
        auto& buffer = queue->buffers.back();
        if (!buffer.commitedSize) {
            buffer.commitedSize = buffer.data.size();
        }
    }

    void commit(std::size_t size) {
        if (!queue) {
            return;
        }
        auto& buffer = queue->buffers.back();
        if (!buffer.commitedSize) {
            buffer.commitedSize = std::min(buffer.data.size(), size);
        }
    }
};

static_assert(turboq::Producer<Producer>);

/// concept-compatible consumer for \ref Queue: messages are fetched and consumed like on a real queue
///
/// - `fetch()` returns the committed span of the front message, or an empty one when the queue is empty;
/// - `consume()` pops the front message;
/// - `reset()` drops everything, useful for tests that start over mid-case.
///
/// The consumer never observes uncommitted buffers: fetch() returns the front buffer before it gets committed
/// (constructs with the zeroed placeholder span).
struct Consumer {
    Queue* queue{nullptr};

    explicit Consumer(Queue* q) noexcept : queue{q} {}

    auto fetch() -> std::span<std::byte const> {
        if (!queue) {
            return {};
        }
        if (queue->buffers.empty()) {
            return {};
        }
        return std::span{queue->buffers.front().data};
    }

    void consume() {
        if (!queue) {
            return;
        }
        if (queue->buffers.empty()) {
            return;
        }
        queue->buffers.pop_front();
    }

    void reset() {
        if (!queue) {
            return;
        }
        queue->buffers.clear();
    }
};

static_assert(turboq::Consumer<Consumer>);

} // namespace turbofw::testing
