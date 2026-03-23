#include "trgt.hpp"
#include "../common/observability.hpp"
void MyCallBack::connection_lost(const std::string &cause)
{
    obs::Logger::instance().log(
        obs::LogLevel::WARN,
        "trgt",
        "connection_lost",
        "Connection to broker lost",
        {{"cause", cause}});
}

void MyCallBack::message_arrived(mqtt::const_message_ptr messagePtr)
{
    {
        std::lock_guard<std::mutex> lock(this->msgMutex);
        this->msg = messagePtr->to_string();
        recived_msg_flag.store(true, std::memory_order_relaxed);
    }

    obs::Logger::instance().log(
        obs::LogLevel::DEBUG,
        "trgt",
        "message_arrived",
        "Message arrived",
        {{"topic", messagePtr->get_topic()}, {"size", obs::to_field_value(messagePtr->to_string().size())}});
}

void MyCallBack::delivery_complete(mqtt::delivery_token_ptr token)
{
    obs::Logger::instance().log(
        obs::LogLevel::DEBUG,
        "trgt",
        "delivery_complete",
        "Delivery complete",
        {{"topic", token->get_message()->get_topic()}});
}

std::string MyCallBack::take_message()
{
    std::lock_guard<std::mutex> lock(this->msgMutex);
    recived_msg_flag.store(false, std::memory_order_relaxed);
    return this->msg;
}