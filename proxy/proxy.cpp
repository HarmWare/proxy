#include <iostream>
#include <mutex>
#include <stdexcept>
#include <algorithm>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include "config.hpp"
#include "mqtt/async_client.h"
#include "proxy.hpp"
#include "../common/observability.hpp"

Proxy::Proxy(ConfigHandler &config) : proxyClient(config.getAddress(), config.getClientID(), config.getMaxBufMsgs(), nullptr)
{
    /* get the names of topics in a local variable */
    topicsNames_t subTopicsNames = config.getSubTocpicsNames();
    topicsNames_t pubTopicsNames = config.getPubTocpicsNames();

    /* get qos and retained flag in a local variable */
    uint8_t qualityOfService = config.getQualityOfService();

    bool retainedFlag = config.getRetainedFlag();

    /* set the call back of message arrival */
    this->proxyClient.set_message_callback([&](mqtt::const_message_ptr msg)
                                           {
                                            /* take the content of the message */
                                            std::string topic = msg->get_topic();
                                            std::string content = msg->to_string();
                                            this->metricsReceivedMessages.fetch_add(1, std::memory_order_relaxed);
                                            
                                            obs::Logger::instance().log(
                                                obs::LogLevel::DEBUG,
                                                "proxy",
                                                "message_received",
                                                "MQTT message arrived",
                                                {{"topic", topic}, {"size", obs::to_field_value(content.size())}});

                                            for (uint8_t i = 0; i < this->numberOfRpis + 1; i++)/* which topic i received on */
                                            {
                                                if (topic == this->subTopics[i].get_name())
                                                {
                                                    {
                                                        std::lock_guard<std::mutex> lk(this->flagMutex);
                                                        this->Rx |= (1 << i);               /* set the corresponding bit */
                                                    }

                                                    if (!i)  /* cpy the content */
                                                    {
                                                        std::lock_guard<std::mutex> lk(this->sensorsMutex);
                                                        this->sensorsMsgs[i] = content;
                                                    }
                                                    else    /* cpy the content */
                                                    {
                                                        std::lock_guard<std::mutex> lk(this->actionsMutex);
                                                        this->actionsMsgs[i] = content;
                                                    }
                                                    this->rxCondition.notify_one();
                                                }
                                            } });

    /* set the call back of connection */
    this->proxyClient.set_connected_handler([&](const std::string &)
                                            { std::cout << "Connected to server: '"
                                                        << this->proxyClient.get_server_uri() << "'"
                                                        << std::endl; });

    /* set the call back of the connection lost */
    this->proxyClient.set_connection_lost_handler([&](const std::string &)
                                                  { std::cout << "Connection to server: '"
                                                              << this->proxyClient.get_server_uri()
                                                              << "' lost!" << std::endl; });

    /* set the call back of disconnection */
    this->proxyClient.set_disconnected_handler([&](const mqtt::properties &, mqtt::ReasonCode)
                                               { std::cout << "Disconnected to server: '"
                                                           << this->proxyClient.get_server_uri() << "'"
                                                           << std::endl; });

    /* set the number of rpis */
    this->numberOfRpis = config.getNumberOfRpis();

    /* resize the messages data vectors */
    this->actionsMsgs.resize(this->numberOfRpis + 1);
    this->sensorsMsgs.resize(this->numberOfRpis + 1);

    /* calculate the mask of the Rx flag */
    this->maskRx = (1ULL << (this->numberOfRpis + 1)) - 2;

    /* create the topics of publishing and subscription */
    for (uint8_t i = 0; i < this->numberOfRpis + 1; i++)
    {
        this->pubTopics.push_back({proxyClient, pubTopicsNames[i], qualityOfService, retainedFlag});
        this->subTopics.push_back({proxyClient, subTopicsNames[i], qualityOfService, retainedFlag});
    }

    /* create a connection options handler */
    this->connectionOptions = mqtt::connect_options_builder()
                                  .keep_alive_interval(std::chrono::seconds(config.getKeepAliveTime()))
                                  .clean_session(config.getCleanSession())
                                  .automatic_reconnect(config.getAutoReconnect())
                                  .finalize();
}

Proxy::~Proxy() {}

/**
 * @brief Connects to an MQTT server using the default options.
 * @return token used to track and wait for the connect to complete. The
 *  	   token will be passed to any callback that has been set.
 * @throw exception for non security related problems
 * @throw security_exception for security related problems
 */
void Proxy::connect(void)
{
    /* connect the client to the broker with the connection options that have been set */
    this->proxyClient.connect(this->connectionOptions)->wait();
}

/**
 * @brief Disconnects from the MQTT broker.
 * @return token used to track and wait for the disconnect to complete.
 *  	   The token will be passed to any callback that has been set.
 * @throw exception for problems encountered while disconnecting
 */
void Proxy::disconnect(void)
{
    /* disconnect the client from the broker */
    this->proxyClient.disconnect()->wait();
}

/**
 * @brief Returns the address of the server used by this client.
 * @return The server's address, as a URI String.
 */
std::string Proxy::get_server_uri(void)
{
    /* return the servers's address */
    return this->proxyClient.get_server_uri();
}

void Proxy::subscribe(void)
{
    /* loop and subscribe with each topic of subscribtion */
    for (uint8_t i = 0; i < this->numberOfRpis + 1; i++)
    {
        this->subTopics[i].subscribe();
        obs::Logger::instance().log(
            obs::LogLevel::INFO,
            "proxy",
            "subscribed",
            "Subscribed to topic",
            {{"topic", this->subTopics[i].get_name()}});
    }
}
Proxy_Flag_t Proxy::getRxFalg()
{
    std::lock_guard<std::mutex> lk(this->flagMutex);
    /* if bit-0 of the flag equal to 1 so the proxy has received from carla*/
    if (this->Rx & 1)
        return Proxy_Flag_t::CARLA;

    /* if bits[1:numberOfRpis] all equal to 1 so the proxy has received from all rpis */
    else if ((this->Rx == this->maskRx) || (this->Rx == this->maskRx + 1))
        return Proxy_Flag_t::RPIS;

    else
        return Proxy_Flag_t::NOT;
}
void Proxy::publish(Proxy_Flag_t type)
{
    /* publish what has been received from sim */
    if (type == Proxy_Flag_t::CARLA)
    {
        for (uint8_t i = 0; i < this->numberOfRpis; i++)
        {
            try
            {
                this->pubTopics[i + 1].publish(this->sensorsMsgs[i + 1]);
                this->metricsPublishedMessages.fetch_add(1, std::memory_order_relaxed);
                obs::Logger::instance().log(
                    obs::LogLevel::DEBUG,
                    "proxy",
                    "published",
                    "Published message to target topic",
                    {{"topic", this->pubTopics[i + 1].get_name()},
                     {"size", obs::to_field_value(this->sensorsMsgs[i + 1].size())}});
            }
            catch (const std::exception &exception)
            {
                this->metricsPublishErrors.fetch_add(1, std::memory_order_relaxed);
                throw std::runtime_error(std::string("Failed to publish target message: ") + exception.what());
            }
        }
    }

    /* publish what has been received from trgts */
    else if (type == Proxy_Flag_t::RPIS)
    {
        try
        {
            this->pubTopics[0].publish(this->actionsMsgs[0]);
            this->metricsPublishedMessages.fetch_add(1, std::memory_order_relaxed);
            obs::Logger::instance().log(
                obs::LogLevel::DEBUG,
                "proxy",
                "published",
                "Published aggregate action message",
                {{"topic", this->pubTopics[0].get_name()},
                 {"size", obs::to_field_value(this->actionsMsgs[0].size())}});
        }
        catch (const std::exception &exception)
        {
            this->metricsPublishErrors.fetch_add(1, std::memory_order_relaxed);
            throw std::runtime_error(std::string("Failed to publish simulator action message: ") + exception.what());
        }
    }
}

void Proxy::clearRxFlag(Proxy_Flag_t type)
{
    std::lock_guard<std::mutex> lk(this->flagMutex);
    /* clear the corresponding bit of carla (bit-0) */
    if (type == Proxy_Flag_t::CARLA)
        this->Rx &= (~(1));

    /* clear the corresponding bits of rpis bits[1:numberOfRpis] */
    else
        this->Rx &= (~(this->maskRx));
}

void Proxy::parse()
{
    std::lock_guard<std::mutex> lk(this->sensorsMutex);

    if (this->sensorsMsgs.empty() || this->sensorsMsgs[0].empty())
    {
        this->metricsParseErrors.fetch_add(1, std::memory_order_relaxed);
        obs::Logger::instance().log(obs::LogLevel::WARN, "proxy", "parse_skipped", "Simulator payload is empty");
        return;
    }

    std::vector<std::string> parsedStrings = parseJSONString(this->sensorsMsgs[0]);

    /* Keep a stable size for all configured RPIs to avoid out-of-bounds access */
    this->sensorsMsgs.resize(this->numberOfRpis + 1);

    /* Clear previous per-target payloads */
    std::fill(this->sensorsMsgs.begin() + 1, this->sensorsMsgs.end(), std::string{});

    /* Assign parsed strings to sensorMsgs starting from index 1 */
    const size_t payloadCount = std::min(parsedStrings.size(), static_cast<size_t>(this->numberOfRpis));
    for (size_t i = 0; i < payloadCount; ++i)
    {
        this->sensorsMsgs[i + 1] = parsedStrings[i];
    }
}

void Proxy::compose()
{
    std::lock_guard<std::mutex> lk(this->actionsMutex);

    if (this->actionsMsgs.size() < 2)
    {
        this->metricsComposeErrors.fetch_add(1, std::memory_order_relaxed);
        obs::Logger::instance().log(obs::LogLevel::ERROR, "proxy", "compose_failed", "Not enough messages to compose");
        return;
    }

    /* Collect the strings from actionMsgs[1] to actionMsgs[n]*/
    std::vector<std::string> stringList(this->actionsMsgs.begin() + 1, this->actionsMsgs.end());

    /* Compose them into a single JSON string */
    std::string composedJSON = composeJSONString(stringList);

    /* Assign the composed JSON string to actionMsgs[0] */
    this->actionsMsgs[0] = composedJSON;
}

std::vector<std::string> Proxy::parseJSONString(const std::string &jsonString)
{
    namespace pt = boost::property_tree;

    std::vector<std::string> resultList;
    try
    {
        pt::ptree tree;
        std::istringstream iss(jsonString);
        pt::read_json(iss, tree);

        /* Iterate over each property in the JSON object */
        for (const auto &pair : tree)
        {
            std::ostringstream oss;
            oss << "[";
            bool first = true;
            for (const auto &item : pair.second)
            {
                if (!first)
                {
                    oss << ", ";
                }
                oss << item.second.get_value<float>();
                first = false;
            }
            oss << "]";
            resultList.push_back(oss.str());
        }
    }
    catch (const std::exception &e)
    {
        this->metricsParseErrors.fetch_add(1, std::memory_order_relaxed);
        obs::Logger::instance().log(
            obs::LogLevel::ERROR,
            "proxy",
            "json_parse_error",
            "Error parsing simulator JSON payload",
            {{"error", e.what()}});
    }

    return resultList;
}

std::string Proxy::composeJSONString(const std::vector<std::string> &stringList)
{
    std::ostringstream oss;

    try
    {
        oss << "["; // Start of the JSON array

        for (size_t i = 0; i < stringList.size(); ++i)
        {
            if (i > 0)
                oss << ","; // Separate elements with a comma except before the first element

            oss << stringList[i]; // Append the current string as-is
        }

        oss << "]"; // End of the JSON array

        return oss.str();
    }
    catch (const std::exception &e)
    {
        this->metricsComposeErrors.fetch_add(1, std::memory_order_relaxed);
        obs::Logger::instance().log(
            obs::LogLevel::ERROR,
            "proxy",
            "json_compose_error",
            "Error composing action JSON payload",
            {{"error", e.what()}});
        return "";
    }
}

Proxy_Flag_t Proxy::waitForData()
{
    std::unique_lock<std::mutex> lk(this->flagMutex);
    this->rxCondition.wait(lk, [this]
                           { return (this->Rx & 1) ||
                                    (this->Rx == this->maskRx) ||
                                    (this->Rx == this->maskRx + 1); });

    if (this->Rx & 1)
        return Proxy_Flag_t::CARLA;
    return Proxy_Flag_t::RPIS;
}

Proxy_Flag_t Proxy::waitForData(std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lk(this->flagMutex);
    const bool ready = this->rxCondition.wait_for(
        lk,
        timeout,
        [this]
        {
            return (this->Rx & 1) ||
                   (this->Rx == this->maskRx) ||
                   (this->Rx == this->maskRx + 1);
        });

    if (!ready)
    {
        this->metricsWaitTimeouts.fetch_add(1, std::memory_order_relaxed);
        return Proxy_Flag_t::NOT;
    }

    if (this->Rx & 1)
        return Proxy_Flag_t::CARLA;
    return Proxy_Flag_t::RPIS;
}

Proxy::RuntimeMetricsSnapshot Proxy::getMetricsSnapshot() const
{
    RuntimeMetricsSnapshot snapshot;
    snapshot.receivedMessages = this->metricsReceivedMessages.load(std::memory_order_relaxed);
    snapshot.publishedMessages = this->metricsPublishedMessages.load(std::memory_order_relaxed);
    snapshot.parseErrors = this->metricsParseErrors.load(std::memory_order_relaxed);
    snapshot.composeErrors = this->metricsComposeErrors.load(std::memory_order_relaxed);
    snapshot.publishErrors = this->metricsPublishErrors.load(std::memory_order_relaxed);
    snapshot.waitTimeouts = this->metricsWaitTimeouts.load(std::memory_order_relaxed);
    return snapshot;
}