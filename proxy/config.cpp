#include <iostream>
#include <iomanip>
#include <sstream>
#include "config.hpp"

ConfigHandler::ConfigHandler(/* args */) {}

ConfigHandler::~ConfigHandler() {}

/**
 * @brief Sets the configuration File Path.
 *
 * @param filePath as string
 * @return An enum of Proxy_Error_t.
 */
void ConfigHandler::setConfigFilePath(std::string filePath)
{
    /* set the configuration file path to be the passed parameter */
    this->configFilePath = filePath;
}

/**
 * @brief Gets the configuration File Path.
 *
 * @return std::string as the file path.
 */
std::string ConfigHandler::getConfigFilePath(void)
{
    /* return the configuration file path */
    return this->configFilePath;
}

/**
 * @brief Loads the Configuaration.
 *
 * Loads the Configuaration parameters from the configuation file
 * These configuartion ()
 *
 * @return An enum of Proxy_Error_t.
 */
Config_Error_t ConfigHandler::loadConfiguaration(void)
{
    try
    {
        this->myTopicsData.pubTopicsNames.clear();
        this->myTopicsData.subTopicsNames.clear();

        /* create the configuration parameters tree */
        boost::property_tree::ptree configTree;

        /* fill the tree with the content at the configuration file */
        boost::property_tree::ini_parser::read_ini(this->configFilePath, configTree);

        /* get the client parameters */
        this->myClientData.address = configTree.get<std::string>("client.address");
        this->myClientData.clientId = configTree.get<std::string>("client.clientId");
        this->myClientData.maxBufMsgs = configTree.get<uint32_t>("client.maxBufMsgs");
        this->myClientData.cleanSession = configTree.get<bool>("client.cleanSession");
        this->myClientData.autoReconnect = configTree.get<bool>("client.autoReconnect");
        this->myClientData.keepAliveTime = configTree.get<uint64_t>("client.keepAliveTime");

        this->myTopicsData.qualityOfService = configTree.get<uint8_t>("topics.qualityOfService");
        this->myTopicsData.retainedFlag = configTree.get<bool>("topics.retainedFlag");

        /* get the number of the rpis in the system */
        this->myTopicsData.numberOfRpis = configTree.get<uint8_t>("topics.numberOfRpis");
        if (this->myTopicsData.numberOfRpis == 0 || this->myTopicsData.numberOfRpis > MAX_SUPPORTED_RPIS)
        {
            std::cerr << "Invalid topics.numberOfRpis. Supported range is [1, "
                      << static_cast<int>(MAX_SUPPORTED_RPIS) << "]" << std::endl;
            return Config_Error_t::NOT_OK;
        }

        /* get sim's topics */
        this->myTopicsData.pubTopicsNames.push_back(configTree.get<std::string>("topics.simActionsTopic"));
        this->myTopicsData.subTopicsNames.push_back(configTree.get<std::string>("topics.simSensorsTopic"));

        /* loop to get trgt topics : x = [1:numberOfRpis] */
        for (uint8_t i = 0; i < this->myTopicsData.numberOfRpis; i++)
        {
            std::ostringstream indexStream;
            indexStream << std::setw(2) << std::setfill('0') << static_cast<int>(i + 1);
            const std::string keySuffix = indexStream.str();
            this->myTopicsData.pubTopicsNames.push_back(configTree.get<std::string>("topics.trgt" + keySuffix + "SensorsTopic"));
            this->myTopicsData.subTopicsNames.push_back(configTree.get<std::string>("topics.trgt" + keySuffix + "ActionsTopic"));
        }

        return Config_Error_t::OK;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Config error: " << e.what() << std::endl;
        return Config_Error_t::NOT_OK;
    }
}

std::string ConfigHandler::getAddress()
{
    return myClientData.address;
}

std::string ConfigHandler::getClientID()
{
    return myClientData.clientId;
}

uint32_t ConfigHandler::getMaxBufMsgs()
{
    return myClientData.maxBufMsgs;
}

bool ConfigHandler::getCleanSession()
{
    return myClientData.cleanSession;
}

bool ConfigHandler::getAutoReconnect()
{
    return myClientData.autoReconnect;
}

uint64_t ConfigHandler::getKeepAliveTime()
{
    return myClientData.keepAliveTime;
}

topicsNames_t ConfigHandler::getPubTocpicsNames()
{
    return myTopicsData.pubTopicsNames;
}

topicsNames_t ConfigHandler::getSubTocpicsNames()
{
    return myTopicsData.subTopicsNames;
}

uint8_t ConfigHandler::getNumberOfRpis()
{
    return myTopicsData.numberOfRpis;
}

uint8_t ConfigHandler::getQualityOfService()
{
    return myTopicsData.qualityOfService;
}

bool ConfigHandler::getRetainedFlag()
{
    return myTopicsData.retainedFlag;
}