#include "sim.hpp"

void MyCallBack::connection_lost(const std::string &cause)
{
    std::cout << "Connection lost: " << cause << std::endl;
}

void MyCallBack::message_arrived(mqtt::const_message_ptr msg)
{
    recived_msg_flag = true;

    std::cout << "Message arrived with data: " << msg->to_string()
              << ", from topic: " << msg->get_topic()
              << std::endl;

    this->msg = msg->to_string();
}

void MyCallBack::delivery_complete(mqtt::delivery_token_ptr token)
{
    std::cout << "Delivery complete for data: " << token->get_message()->get_payload()
              << ", to topic: " << token->get_message()->get_topic()
              << std::endl;
}