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
            ("e,env", "Path to environment config", cxxopts::value<std::filesystem::path>(), "PATH")
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

        if (!parseResult.count("env")) {
            throw std::runtime_error{"option '--env' is mandatory"};
        }
        auto const sys = turbofw::SystemDefinition::loadFromFile(parseResult["env"].as<std::filesystem::path>());
        if (!sys) {
            throw std::runtime_error{sys.error()};
        }

        // The environment of this service
        auto const services = sys->services();
        if (!services) {
            throw std::runtime_error{services.error()};
        }
        auto const found = std::ranges::find_if(*services, [&](auto const& service) {
            return service.name == config->service.name;
        });
        if (found == services->end()) {
            throw std::runtime_error{
                std::format("service \"{}\" is not configured in the environment", config->service.name)};
        }
        auto const env = turbofw::Environment::create(std::move(*found));
        if (!env) {
            throw std::runtime_error{env.error()};
        }

        if (parseResult.count("stdout")) {
            initLoggerStdOut();
        } else {
            // the daily log file goes under the working directory (or the current one)
            auto const logPrefix = env->workingDirectory().empty() ? std::filesystem::path{"logs"} / "MotoMoto"
                                                                   : env->workingDirectory() / "logs" / "MotoMoto";
            auto const logLevel = logLevelFromString(config->logger.logLevel).value_or(turbofw::LogLevel::Debug);
            initLoggerDailyFile(logPrefix, logLevel);
        }

        // Run process loop
        Process{*env}.run();

        shutdownLogger();

    } catch (std::exception const& e) {
        std::println(stderr, "ERROR: {}", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
