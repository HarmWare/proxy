#include <iostream>
#include "config.hpp"
#include "proxy.hpp"

const std::string DEFAULT_CONFIG_FILEPATH{"./../config.ini"};

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
    ConfigHandler myConfig;

    /* getting the file of configuaration */
    myConfig.setConfigFilePath((argc > 1) ? std::string(argv[1]) : DEFAULT_CONFIG_FILEPATH);

    /* setting the configuarations */
    std::cout << "Loading configuaration from: '" << myConfig.getConfigFilePath() << "'..." << std::endl;

    if (myConfig.loadConfiguaration() == Config_Error_t::NOT_OK)
    {
        std::cerr << "Unable to load configuaration!" << std::endl;
        std::cerr << "LOADING THE DEFAULT CONFIGUARATION..." << std::endl;
    }

    Proxy myProxy(myConfig);


    try
    {
        /* Connect to the MQTT broker and wait till done */
        std::cout << "Connecting to server '" << myProxy.get_server_uri() << "'..." << std::flush;
        myProxy.connect();
        std::cout << "OK" << std::endl;

        myProxy.subscribe(); /* will be overloaded to match void input */

        while (true)
        {
            /* block until data is available from CARLA or all RPIs */
            Proxy_Flag_t flag = myProxy.waitForData();

            if (flag == Proxy_Flag_t::CARLA) /* received from carla */
            {
                /* parsing information */
                std::cout << "parsing..." << std::endl;
                myProxy.parse();

                /* publish to RPIS */
                myProxy.publish(Proxy_Flag_t::CARLA);

                /* clear the flag */
                myProxy.clearRxFlag(Proxy_Flag_t::CARLA);
            }
            else if (flag == Proxy_Flag_t::RPIS)
            {
                /* composing information */
                std::cout << "composing..." << std::endl;
                myProxy.compose();

                /* publish to CARLA */
                myProxy.publish(Proxy_Flag_t::RPIS);

                /* clear the flag */
                myProxy.clearRxFlag(Proxy_Flag_t::RPIS);
            }
        }
    }
    catch (const std::exception &e)
    {
        /* print the exception error */
        std::cerr << e.what() << '\n';

        return 1;
    }
}