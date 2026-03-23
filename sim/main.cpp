#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include <iostream>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include "mqtt/async_client.h"
#include "filehandling.hpp"
#include "sim.hpp"
#include "../common/observability.hpp"
#include "../common/runtime_control.hpp"

namespace
{
bool looks_like_json_payload(const std::string &value)
{
    const auto first = value.find_first_not_of(" \t\n\r");
    if (first == std::string::npos)
    {
        return false;
    }

    const char firstChar = value[first];
    return firstChar == '{' || firstChar == '[';
}
} // namespace

const std::string DFLT_ADDRESS{"mqtt://localhost:1883"};
const std::string CLIENT_ID{"sim"};
const std::string TOPIC_TO_PUB{"sim/sensors"};
const std::string TOPIC_TO_SUB{"sim/actions"};

const int QOS{1};

const std::filesystem::path DEFAULT_INPUT_FILE_PATH{"../../.logs/sim/sensors.csv"};
const std::filesystem::path DEFAULT_OUTPUT_FILE_PATH{"../../.logs/sim/actions.csv"};

const auto PERIOD = std::chrono::seconds(5);
const int MAX_BUFFERED_MSGS = 120; // 120 * 5sec => 10min off-line buffering

int main(int argc, char *argv[])
{
    runtime::ShutdownController::install();
    obs::Logger::instance().set_min_level(obs::LogLevel::INFO);

    /* init */
    /* get the passed argument to be the address, if not use the defualt */
    std::string address = (argc > 1) ? std::string(argv[1]) : DFLT_ADDRESS;
    const std::filesystem::path inputFilePath = (argc > 2) ? std::filesystem::path(argv[2]) : DEFAULT_INPUT_FILE_PATH;
    const std::filesystem::path outputFilePath = (argc > 3) ? std::filesystem::path(argv[3]) : DEFAULT_OUTPUT_FILE_PATH;

    /* create a client object */
    mqtt::async_client client(address, CLIENT_ID, MAX_BUFFERED_MSGS, NULL);

    /* create a callBack object and set the callBack */
    MyCallBack callback;
    client.set_callback(callback);

    /* set connection options */
    auto connOpts = mqtt::connect_options_builder()
                        .keep_alive_interval(MAX_BUFFERED_MSGS * PERIOD)
                        .clean_session(true)
                        .automatic_reconnect(true)
                        .finalize();

    /* creating the topics */
    mqtt::topic topForBub(client, TOPIC_TO_PUB, QOS, true);
    mqtt::topic topForSub(client, TOPIC_TO_SUB, QOS, true);

    /* high level data dealing */
    FileHandling handler;
    std::string sensors;

    try
    {
        /* Connect to the MQTT broker and wait till done */
        obs::Logger::instance().log(
            obs::LogLevel::INFO,
            "sim",
            "broker_connecting",
            "Connecting to MQTT broker",
            {{"uri", address}});
        client.connect(connOpts)->wait();
        obs::Logger::instance().log(
            obs::LogLevel::INFO,
            "sim",
            "broker_connected",
            "Connected to MQTT broker",
            {{"uri", address}});

        /* subscribe for the actions messages */
        topForSub.subscribe();
        while (!runtime::ShutdownController::requested())
        {
            /* getting sensors data from carla */
            const auto maybeSensors = handler.getData(inputFilePath);
            if (!maybeSensors.has_value())
            {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
            sensors = *maybeSensors;

            if (!looks_like_json_payload(sensors))
            {
                obs::Logger::instance().log(
                    obs::LogLevel::WARN,
                    "sim",
                    "publish_skipped",
                    "Skipping invalid or empty sensors payload",
                    {{"path", inputFilePath.string()}});
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }

            /* publish the new data each to its topic */
            topForBub.publish(sensors);

            if (callback.recived_msg_flag.load(std::memory_order_relaxed))
            {
                const auto actionMessage = callback.take_message();
                handler.setData(actionMessage, outputFilePath);
            }

            /* wait */
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }

        /* disconnect and wait till done */
        obs::Logger::instance().log(obs::LogLevel::INFO, "sim", "shutdown", "Disconnecting simulator harness");
        client.disconnect()->wait();
        obs::Logger::instance().log(obs::LogLevel::INFO, "sim", "disconnected", "Simulator harness disconnected");
    }
    catch (const mqtt::exception &exc)
    {
        obs::Logger::instance().log(
            obs::LogLevel::ERROR,
            "sim",
            "runtime_exception",
            "Unhandled simulator exception",
            {{"error", exc.what()}});
        return 1;
    }
    return 0;
}