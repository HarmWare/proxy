#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <string>
#include <chrono>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include "mqtt/async_client.h"
#include "filehandling.hpp"
#include "trgt.hpp"

const std::string DFLT_ADDRESS{"tcp://localhost:1883"};
const int QOS{1};
const std::string DEF_ID{"01"};

const auto PERIOD = std::chrono::seconds(5);
const int MAX_BUFFERED_MSGS = 120; // 120 * 5sec => 10min off-line buffering

int main(int argc, char *argv[])
{
    /*const intialization*/
    /* init */
    /* get the passed argument to be the TRGT_ID, INPUT_FILE_PATH, and OUTPUT_FILE_PATH in this order */
    std::string TRGT_ID = (argc > 1) ? std::string(argv[1]) : "01";
    std::string INPUT_FILE_PATH = (argc > 2) ? std::string(argv[2]) : "../../.logs/trgt" + TRGT_ID + "/actions.csv";
    std::string OUTPUT_FILE_PATH = (argc > 3) ? std::string(argv[3]) : "../../.logs/trgt" + TRGT_ID + "/sensors.csv";

    const std::string CLIENT_ID{"trgt_" + TRGT_ID};
    const std::string TOPIC_TO_PUB{"trgt/" + TRGT_ID + "/actions"};
    const std::string TOPIC_TO_SUB{"trgt/" + TRGT_ID + "/sensors"};

    /* init */
    /* get the passed argument to be the address, if not use the defualt */
    std::string address = DFLT_ADDRESS;

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
        std::cout << "Connecting to server '" << address << "'..." << std::flush;
        client.connect(connOpts)->wait();
        std::cout << "OK" << std::endl;

        /* subscribe for the actions messages */
        topForSub.subscribe();
        while (true)
        {
            /* getting actions data from control algorithms (poc) */
            actions = handler.getData(INPUT_FILE_PATH);

            /* publish the new data each to its topic */
            topForBub.publish(actions);

            if (callback.recived_msg_flag)
            {
                handler.setData(callback.msg, OUTPUT_FILE_PATH);
                callback.recived_msg_flag = false;
            }

            /* wait */
            sleep(2);
        }

        /* disconnect and wait till done */
        std::cout << "\nDisconnecting..." << std::flush;
        client.disconnect()->wait();
        std::cout << "OK" << std::endl;
    }
    catch (const mqtt::exception &exc)
    {
        /* print the exception error */
        std::cerr << exc.what() << std::endl;
        return 1;
    }
    return 0;
}