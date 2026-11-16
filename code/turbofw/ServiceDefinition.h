// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <filesystem>
#include <map>
#include <string>

#include "QueueDefinition.h"
#include "Types.h"

namespace turbofw {

/// A service definition
struct ServiceDefinition {
    std::string name;
    ServiceId serviceId;
    std::map<std::string, QueueDefinition> queues;
    std::filesystem::path workingDirectory;
};

} // namespace turbofw
