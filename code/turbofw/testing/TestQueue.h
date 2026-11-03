// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <list>
#include <optional>
#include <span>
#include <vector>

#include <turboq/Concepts.h>

namespace turbofw::testing {

struct Queue {
    struct Buffer {
        std::vector<std::byte> data;
        std::optional<std::size_t> commitedSize;
    };

    std::list<Buffer> buffers;
    std::size_t maxSize{std::size_t(-1)};
};

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
