#include <gtest/gtest.h>

#include <atomic>
#include <cstdio>
#include <fstream>
#include <thread>

#include "engine/core/config/cvar.hpp"

using namespace Shard::Engine::Core;

namespace {
    // Each test works on its own registry : the global one holds the engine's real variables.
    // CVar registers with Global(), so the tests that need isolation use unique names instead.
    CVarRegistry& Registry() { return CVarRegistry::Global(); }

    // A value set on a layer waits in the registry for a variable of that name (that is how a config file can be read before
    // the variable exists), and outlives the variable. A test that runs again (--gtest_repeat) would then find the previous
    // run's values : every test starts from empty layers.
    class CleanLayers : public ::testing::EmptyTestEventListener {
        void OnTestStart(const ::testing::TestInfo&) override {
            for (ConfigLayer layer : {ConfigLayer::Engine, ConfigLayer::Project, ConfigLayer::User, ConfigLayer::CommandLine, ConfigLayer::Runtime})
                CVarRegistry::Global().ClearLayer(layer);
        }
    };
    const bool g_cleanLayersInstalled = [] {
        ::testing::UnitTest::GetInstance()->listeners().Append(new CleanLayers);
        return true;
    }();
}

TEST(CVar, DefaultAndSet) {
    CVar<int> v("test.basic.int", 5, "an int", CVarFlags::None, 0, 10);
    EXPECT_EQ(v.Get(), 5);
    EXPECT_EQ(int(v), 5);
    EXPECT_TRUE(v.Set(7));
    EXPECT_EQ(v.Get(), 7);
    EXPECT_FALSE(v.IsDefault());
    EXPECT_EQ(v.ToString(), "7");
    EXPECT_EQ(v.DefaultString(), "5");
}

TEST(CVar, NumericValuesAreClampedToTheirRange) {
    CVar<int> i("test.clamp.int", 5, "", CVarFlags::None, 0, 10);
    i.Set(100);
    EXPECT_EQ(i.Get(), 10);
    i.Set(-100);
    EXPECT_EQ(i.Get(), 0);
    CVar<float> f("test.clamp.float", 0.5f, "", CVarFlags::None, 0.0f, 1.0f);
    f.Set(3.0f);
    EXPECT_FLOAT_EQ(f.Get(), 1.0f);
}

TEST(CVar, AllTypes) {
    CVar<bool> b("test.types.bool", false);
    CVar<float> f("test.types.float", 1.5f);
    CVar<std::string> s("test.types.string", "hello");
    EXPECT_EQ(b.ToString(), "false");
    EXPECT_EQ(f.ToString(), "1.5");
    EXPECT_EQ(s.ToString(), "hello");
    EXPECT_STREQ(b.TypeName(), "bool");
    EXPECT_STREQ(f.TypeName(), "float");
    EXPECT_STREQ(s.TypeName(), "string");

    EXPECT_TRUE(Registry().Execute("test.types.bool on").ok);
    EXPECT_TRUE(b.Get());
    EXPECT_TRUE(Registry().Execute("test.types.float 2.25").ok);
    EXPECT_FLOAT_EQ(f.Get(), 2.25f);
    EXPECT_TRUE(Registry().Execute("test.types.string \"two words\"").ok);
    EXPECT_EQ(s.Get(), "two words");
}

TEST(CVar, InvalidTextIsRejectedAndKeepsTheValue) {
    CVar<int> v("test.invalid.int", 3);
    auto r = Registry().Execute("test.invalid.int banana");
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.output.find("banana"), std::string::npos);
    EXPECT_EQ(v.Get(), 3);
    EXPECT_EQ(Registry().GetLayerValue(ConfigLayer::Runtime, "test.invalid.int"), std::nullopt);   // the bad value was not kept

    CVar<bool> b("test.invalid.bool", true);
    EXPECT_FALSE(Registry().Execute("test.invalid.bool maybe").ok);
    EXPECT_TRUE(b.Get());
}

TEST(CVar, NamesAreCaseInsensitive) {
    CVar<int> v("Test.Case.Mixed", 1);
    EXPECT_NE(Registry().Find("test.case.mixed"), nullptr);
    EXPECT_NE(Registry().Find("TEST.CASE.MIXED"), nullptr);
    EXPECT_TRUE(Registry().Execute("TEST.case.MIXED 9").ok);
    EXPECT_EQ(v.Get(), 9);
}

TEST(CVar, ReadOnlyRefusesRuntimeChangesButNotConfigOrOwner) {
    CVar<int> v("test.readonly", 1, "", CVarFlags::ReadOnly);
    EXPECT_FALSE(v.Set(2));
    EXPECT_EQ(v.Get(), 1);
    EXPECT_FALSE(Registry().Execute("test.readonly 2").ok);

    EXPECT_TRUE(Registry().SetLayerValue(ConfigLayer::Engine, "test.readonly", "3"));   // a config file may set it
    EXPECT_EQ(v.Get(), 3);
    v.ForceSet(4);                                                                       // so may the owner
    EXPECT_EQ(v.Get(), 4);
}

TEST(CVar, ChangeCallbackFiresOnRealChangesOnly) {
    CVar<int> v("test.callback", 0);
    int calls = 0, last = -1;
    v.AddOnChange([&](const CVarBase& base) { ++calls; last = std::stoi(base.ToString()); });
    v.Set(5);
    v.Set(5);                                                                            // same value : no call
    v.Set(8);
    EXPECT_EQ(calls, 2);
    EXPECT_EQ(last, 8);
}

TEST(CVar, ResetGoesBackToTheLowerLayers) {
    CVar<int> v("test.reset", 1);
    Registry().SetLayerValue(ConfigLayer::Project, "test.reset", "20");
    EXPECT_EQ(v.Get(), 20);
    v.Set(99);
    EXPECT_EQ(v.Get(), 99);
    EXPECT_TRUE(Registry().Reset("test.reset"));
    EXPECT_EQ(v.Get(), 20);                                                              // the project value again
    Registry().ClearLayerValue(ConfigLayer::Project, "test.reset");
    EXPECT_EQ(v.Get(), 1);                                                               // the default
}

TEST(Layers, HigherLayersWin) {
    CVar<int> v("test.layers", 0);
    Registry().SetLayerValue(ConfigLayer::Engine, "test.layers", "1");
    EXPECT_EQ(v.Get(), 1);
    Registry().SetLayerValue(ConfigLayer::Project, "test.layers", "2");
    EXPECT_EQ(v.Get(), 2);
    Registry().SetLayerValue(ConfigLayer::User, "test.layers", "3");
    EXPECT_EQ(v.Get(), 3);
    Registry().SetLayerValue(ConfigLayer::CommandLine, "test.layers", "4");
    EXPECT_EQ(v.Get(), 4);
    Registry().SetLayerValue(ConfigLayer::Runtime, "test.layers", "5");
    EXPECT_EQ(v.Get(), 5);
    EXPECT_EQ(Registry().WinningLayer("test.layers"), ConfigLayer::Runtime);

    // taking the top layers away reveals the ones below
    Registry().ClearLayerValue(ConfigLayer::Runtime, "test.layers");
    EXPECT_EQ(v.Get(), 4);
    Registry().ClearLayer(ConfigLayer::CommandLine);
    EXPECT_EQ(v.Get(), 3);
    EXPECT_EQ(Registry().WinningLayer("test.layers"), ConfigLayer::User);
    Registry().ClearLayer(ConfigLayer::User);
    Registry().ClearLayer(ConfigLayer::Project);
    Registry().ClearLayer(ConfigLayer::Engine);
    EXPECT_EQ(v.Get(), 0);
    EXPECT_EQ(Registry().WinningLayer("test.layers"), ConfigLayer::Default);
}

TEST(Layers, ValueSetBeforeTheVariableExistsIsAppliedOnRegistration) {
    EXPECT_TRUE(Registry().SetLayerValue(ConfigLayer::Engine, "test.late.registration", "42"));
    EXPECT_EQ(Registry().Find("test.late.registration"), nullptr);
    CVar<int> v("test.late.registration", 1);
    EXPECT_EQ(v.Get(), 42);
}

TEST(ConfigText, ParsesKeysSectionsCommentsAndQuotes) {
    CVar<int> a("test.cfg.a", 0);
    CVar<std::string> b("test.cfg.b", "");
    CVar<bool> c("test.cfg.c", false);
    std::vector<std::string> warnings;
    const size_t applied = Registry().LoadText(ConfigLayer::Project,
        "# a comment\n"
        "; another\n"
        "\n"
        "test.cfg.a = 12\n"
        "[test.cfg]\n"
        "b = \"quoted value\"\n"
        "c = yes\n"
        "this line is wrong\n"
        "a = notanumber\n", &warnings);
    EXPECT_EQ(applied, 3u);
    EXPECT_EQ(a.Get(), 12);                           // the bad 'a = notanumber' did not replace it
    EXPECT_EQ(b.Get(), "quoted value");
    EXPECT_TRUE(c.Get());
    EXPECT_EQ(warnings.size(), 2u);
    Registry().ClearLayer(ConfigLayer::Project);
}

TEST(ConfigFiles, SaveArchiveAndReload) {
    CVar<int> archived("test.save.archived", 5, "", CVarFlags::Archive);
    CVar<int> untouched("test.save.untouched", 5, "", CVarFlags::Archive);
    CVar<int> notArchived("test.save.plain", 5);
    archived.Set(77);
    notArchived.Set(88);

    const std::string path = std::string(::testing::TempDir()) + "shard_cvar_test.cfg";
    auto saved = Registry().SaveArchive(path);
    ASSERT_TRUE(saved);

    std::ifstream in(path);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("test.save.archived = 77"), std::string::npos);
    EXPECT_EQ(content.find("test.save.untouched"), std::string::npos);     // still the default : not saved
    EXPECT_EQ(content.find("test.save.plain"), std::string::npos);         // not an Archive variable

    Registry().Reset("test.save.archived");
    EXPECT_EQ(archived.Get(), 5);
    auto loaded = Registry().LoadFile(ConfigLayer::User, path);
    ASSERT_TRUE(loaded);
    EXPECT_EQ(archived.Get(), 77);
    Registry().ClearLayer(ConfigLayer::User);
    std::remove(path.c_str());
}

TEST(ConfigFiles, MissingFileIsAnError) {
    auto r = Registry().LoadFile(ConfigLayer::Engine, "/definitely/not/here.cfg");
    EXPECT_FALSE(r);
    EXPECT_NE(r.GetError().message.find("here.cfg"), std::string::npos);
}

TEST(CommandLine, PlusSyntax) {
    CVar<int> a("test.cmd.a", 0);
    CVar<std::string> b("test.cmd.b", "");
    const char* argv[] = {"program", "--other", "+test.cmd.a=15", "+test.cmd.b", "some text", "positional"};
    std::vector<std::string> warnings;
    EXPECT_EQ(Registry().ParseCommandLine(6, argv, &warnings), 2u);
    EXPECT_EQ(a.Get(), 15);
    EXPECT_EQ(b.Get(), "some text");
    EXPECT_EQ(Registry().WinningLayer("test.cmd.a"), ConfigLayer::CommandLine);
    Registry().ClearLayer(ConfigLayer::CommandLine);
}

TEST(Console, TokenizerHonoursQuotes) {
    auto t = CVarRegistry::Tokenize("set  name \"a b  c\" tail");
    ASSERT_EQ(t.size(), 4u);
    EXPECT_EQ(t[2], "a b  c");
    EXPECT_TRUE(CVarRegistry::Tokenize("   ").empty());
    EXPECT_EQ(CVarRegistry::Tokenize("\"\"").size(), 1u);                   // an empty quoted string is a token
}

TEST(Console, BuiltinCommands) {
    CVar<int> v("test.console.value", 3, "describes the value");

    auto show = Registry().Execute("test.console.value");
    EXPECT_TRUE(show.ok);
    EXPECT_NE(show.output.find("= 3"), std::string::npos);
    EXPECT_NE(show.output.find("describes the value"), std::string::npos);

    EXPECT_TRUE(Registry().Execute("set test.console.value 9").ok);
    EXPECT_EQ(v.Get(), 9);
    EXPECT_NE(Registry().Execute("get test.console.value").output.find("9"), std::string::npos);
    EXPECT_TRUE(Registry().Execute("reset test.console.value").ok);
    EXPECT_EQ(v.Get(), 3);

    CVar<bool> flag("test.console.flag", false);
    EXPECT_TRUE(Registry().Execute("toggle test.console.flag").ok);
    EXPECT_TRUE(flag.Get());
    EXPECT_FALSE(Registry().Execute("toggle test.console.value").ok);        // not a bool

    EXPECT_EQ(Registry().Execute("echo hello world").output, "hello world");
    EXPECT_NE(Registry().Execute("list test.console").output.find("test.console.value"), std::string::npos);
    EXPECT_NE(Registry().Execute("help").output.find("set name value"), std::string::npos);
    EXPECT_FALSE(Registry().Execute("no_such_thing").ok);
    EXPECT_FALSE(Registry().Execute("set").ok);
    EXPECT_TRUE(Registry().Execute("").ok);
}

TEST(Console, CustomCommands) {
    int calls = 0;
    std::vector<std::string> received;
    Registry().RegisterCommand("test_cmd", "test_cmd a b : records", [&](const std::vector<std::string>& args) {
        ++calls;
        received = args;
        return CommandResult{true, "done"};
    });
    auto r = Registry().Execute("TEST_CMD one \"two words\"");
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.output, "done");
    EXPECT_EQ(calls, 1);
    ASSERT_EQ(received.size(), 2u);
    EXPECT_EQ(received[1], "two words");
    Registry().UnregisterCommand("test_cmd");
    EXPECT_FALSE(Registry().Execute("test_cmd").ok);
}

TEST(Console, CompletionListsVariablesAndCommands) {
    CVar<int> a("test.complete.alpha", 0);
    CVar<int> b("test.complete.beta", 0);
    CVar<int> hidden("test.complete.hidden", 0, "", CVarFlags::Hidden);
    auto names = Registry().Complete("test.complete.");
    EXPECT_EQ(names.size(), 2u);                                             // hidden ones are not suggested
    EXPECT_NE(Registry().Find("test.complete.hidden"), nullptr);             // but they work
    auto commands = Registry().Complete("hel");
    EXPECT_NE(std::find(commands.begin(), commands.end(), "help"), commands.end());
}

TEST(Registry, ThreadSafeReadsWhileChanging) {
    CVar<int> v("test.threads", 0, "", CVarFlags::None, 0, 1000000);
    std::atomic<bool> stop{false};
    std::atomic<int> maxSeen{0};
    std::thread reader([&] { while (!stop) maxSeen = std::max(maxSeen.load(), v.Get()); });
    for (int i = 1; i <= 1000; ++i) v.Set(i);
    stop = true;
    reader.join();
    EXPECT_EQ(v.Get(), 1000);
    EXPECT_LE(maxSeen.load(), 1000);
}

TEST(Registry, VariableUnregistersOnDestruction) {
    {
        CVar<int> v("test.scoped", 1);
        EXPECT_NE(Registry().Find("test.scoped"), nullptr);
    }
    EXPECT_EQ(Registry().Find("test.scoped"), nullptr);
}

TEST(ConfigFiles, OnlyTheUsersChoicesAreSaved) {
    CVar<int> fromCommandLine("test.archive.cmd", 1, "", CVarFlags::Archive);
    CVar<int> fromProject("test.archive.project", 1, "", CVarFlags::Archive);
    CVar<int> fromConsole("test.archive.console", 1, "", CVarFlags::Archive);
    CVar<int> fromUserFile("test.archive.user", 1, "", CVarFlags::Archive);
    Registry().SetLayerValue(ConfigLayer::CommandLine, "test.archive.cmd", "2");
    Registry().SetLayerValue(ConfigLayer::Project, "test.archive.project", "2");
    Registry().SetLayerValue(ConfigLayer::User, "test.archive.user", "2");
    fromConsole.Set(2);

    const std::string text = Registry().ArchiveText();
    EXPECT_EQ(text.find("test.archive.cmd"), std::string::npos);
    EXPECT_EQ(text.find("test.archive.project"), std::string::npos);
    EXPECT_NE(text.find("test.archive.console = 2"), std::string::npos);
    EXPECT_NE(text.find("test.archive.user = 2"), std::string::npos);

    Registry().ClearLayer(ConfigLayer::CommandLine);
    Registry().ClearLayer(ConfigLayer::Project);
    Registry().ClearLayer(ConfigLayer::User);
}
