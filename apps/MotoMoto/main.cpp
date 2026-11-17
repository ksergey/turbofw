// Copyright (c) Sergey Kovalevich <inndie@gmail.com>
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <filesystem>
#include <format>
#include <print>

#include <cxxopts.hpp>

#include <turbofw/Logging.h>
#include <turbofw/SystemDefinition.h>

#include "Config.h"
#include "Process.h"

int main(int argc, char* argv[]) {
    int rc = EXIT_SUCCESS;

    try {
        auto options = cxxopts::Options{"MotoMoto", "The Metadata service"};
        // clang-format off
        options.add_options()
            ("s,system", "Path to system config", cxxopts::value<std::filesystem::path>(), "PATH")
            ("c,config", "Path to service config", cxxopts::value<std::filesystem::path>(), "PATH")
            ("d,dump", "Dump config and exit")
            ("stdout", "Log to stdout")
            ("h,help", "Print this help and exit")
        ;
        // clang-format on

        auto const parseResult = options.parse(argc, argv);
        if (parseResult.count("help")) {
            std::print(stderr, "{}\n", options.help({"", "Group"}));
            return EXIT_SUCCESS;
        }

        if (!parseResult.count("config")) {
            throw std::runtime_error{"option '--config' is mandatory"};
        }
        auto const config = app::Config::loadFromFile(parseResult["config"].as<std::filesystem::path>());
        if (!config) {
            throw std::runtime_error{config.error()};
        }

        if (parseResult.count("dump")) {
            std::print(stdout, "{}\n", config->toJson());
            return EXIT_FAILURE;
        }

        if (!parseResult.count("system")) {
            throw std::runtime_error{"option '--system' is mandatory"};
        }

        auto env = turbofw::SystemDefinition::loadFromFile(parseResult["system"].as<std::filesystem::path>())
                       .and_then([&](auto const& sys) {
                           return sys.environmentFor(config->service.name);
                       });
        if (!env) {
            throw std::runtime_error{env.error()};
        }

        // Set logger max verbosity level
        turbofw::setLogLevel(*turbofw::logLevelFromString(config->logger.logLevel));

        // Init logger sink
        if (parseResult.count("stdout")) {
            env->initLoggerStdOut();
        } else {
            env->initLoggerDailyFile();
        }

        // Run process loop
        app::Process{*config, *env}.run();

    } catch (std::exception const& e) {
        std::println(stderr, "ERROR: {}", e.what());
        if (turbofw::isLoggerBackendRunning()) {
            tq_error("ERROR: {}", e.what());
        }
        rc = EXIT_FAILURE;
    }

    // Shutdown logger
    turbofw::Environment::shutdownLogger();

    return rc;
}
