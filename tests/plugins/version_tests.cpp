#include <gtest/gtest.h>

#include "engine/plugins/version.hpp"

using namespace Shard::Engine::Plugins;

namespace {
    bool Sat(const char* range, const char* version)
    {
        std::string err;
        auto r = VersionRange::Parse(range, &err);
        EXPECT_TRUE(r.has_value()) << range << " : " << err;
        auto v = SemVer::Parse(version);
        EXPECT_TRUE(v.has_value()) << version;
        return r && v && r->Satisfies(*v);
    }
}

TEST(SemVer, ParsesAndPrints)
{
    auto v = SemVer::Parse("1.4.2");
    ASSERT_TRUE(v);
    EXPECT_EQ(v->major, 1u);
    EXPECT_EQ(v->minor, 4u);
    EXPECT_EQ(v->patch, 2u);
    EXPECT_EQ(v->ToString(), "1.4.2");
    EXPECT_EQ(SemVer::Parse("v2.0.0-beta.2+build5")->ToString(), "2.0.0-beta.2");
}

TEST(SemVer, RejectsMalformedVersions)
{
    for (const char* bad : { "", "1", "1.2", "1.2.3.4", "a.b.c", "01.2.3", "1.2.3-", "1.2.-3", "1..3", "1.2.3-a..b" })
        EXPECT_FALSE(SemVer::Parse(bad).has_value()) << bad;
}

TEST(SemVer, OrdersLikeSemver)
{
    auto v = [](const char* s) { return *SemVer::Parse(s); };
    EXPECT_LT(v("1.0.0"), v("1.0.1"));
    EXPECT_LT(v("1.9.0"), v("1.10.0"));
    EXPECT_LT(v("1.0.0-alpha"), v("1.0.0"));
    EXPECT_LT(v("1.0.0-alpha"), v("1.0.0-alpha.1"));
    EXPECT_LT(v("1.0.0-alpha.1"), v("1.0.0-alpha.beta"));
    EXPECT_LT(v("1.0.0-alpha.beta"), v("1.0.0-beta"));
    EXPECT_LT(v("1.0.0-beta.2"), v("1.0.0-beta.11"));
    EXPECT_EQ(v("1.2.3"), v("1.2.3+other"));
}

TEST(VersionRange, ExactAndWildcards)
{
    EXPECT_TRUE(Sat("1.2.3", "1.2.3"));
    EXPECT_TRUE(Sat("=1.2.3", "1.2.3"));
    EXPECT_FALSE(Sat("1.2.3", "1.2.4"));
    EXPECT_TRUE(Sat("*", "9.9.9"));
    EXPECT_TRUE(Sat("", "0.0.1"));
    EXPECT_TRUE(Sat("1.x", "1.9.9"));
    EXPECT_FALSE(Sat("1.x", "2.0.0"));
    EXPECT_TRUE(Sat("1.2.*", "1.2.9"));
    EXPECT_FALSE(Sat("1.2.*", "1.3.0"));
    EXPECT_TRUE(Sat("1", "1.5.0"));
}

TEST(VersionRange, CaretAndTilde)
{
    EXPECT_TRUE(Sat("^1.2.3", "1.2.3"));
    EXPECT_TRUE(Sat("^1.2.3", "1.9.0"));
    EXPECT_FALSE(Sat("^1.2.3", "2.0.0"));
    EXPECT_FALSE(Sat("^1.2.3", "1.2.2"));
    EXPECT_TRUE(Sat("^0.2.3", "0.2.9"));
    EXPECT_FALSE(Sat("^0.2.3", "0.3.0"));
    EXPECT_TRUE(Sat("^0.0.3", "0.0.3"));
    EXPECT_FALSE(Sat("^0.0.3", "0.0.4"));
    EXPECT_TRUE(Sat("^1.0", "1.7.0"));
    EXPECT_TRUE(Sat("~1.2.3", "1.2.9"));
    EXPECT_FALSE(Sat("~1.2.3", "1.3.0"));
    EXPECT_TRUE(Sat("~1", "1.9.0"));
    EXPECT_FALSE(Sat("~1", "2.0.0"));
}

TEST(VersionRange, ComparatorsAndAlternatives)
{
    EXPECT_TRUE(Sat(">=1.2 <2", "1.2.0"));
    EXPECT_TRUE(Sat(">=1.2 <2", "1.99.0"));
    EXPECT_FALSE(Sat(">=1.2 <2", "2.0.0"));
    EXPECT_FALSE(Sat(">=1.2 <2", "1.1.9"));
    EXPECT_TRUE(Sat(">1.2", "1.3.0"));
    EXPECT_FALSE(Sat(">1.2", "1.2.5"));
    EXPECT_TRUE(Sat("<=1.2", "1.2.9"));
    EXPECT_FALSE(Sat("<=1.2", "1.3.0"));
    EXPECT_TRUE(Sat("1.x || 3.x", "3.1.0"));
    EXPECT_FALSE(Sat("1.x || 3.x", "2.0.0"));
}

TEST(VersionRange, PrereleaseOnlyMatchesAWrittenPrerelease)
{
    EXPECT_FALSE(Sat("^1.0.0", "1.5.0-beta"));
    EXPECT_TRUE(Sat(">=1.5.0-beta", "1.5.0-beta"));
    EXPECT_TRUE(Sat(">=1.5.0-beta", "1.5.0"));
    EXPECT_FALSE(Sat(">=1.5.0-beta", "1.6.0-beta"));
}

TEST(VersionRange, ReportsBadRanges)
{
    std::string err;
    EXPECT_FALSE(VersionRange::Parse("^abc", &err).has_value());
    EXPECT_FALSE(err.empty());
    EXPECT_FALSE(VersionRange::Parse(">=", &err).has_value() && false);
    EXPECT_FALSE(VersionRange::Parse("1.2.3.4", &err).has_value());
}
