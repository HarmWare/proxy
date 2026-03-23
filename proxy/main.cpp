#include <iostream>
#include <chrono>
#include <filesystem>
#include <optional>
#include <thread>
#include "config.hpp"
#include "proxy.hpp"
#include "../common/observability.hpp"
#include "../common/runtime_control.hpp"

const std::string DEFAULT_CONFIG_FILEPATH{"./../config.ini"};

namespace
{
std::optional<std::filesystem::path> resolveConfigPath(int argc, char *argv[])
{
    if (argc > 1)
    {
        std::filesystem::path candidate(argv[1]);
        if (std::filesystem::exists(candidate))
        {
            return candidate;
        }
        return std::nullopt;
    }

    std::filesystem::path defaultPath(DEFAULT_CONFIG_FILEPATH);
    if (std::filesystem::exists(defaultPath))
    {
        return defaultPath;
    }
    return std::nullopt;
}
} // namespace

/**
 * @brief The main function that implements the core logic of the program.
 *
 * This function initializes a Proxy object, loads configuration from the command line or uses a default filepath,
 * connects to an MQTT broker, subscribes to topics, and handles incoming messages to perform communication between
 * components (CARLA and RPIS). It also catches exceptions and prints error messages if any occur during execution.
 *
 * @param argc The number of command-line arguments.
 * @param argv An array of strings containing the command-line arguments.
 *
 * @return Returns 0 on successful execution, or 1 if an exception occurs.
 */
int main(int argc, char *argv[])
{
    runtime::ShutdownController::install();
    obs::Logger::instance().set_min_level(obs::LogLevel::INFO);

    ConfigHandler myConfig;

    const auto configPath = resolveConfigPath(argc, argv);
    if (!configPath.has_value())
    {
        obs::Logger::instance().log(
            obs::LogLevel::FATAL,
            "proxy",
            "config_missing",
            "Configuration file not found",
            {{"hint", "Pass valid config path or provide ../config.ini"}});
        return 1;
    }

    myConfig.setConfigFilePath(configPath->string());

    obs::Logger::instance().log(
        obs::LogLevel::INFO,
        "proxy",
        "config_loading",
        "Loading configuration",
        {{"path", myConfig.getConfigFilePath()}});

    if (myConfig.loadConfiguaration() == Config_Error_t::NOT_OK)
    {
        obs::Logger::instance().log(
            obs::LogLevel::ERROR,
            "proxy",
            "config_load_failed",
            "Unable to load configuration");
        if (myConfig.getConfigFilePath() != DEFAULT_CONFIG_FILEPATH)
        {
            myConfig.setConfigFilePath(DEFAULT_CONFIG_FILEPATH);
            obs::Logger::instance().log(
                obs::LogLevel::WARN,
                "proxy",
                "config_fallback",
                "Retrying with default configuration",
                {{"path", DEFAULT_CONFIG_FILEPATH}});
            if (myConfig.loadConfiguaration() == Config_Error_t::NOT_OK)
            {
                obs::Logger::instance().log(
                    obs::LogLevel::FATAL,
                    "proxy",
                    "config_default_failed",
                    "Default configuration loading failed");
                return 1;
            }
        }
        else
        {
            return 1;
        }
    }

    Proxy myProxy(myConfig);
    obs::CircuitBreaker publishBreaker(3, std::chrono::milliseconds(5000));
    auto lastHeartbeat = std::chrono::steady_clock::now();

    try
    {
        obs::Logger::instance().log(
            obs::LogLevel::INFO,
            "proxy",
            "broker_connecting",
            "Connecting to MQTT broker",
            {{"uri", myProxy.get_server_uri()}});
        myProxy.connect();

        obs::Logger::instance().log(
            obs::LogLevel::INFO,
            "proxy",
            "broker_connected",
            "Connected to MQTT broker",
            {{"uri", myProxy.get_server_uri()}});

        myProxy.subscribe(); /* will be overloaded to match void input */

        while (!runtime::ShutdownController::requested())
        {
            /* block until data is available from CARLA or all RPIs */
            Proxy_Flag_t flag = myProxy.waitForData(std::chrono::milliseconds(1000));

            if (flag == Proxy_Flag_t::NOT)
            {
                const auto now = std::chrono::steady_clock::now();
                if (now - lastHeartbeat >= std::chrono::seconds(5))
                {
                    const auto metrics = myProxy.getMetricsSnapshot();
                    obs::Logger::instance().log(
                        obs::LogLevel::INFO,
                        "proxy",
                        "heartbeat",
                        "Proxy alive",
                        {{"received_messages", obs::to_field_value(metrics.receivedMessages)},
                         {"published_messages", obs::to_field_value(metrics.publishedMessages)},
                         {"parse_errors", obs::to_field_value(metrics.parseErrors)},
                         {"compose_errors", obs::to_field_value(metrics.composeErrors)},
                         {"publish_errors", obs::to_field_value(metrics.publishErrors)},
                         {"wait_timeouts", obs::to_field_value(metrics.waitTimeouts)},
                         {"circuit_breaker", publishBreaker.state_string()}});
                    lastHeartbeat = now;
                }
                continue;
            }

            if (!publishBreaker.allow())
            {
                obs::Logger::instance().log(
                    obs::LogLevel::WARN,
                    "proxy",
                    "circuit_open",
                    "Skipping publish cycle due to open circuit breaker");
                continue;
            }

            if (flag == Proxy_Flag_t::CARLA) /* received from carla */
            {
                myProxy.parse();

                /* publish to RPIS */
                myProxy.publish(Proxy_Flag_t::CARLA);

                /* clear the flag */
                myProxy.clearRxFlag(Proxy_Flag_t::CARLA);
            }
            else if (flag == Proxy_Flag_t::RPIS)
            {
                myProxy.compose();

                /* publish to CARLA */
                myProxy.publish(Proxy_Flag_t::RPIS);

                /* clear the flag */
                myProxy.clearRxFlag(Proxy_Flag_t::RPIS);
            }

            publishBreaker.on_success();
        }

        obs::Logger::instance().log(
            obs::LogLevel::INFO,
            "proxy",
            "shutdown_requested",
            "Shutdown requested, disconnecting proxy");
        myProxy.disconnect();
    }
    catch (const std::exception &e)
    {
        publishBreaker.on_failure();
        obs::Logger::instance().log(
            obs::LogLevel::ERROR,
            "proxy",
            "runtime_exception",
            "Unhandled runtime exception",
            {{"error", e.what()}});

        return 1;
    }

    return 0;
}