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
    using namespace app;

    using turbofw::initLoggerDailyFile;
    using turbofw::initLoggerStdOut;
    using turbofw::logLevelFromString;
    using turbofw::shutdownLogger;

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
        auto const config = Config::loadFromFile(parseResult["config"].as<std::filesystem::path>());
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
        auto const sys = turbofw::SystemDefinition::loadFromFile(parseResult["system"].as<std::filesystem::path>());

        auto const env = sys.environmentFor(config->service.name);

        // TODO: env.initLogger(...)

        if (parseResult.count("stdout")) {
            initLoggerStdOut();
        } else {
#if 0
            // the daily log file goes under the working directory (or the current one)
            auto const logPrefix = env.workingDirectory().empty() ? std::filesystem::path{"logs"} / "MotoMoto"
                                                                   : env.workingDirectory() / "logs" / "MotoMoto";
            auto const logLevel = logLevelFromString(config->logger.logLevel).value_or(turbofw::LogLevel::Debug);
            initLoggerDailyFile(logPrefix, logLevel);
#endif
        }

        // Run process loop
        Process{*config, env}.run();

        shutdownLogger();

    } catch (std::exception const& e) {
        std::println(stderr, "ERROR: {}", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
