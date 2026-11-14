// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <turbofw/Environment.h>
#include <turbofw/ProducerConsumerTraits.h>
#include <turbofw/Types.h>
#include <turbofw/mixin/SBEKeepAliveMixin.h>
#include <turbofw/mixin/SBEReceiveMixin.h>
#include <turbofw/mixin/SBESendMixin.h>
#include <turbofw/mixin/SBEServiceIdStampMixin.h>

#include "Config.h"

namespace app {

using turbofw::Environment;
using turbofw::ServiceId;

class Process {
private:
    // clang-format off
    using Producer = turbofw::MakeProducer<turboq::MulticastMessageQueue::Producer,
        turbofw::Mixins<
            turbofw::mixin::SBESendMixin,
            turbofw::mixin::SBEServiceIdStampMixin,
            turbofw::mixin::SBEKeepAliveMixin
        >
    >;
    using Consumer = turbofw::MakeConsumer<turboq::MPSCMessageQueue::Consumer,
        turbofw::Mixins<
            turbofw::mixin::SBEReceiveMixin
        >
    >;
    // clang-format on

    Producer producer_;
    Consumer consumer_;

    bool running_{false};

public:
    Process(Process const&) = delete;
    Process& operator=(Process const&) = delete;

    Process(Config const& config, Environment const& env);

    /// Start process loop
    void run();

    /// Request to shutdown process loop
    void shutdown();
};

} // namespace app
