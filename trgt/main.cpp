#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <string>
#include <chrono>
#include <iostream>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <optional>
#include <string_view>
#include <thread>
#include "mqtt/async_client.h"
#include "filehandling.hpp"
#include "trgt.hpp"
#include "../common/observability.hpp"
#include "../common/runtime_control.hpp"

const std::string DFLT_ADDRESS{"tcp://localhost:1883"};
const int QOS{1};
const std::string DEF_ID{"01"};

const auto PERIOD = std::chrono::seconds(5);
const int MAX_BUFFERED_MSGS = 120; // 120 * 5sec => 10min off-line buffering

namespace
{
std::optional<std::string> parseTargetId(std::string_view value)
{
    if (value.size() != 2)
    {
        return std::nullopt;
    }

    if (!std::isdigit(static_cast<unsigned char>(value[0])) ||
        !std::isdigit(static_cast<unsigned char>(value[1])))
    {
        return std::nullopt;
    }

    return std::string(value);
}

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

int main(int argc, char *argv[])
{
    runtime::ShutdownController::install();
    obs::Logger::instance().set_min_level(obs::LogLevel::INFO);

    /*const intialization*/
    /* init */
    /* get the passed argument to be the TRGT_ID, INPUT_FILE_PATH, and OUTPUT_FILE_PATH in this order */
    const std::string rawTargetId = (argc > 1) ? std::string(argv[1]) : "01";
    auto targetId = parseTargetId(rawTargetId);
    if (!targetId.has_value())
    {
        obs::Logger::instance().log(
            obs::LogLevel::FATAL,
            "trgt",
            "invalid_target_id",
            "Target ID must be a two-digit numeric value",
            {{"value", rawTargetId}});
        return 1;
    }

    const std::filesystem::path INPUT_FILE_PATH = (argc > 2)
                                                       ? std::filesystem::path(argv[2])
                                                       : std::filesystem::path("../../.logs/trgt" + *targetId + "/actions.csv");
    const std::filesystem::path OUTPUT_FILE_PATH = (argc > 3)
                                                        ? std::filesystem::path(argv[3])
                                                        : std::filesystem::path("../../.logs/trgt" + *targetId + "/sensors.csv");

    const std::string CLIENT_ID{"trgt_" + *targetId};
    const std::string TOPIC_TO_PUB{"trgt/" + *targetId + "/actions"};
    const std::string TOPIC_TO_SUB{"trgt/" + *targetId + "/sensors"};

    /* init */
    /* optional argument 4 is broker address */
    std::string address = (argc > 4) ? std::string(argv[4]) : DFLT_ADDRESS;

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
    std::string actions;

    try
    {
        /* Connect to the MQTT broker and wait till done */
        obs::Logger::instance().log(
            obs::LogLevel::INFO,
            "trgt",
            "broker_connecting",
            "Connecting to MQTT broker",
            {{"uri", address}, {"target_id", *targetId}});
        client.connect(connOpts)->wait();
        obs::Logger::instance().log(
            obs::LogLevel::INFO,
            "trgt",
            "broker_connected",
            "Connected to MQTT broker",
            {{"uri", address}, {"target_id", *targetId}});

        /* subscribe for the actions messages */
        topForSub.subscribe();
        while (!runtime::ShutdownController::requested())
        {
            /* getting actions data from control algorithms (poc) */
            const auto maybeActions = handler.getData(INPUT_FILE_PATH);
            if (!maybeActions.has_value())
            {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
            actions = *maybeActions;

            if (!looks_like_json_payload(actions))
            {
                obs::Logger::instance().log(
                    obs::LogLevel::WARN,
                    "trgt",
                    "publish_skipped",
                    "Skipping invalid or empty action payload",
                    {{"path", INPUT_FILE_PATH.string()}, {"target_id", *targetId}});
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }

            /* publish the new data each to its topic */
            topForBub.publish(actions);

            if (callback.recived_msg_flag.load(std::memory_order_relaxed))
            {
                const auto sensorMessage = callback.take_message();
                handler.setData(sensorMessage, OUTPUT_FILE_PATH);
            }

            /* wait */
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }

        /* disconnect and wait till done */
        obs::Logger::instance().log(obs::LogLevel::INFO, "trgt", "shutdown", "Disconnecting target harness");
        client.disconnect()->wait();
        obs::Logger::instance().log(obs::LogLevel::INFO, "trgt", "disconnected", "Target harness disconnected");
    }
    catch (const mqtt::exception &exc)
    {
        obs::Logger::instance().log(
            obs::LogLevel::ERROR,
            "trgt",
            "runtime_exception",
            "Unhandled target exception",
            {{"error", exc.what()}});
        return 1;
    }
    return 0;
}