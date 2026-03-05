#include "configTest_testCases.hpp"
#include "config.hpp"
#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <string>
using namespace ::testing;

void configTest::SetUp()
{
    handler.setConfigFilePath("config.ini");
    handler.loadConfiguaration();
}
void configTest::TearDown() {}

TEST_F(configTest, getAddress)
{
    std::string address{"mqtt://localhost:1883"};
    EXPECT_STREQ(address.c_str(), handler.getAddress().c_str());
}

TEST_F(configTest, getClientID)
{
    std::string ID{"proxy"};
    EXPECT_STREQ(ID.c_str(), handler.getClientID().c_str());
}

TEST_F(configTest, getMaxBufMsgs_1)
{
    int MaxBufMsgs{150};
    EXPECT_NE(MaxBufMsgs, handler.getMaxBufMsgs());
}

TEST_F(configTest, getMaxBufMsgs_2)
{
    int MaxBufMsgs{1000};
    EXPECT_EQ(MaxBufMsgs, handler.getMaxBufMsgs());
}

TEST_F(configTest, getCleanSession)
{
    EXPECT_TRUE(handler.getCleanSession());
}

TEST_F(configTest, getAutoReconnect)
{
    EXPECT_TRUE(handler.getAutoReconnect());
}

TEST_F(configTest, getKeepAliveTime_1)
{
    int aliveTime{120};
    EXPECT_NE(aliveTime, handler.getKeepAliveTime());
}

TEST_F(configTest, getKeepAliveTime_2)
{
    int aliveTime{600};
    EXPECT_EQ(aliveTime, handler.getKeepAliveTime());
}

TEST_F(configTest, getPubTocpicsNames)
{
    EXPECT_THAT(handler.getPubTocpicsNames(), ElementsAre("sim/actions", "trgt/01/sensors", "trgt/02/sensors"));
}

TEST_F(configTest, getPubTocpicsNames_2)
{
    EXPECT_THAT(handler.getPubTocpicsNames(), Not(ElementsAre("sim/actio", "trgt/01/sens", "trgt/02/sensors")));
}

TEST_F(configTest, getSubTocpicsNames_1)
{
    EXPECT_THAT(handler.getSubTocpicsNames(), ElementsAre("sim/sensors", "trgt/01/actions", "trgt/02/actions"));
}

TEST_F(configTest, getSubTocpicsNames_2)
{
    EXPECT_THAT(handler.getPubTocpicsNames(), Not(ElementsAre("sim/actio", "trgt/01/sens", "trgt/02/sensors")));
}

TEST_F(configTest, getNumberOfRpis_1)
{
    int RPIS{6};
    EXPECT_NE(RPIS, handler.getNumberOfRpis());
}

TEST_F(configTest, getNumberOfRpis_2)
{
    int RPIS{2};
    EXPECT_EQ(RPIS, handler.getNumberOfRpis());
}

TEST_F(configTest, getQualityOfService_1)
{
    int QOS{1};
    EXPECT_EQ(QOS, handler.getQualityOfService());
}

TEST_F(configTest, getQualityOfService_2)
{
    int QOS{3};
    EXPECT_NE(QOS, handler.getQualityOfService());
}

TEST_F(configTest, getRetainedFlag_2)
{
    EXPECT_TRUE(handler.getRetainedFlag());
}
