// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <benchmark/benchmark.h>

#include "Clock.h"

namespace {

void BM_RealtimeNanos(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(turbofw::realtimeNanos());
    }
}
BENCHMARK(BM_RealtimeNanos);

void BM_MonotonicNanos(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(turbofw::monotonicNanos());
    }
}
BENCHMARK(BM_MonotonicNanos);

} // namespace
