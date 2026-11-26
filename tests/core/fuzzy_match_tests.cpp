#include <gtest/gtest.h>

#include "engine/core/text/fuzzy_match.hpp"

using namespace Shard::Engine::Core::Text;

TEST(FuzzyMatch, EmptyQueryMatchesEverything)
{
    EXPECT_TRUE(FuzzyMatch("", "anything").matched);
    EXPECT_TRUE(FuzzyMatch("   ", "anything").matched);
    EXPECT_EQ(FuzzyMatch("", "anything").score, 0);
}

TEST(FuzzyMatch, IsCaseInsensitive)
{
    EXPECT_TRUE(FuzzyMatch("BRICK", "brickwall").matched);
    EXPECT_TRUE(FuzzyMatch("brick", "BrickWall").matched);
}

TEST(FuzzyMatch, RejectsLettersThatAreNotThere)
{
    EXPECT_FALSE(FuzzyMatch("xyz", "BrickWall").matched);
    EXPECT_FALSE(FuzzyMatch("kcirb", "brick").matched);
    EXPECT_FALSE(FuzzyMatch("bricks", "brick").matched);
}

TEST(FuzzyMatch, SubsequenceMatches)
{
    EXPECT_TRUE(FuzzyMatch("bw", "BrickWall").matched);
    EXPECT_TRUE(FuzzyMatch("bkwl", "BrickWall").matched);
    EXPECT_FALSE(FuzzyMatch("wb", "Brick").matched);
}

TEST(FuzzyMatch, EveryTokenMustMatchInAnyOrder)
{
    EXPECT_TRUE(FuzzyMatch("wall brick", "BrickWall_02").matched);
    EXPECT_TRUE(FuzzyMatch("02 brick", "BrickWall_02").matched);
    EXPECT_FALSE(FuzzyMatch("wall stone", "BrickWall_02").matched);
}

TEST(FuzzyMatch, RanksExactThenPrefixThenWordStartThenSubstringThenSubsequence)
{
    const int exact = FuzzyMatch("wall", "Wall").score;
    const int prefix = FuzzyMatch("wall", "WallBrick").score;
    const int wordStart = FuzzyMatch("wall", "Brick_Wall").score;
    const int inside = FuzzyMatch("all", "Brick_Wall").score;
    const int subsequence = FuzzyMatch("bwl", "Brick_Wall").score;

    EXPECT_GT(exact, prefix);
    EXPECT_GT(prefix, wordStart);
    EXPECT_GT(wordStart, inside);
    EXPECT_GT(inside, subsequence);
    EXPECT_GT(subsequence, 0);
}

TEST(FuzzyMatch, ShorterCandidateRanksFirst)
{
    EXPECT_GT(FuzzyMatch("rock", "Rock").score, FuzzyMatch("rock", "Rock_Large_Mossy_Variant").score);
}

TEST(FuzzyMatch, CamelCaseCountsAsWordStarts)
{
    // "sm" hits S and M of StaticMesh, both word starts : better than the scattered letters of "assimilate".
    EXPECT_GT(FuzzyMatch("sm", "StaticMesh").score, FuzzyMatch("sm", "Assimilate").score);
}

TEST(FuzzyMatch, ConsecutiveLettersBeatScatteredOnes)
{
    EXPECT_GT(FuzzyMatch("brwl", "BrWall").score, FuzzyMatch("brwl", "BxxRxxxWxxxL").score);
}

TEST(FuzzyMatch, MatchAnyTakesTheBestField)
{
    std::vector<std::string_view> fields = { "Cube", "Mesh Renderer" };
    FuzzyResult r = FuzzyMatchAny("mesh", fields);
    EXPECT_TRUE(r.matched);
    EXPECT_GT(r.score, 0);

    EXPECT_FALSE(FuzzyMatchAny("zzz", fields).matched);

    // A weight below 100 demotes a field.
    FuzzyResult heavy = FuzzyMatchAny("cube", fields, { 100, 100 });
    FuzzyResult light = FuzzyMatchAny("cube", fields, { 50, 100 });
    EXPECT_GT(heavy.score, light.score);
}

TEST(FuzzyMatch, PositionsPointAtTheMatchedLetters)
{
    auto pos = FuzzyMatchPositions("wall", "Brick_Wall");
    ASSERT_EQ(pos.size(), 4u);
    EXPECT_EQ(pos[0], 6u);
    EXPECT_EQ(pos[3], 9u);

    auto sub = FuzzyMatchPositions("bw", "BrickWall");
    ASSERT_EQ(sub.size(), 2u);
    EXPECT_EQ(sub[0], 0u);
    EXPECT_EQ(sub[1], 5u);

    EXPECT_TRUE(FuzzyMatchPositions("zzz", "BrickWall").empty());
}
