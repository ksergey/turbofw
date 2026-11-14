// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <turbofw/Environment.h>

#include "Config.h"

namespace app {

using turbofw::Environment;

class Process {
private:
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
