#include <gtest/gtest.h>

#include "engine/platform/replay/input_replay.hpp"

using Shard::Engine::Core::Platform::ReplayData;

TEST(ReplayPlugins, PluginLinesRoundTrip)
{
    ReplayData data;
    data.seed = 7;
    data.frames = 10;
    data.plugins = { "com.acme.weather 1.4.2 00000000000000ab", "game 0.0.0 0000000000000000" };
    ReplayData::Check check;
    check.frames = 10;
    check.hash = 0x1234;
    data.checks.push_back(check);

    const std::string text = data.Serialize();
    EXPECT_NE(text.find("plugin com.acme.weather 1.4.2 00000000000000ab"), std::string::npos);

    ReplayData back;
    std::string error;
    ASSERT_TRUE(ReplayData::Parse(text, back, error)) << error;
    EXPECT_EQ(back.plugins, data.plugins);
    EXPECT_EQ(back.seed, 7u);
    EXPECT_EQ(back.checks.size(), 1u);
}

TEST(ReplayPlugins, AReplayWithoutPluginLinesStillReads)
{
    ReplayData back;
    std::string error;
    ASSERT_TRUE(ReplayData::Parse("shard-replay 1\nseed 1\nframes 5\n", back, error)) << error;
    EXPECT_TRUE(back.plugins.empty());
}

TEST(ReplayPlugins, ABadPluginLineIsReported)
{
    ReplayData back;
    std::string error;
    EXPECT_FALSE(ReplayData::Parse("shard-replay 1\nplugin only-two fields\n", back, error));
    EXPECT_NE(error.find("plugin"), std::string::npos);
}
