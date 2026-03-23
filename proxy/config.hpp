#ifndef CONFIG__HHP_
#define CONFIG__HHP_
#include <iostream>
#include <chrono>
#include <limits>
#include "boost/property_tree/ini_parser.hpp"

enum class Config_Error_t
{
    OK,
    NOT_OK
};

using topicsNames_t = std::vector<std::string>;
constexpr uint8_t MAX_SUPPORTED_RPIS = 32;

struct ClientData
{
    std::string address;
    std::string clientId;
    uint32_t maxBufMsgs;
    bool cleanSession;
    bool autoReconnect;
    uint64_t keepAliveTime;
};

struct TopicsData
{
    topicsNames_t pubTopicsNames;
    topicsNames_t subTopicsNames;
    uint8_t numberOfRpis;
    uint8_t qualityOfService;
    bool retainedFlag;
};


class ConfigHandler
{
public:
    ConfigHandler(/* args */);
    ~ConfigHandler();

    /**
     * @brief Sets the configuration File Path.
     *
     * @param filePath as string
     * @return An enum of Proxy_Error_t.
     */
    void setConfigFilePath(std::string);

    /**
     * @brief Gets the configuration File Path.
     *
     * @return std::string as the file path.
     */
    std::string getConfigFilePath();

    /**
     * @brief Loads the Configuaration.
     *
     * Loads the Configuaration parameters from the configuation file
     * These configuartion ()
     *
     * @return An enum of Proxy_Error_t.
     */
    Config_Error_t loadConfiguaration(void);

    std::string getAddress();
    std::string getClientID();
    uint32_t getMaxBufMsgs();
    bool getCleanSession();
    bool getAutoReconnect();
    uint64_t getKeepAliveTime();

    topicsNames_t getPubTocpicsNames();
    topicsNames_t getSubTocpicsNames();

    uint8_t getNumberOfRpis();
    uint8_t getQualityOfService();
    bool getRetainedFlag();

private:
    static_assert(MAX_SUPPORTED_RPIS < std::numeric_limits<uint8_t>::max(), "MAX_SUPPORTED_RPIS must fit in uint8_t");
    ClientData myClientData;
    TopicsData myTopicsData;
    std::string configFilePath{"./config.ini"};
};

#endif /* CONFIG__HHP_ */