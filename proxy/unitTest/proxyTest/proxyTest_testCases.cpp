#include <gmock/gmock-actions.h>
#include <gmock/gmock-spec-builders.h>
#include <gtest/gtest.h>
#include "proxyTest_testCases.hpp"
#include "proxy.hpp"
#include <chrono>
void ProxyTest::SetUp()
{
    EXPECT_CALL(mch, getAddress).Times(1).WillOnce(testing::Return("tcp://localhost:1883"));
    EXPECT_CALL(mch, getClientID).Times(1).WillOnce(testing::Return("proxy"));
    EXPECT_CALL(mch, getMaxBufMsgs).Times(1).WillOnce(testing::Return(120));
    EXPECT_CALL(mch, getSubTocpicsNames).Times(1).WillOnce(testing::Return(std::vector<std::string>{"sim/sensors", "trgt/01/actions", "trgt/02/actions"}));
    EXPECT_CALL(mch, getPubTocpicsNames).Times(1).WillOnce(testing::Return(std::vector<std::string>{"sim/actions", "trgt/01/sensors", "trgt/02/sensors"}));
    EXPECT_CALL(mch, getQualityOfService).Times(1).WillRepeatedly(testing::Return(1));
    EXPECT_CALL(mch, getRetainedFlag).Times(1).WillRepeatedly(testing::Return(true));
    EXPECT_CALL(mch, getKeepAliveTime).Times(1).WillOnce(testing::Return(600));
    EXPECT_CALL(mch, getCleanSession).Times(1).WillOnce(testing::Return(true));
    EXPECT_CALL(mch, getAutoReconnect).Times(1).WillOnce(testing::Return(true));
    EXPECT_CALL(mch, getNumberOfRpis).Times(1).WillOnce(testing::Return(2));
    myProxy = new Proxy(mch);
}
void ProxyTest::TearDown()
{
    delete myProxy;
}

TEST_F(ProxyTest, Proxy_Flag)
{

    EXPECT_EQ(myProxy->getRxFalg(), Proxy_Flag_t::NOT);
}

TEST_F(ProxyTest, get_server_uri)
{

    EXPECT_STREQ(myProxy->get_server_uri().c_str(), "tcp://localhost:1883");
}

TEST_F(ProxyTest, publishSenario)
{
    /*you need to launch the mosquitto first*/
    myProxy->connect();
    myProxy->publish(Proxy_Flag_t::CARLA);
    myProxy->publish(Proxy_Flag_t::RPIS);
    myProxy->disconnect();

}

TEST_F(ProxyTest,subscibeSenario)
{
    /*you need to launch the mosquitto first*/
    myProxy->connect();
    myProxy->subscribe();
    myProxy->disconnect();

}

TEST_F(ProxyTest,getDatafromCarla)
{
    /*you need to launch the mosquitto first*/
    myProxy->connect();
    myProxy->subscribe();
    /*wait until mosquitto send to test*/
     std::this_thread::sleep_for(std::chrono::seconds(4));
     if (myProxy->getRxFalg() == Proxy_Flag_t::CARLA) /* received from carla */
            {

                /* parsing information */
                myProxy->parse();

                /* publish to RPIS */
                myProxy->publish(Proxy_Flag_t::CARLA);

                /* clear the flag */
                myProxy->clearRxFlag(Proxy_Flag_t::CARLA);
            }
    myProxy->disconnect();

}


TEST_F(ProxyTest,getDatafromRPIS)
{
    /*you need to launch the mosquitto first*/
    myProxy->connect();
    myProxy->subscribe();
    /*wait until mosquitto send to test*/
     std::this_thread::sleep_for(std::chrono::seconds(4));
     if (myProxy->getRxFalg() == Proxy_Flag_t::RPIS)
            {
                /* composing information */
                myProxy->compose();

                /* publish to CARLA */
                myProxy->publish(Proxy_Flag_t::RPIS);

                /* clear the flag */
                myProxy->clearRxFlag(Proxy_Flag_t::RPIS);
            }
    myProxy->disconnect();

}


