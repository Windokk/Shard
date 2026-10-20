#include <gtest/gtest.h>

#include "engine/core/tags/tag.hpp"
#include "engine/core/tags/tag_container.hpp"
#include "engine/core/tags/tag_query.hpp"
#include "engine/core/tags/tag_registry.hpp"

using namespace Shard::Engine::Core;

namespace {
    SH_DECLARE_TAG(TagFromCode, "Tests.Native.Declared");
}

// The registry is the process's : each test starts from an empty tree with the project's tags of the example below
class TagTest : public ::testing::Test {
    protected:
        void SetUp() override {
            Registry().Clear();
            Registry().SetDeclarations(TagSource::Project, "", {
                { "Enemy.Undead.Skeleton", "Breaks into bones" },
                { "Enemy.Undead.Ghost", "" },
                { "Enemy.Boss", "" },
                { "Damage.Fire", "" },
                { "Status.Dead", "" },
                { "Status.Burning", "" },
            });
        }

        void TearDown() override {
            Registry().Clear();
        }

        static TagRegistry& Registry() { return TagRegistry::Get(); }
        static Tag T(const char* name) { return Tag::Request(name); }
};

// ---- Names ----

TEST_F(TagTest, ValidNamesAreDottedSegments) {
    EXPECT_TRUE(TagRegistry::IsValidName("Enemy"));
    EXPECT_TRUE(TagRegistry::IsValidName("Enemy.Undead_2.Skeleton"));

    EXPECT_FALSE(TagRegistry::IsValidName(""));
    EXPECT_FALSE(TagRegistry::IsValidName(".Enemy"));
    EXPECT_FALSE(TagRegistry::IsValidName("Enemy."));
    EXPECT_FALSE(TagRegistry::IsValidName("Enemy..Boss"));
    EXPECT_FALSE(TagRegistry::IsValidName("Enemy Boss"));
    EXPECT_FALSE(TagRegistry::IsValidName("Enemy-Boss"));
    EXPECT_TRUE(TagRegistry::IsValidName("A.B.C.D.E.F.G.H"));
    EXPECT_FALSE(TagRegistry::IsValidName("A.B.C.D.E.F.G.H.I"));
}

TEST_F(TagTest, NamesAreCaseSensitive) {
    EXPECT_TRUE(T("Enemy").IsValid());
    EXPECT_FALSE(T("enemy").IsValid());
}

// ---- The tree ----

TEST_F(TagTest, DeclaringATagDeclaresItsParents) {
    const Tag skeleton = T("Enemy.Undead.Skeleton");
    ASSERT_TRUE(skeleton.IsValid());

    EXPECT_EQ(skeleton.GetParent(), T("Enemy.Undead"));
    EXPECT_EQ(skeleton.GetParent().GetParent(), T("Enemy"));
    EXPECT_FALSE(T("Enemy").GetParent().IsValid());

    const TagInfo* parent = Registry().GetInfo(T("Enemy.Undead"));
    ASSERT_NE(parent, nullptr);
    EXPECT_TRUE(parent->declarations.empty());
    ASSERT_EQ(parent->children.size(), 2u);
    EXPECT_EQ(parent->children[0], T("Enemy.Undead.Ghost"));      // sorted by name
    EXPECT_EQ(parent->children[1], T("Enemy.Undead.Skeleton"));
    EXPECT_EQ(Registry().GetInfo(skeleton)->Comment(), "Breaks into bones");
}

TEST_F(TagTest, ChildOfIsStrict) {
    EXPECT_TRUE(T("Enemy.Undead.Skeleton").IsChildOf(T("Enemy")));
    EXPECT_TRUE(T("Enemy.Undead.Skeleton").IsChildOf(T("Enemy.Undead")));
    EXPECT_FALSE(T("Enemy").IsChildOf(T("Enemy")));
    EXPECT_FALSE(T("Enemy").IsChildOf(T("Enemy.Undead")));
    EXPECT_FALSE(T("Enemy.Boss").IsChildOf(T("Enemy.Undead")));
    EXPECT_TRUE(T("Enemy").Matches(T("Enemy")));
}

TEST_F(TagTest, RootsAndDescendantsAreSortedByName) {
    const std::vector<Tag> roots = Registry().GetRoots();
    ASSERT_EQ(roots.size(), 3u);
    EXPECT_EQ(roots[0].ToString(), "Damage");
    EXPECT_EQ(roots[1].ToString(), "Enemy");
    EXPECT_EQ(roots[2].ToString(), "Status");

    std::vector<Tag> under;
    Registry().CollectDescendants(T("Enemy"), under);
    std::vector<std::string> names;
    for (Tag t : under)
        names.push_back(t.ToString());
    EXPECT_EQ(names, (std::vector<std::string>{ "Enemy", "Enemy.Boss", "Enemy.Undead", "Enemy.Undead.Ghost", "Enemy.Undead.Skeleton" }));
}

TEST_F(TagTest, AnUnknownNameIsNotRequested) {
    EXPECT_FALSE(T("Enemy.Dragon").IsValid());
    EXPECT_FALSE(Tag::Request("").IsValid());
}

TEST_F(TagTest, SourcesAreMergedAndRemovedSeparately) {
    Registry().Declare("Weather.Rain", TagSource::Plugin, "com.acme.weather");
    Registry().Declare("Enemy.Boss", TagSource::Plugin, "com.acme.weather");
    EXPECT_TRUE(T("Weather.Rain").IsValid());
    EXPECT_EQ(Registry().GetInfo(T("Enemy.Boss"))->Sources(), uint8_t(TagSource::Project) | uint8_t(TagSource::Plugin));

    Registry().RemoveDeclarations(TagSource::Plugin, "com.acme.weather");
    EXPECT_FALSE(T("Weather.Rain").IsValid());
    EXPECT_FALSE(T("Weather").IsValid());
    EXPECT_EQ(Registry().GetInfo(T("Enemy.Boss"))->Sources(), uint8_t(TagSource::Project));
}

TEST_F(TagTest, ReplacingTheProjectTagsKeepsTheOthers) {
    Registry().Declare("Weather.Rain", TagSource::Plugin, "com.acme.weather");
    Registry().SetDeclarations(TagSource::Project, "", { { "Pickup.Health", "" } });

    EXPECT_FALSE(T("Enemy").IsValid());
    EXPECT_TRUE(T("Pickup.Health").IsValid());
    EXPECT_TRUE(T("Weather.Rain").IsValid());
}

TEST_F(TagTest, TheVersionChangesWithTheTree) {
    const uint64_t before = Registry().GetVersion();
    Registry().Declare("Pickup", TagSource::Project);
    EXPECT_NE(Registry().GetVersion(), before);
}

TEST_F(TagTest, CodeDeclarationsSurviveAsLongAsTheyAreDeclared) {
    // Clear() in SetUp forgot it : declared again like the static initialization did
    EXPECT_EQ(DeclareNativeTag("Tests.Native.Declared"), TagFromCode);
    EXPECT_TRUE(Tag::Request("Tests.Native.Declared").IsValid());
    EXPECT_EQ(Registry().GetInfo(TagFromCode)->Sources(), uint8_t(TagSource::Code));
}

// ---- Redirects and files ----

TEST_F(TagTest, RedirectsAreFollowed) {
    Registry().SetRedirects({ { "Enemy.Undead.Skelly", "Enemy.Undead.Bones" }, { "Enemy.Undead.Bones", "Enemy.Undead.Skeleton" } });

    EXPECT_EQ(T("Enemy.Undead.Skelly"), T("Enemy.Undead.Skeleton"));
    EXPECT_EQ(Registry().Resolve("Enemy.Undead.Skelly"), T("Enemy.Undead.Skeleton"));
}

TEST_F(TagTest, ARedirectLoopEnds) {
    Registry().SetRedirects({ { "A", "B" }, { "B", "A" } });
    const std::string name = Registry().ApplyRedirects("A");
    EXPECT_TRUE(name == "A" || name == "B");
}

TEST_F(TagTest, AnUnknownNameFromAFileIsKeptAndMatchesOnlyItself) {
    const Tag dragon = Registry().Resolve("Enemy.Dragon");
    ASSERT_TRUE(dragon.IsValid());
    EXPECT_FALSE(Registry().IsKnown(dragon));
    EXPECT_EQ(dragon.ToString(), "Enemy.Dragon");
    EXPECT_FALSE(dragon.IsChildOf(T("Enemy")));

    TagContainer tags{ dragon };
    EXPECT_TRUE(tags.HasTag(dragon));
    EXPECT_FALSE(tags.HasTag(T("Enemy")));
}

TEST_F(TagTest, AnInvalidNameFromAFileIsDropped) {
    EXPECT_FALSE(Registry().Resolve("not a tag").IsValid());
}

TEST_F(TagTest, ARemovedTagKeepsItsName) {
    const Tag ghost = T("Enemy.Undead.Ghost");
    Registry().SetDeclarations(TagSource::Project, "", {});
    EXPECT_EQ(ghost.ToString(), "Enemy.Undead.Ghost");
}

// ---- Containers ----

TEST_F(TagTest, HasTagIncludesTheParents) {
    TagContainer tags{ T("Enemy.Undead.Skeleton") };

    EXPECT_TRUE(tags.HasTag(T("Enemy")));
    EXPECT_TRUE(tags.HasTag(T("Enemy.Undead")));
    EXPECT_TRUE(tags.HasTag(T("Enemy.Undead.Skeleton")));
    EXPECT_FALSE(tags.HasTag(T("Enemy.Boss")));
    EXPECT_FALSE(tags.HasTag(Tag()));

    EXPECT_TRUE(tags.HasTagExact(T("Enemy.Undead.Skeleton")));
    EXPECT_FALSE(tags.HasTagExact(T("Enemy")));
}

TEST_F(TagTest, MatchingGoesOneWay) {
    TagContainer tags{ T("Enemy") };
    EXPECT_FALSE(tags.HasTag(T("Enemy.Undead.Skeleton")));
}

TEST_F(TagTest, AnyAndAll) {
    TagContainer tags{ T("Enemy.Boss"), T("Status.Burning") };

    EXPECT_TRUE(tags.HasAny(TagContainer{ T("Damage"), T("Enemy") }));
    EXPECT_FALSE(tags.HasAny(TagContainer{ T("Damage"), T("Status.Dead") }));
    EXPECT_FALSE(tags.HasAny(TagContainer()));

    EXPECT_TRUE(tags.HasAll(TagContainer{ T("Enemy"), T("Status") }));
    EXPECT_FALSE(tags.HasAll(TagContainer{ T("Enemy"), T("Damage") }));
    EXPECT_TRUE(tags.HasAll(TagContainer()));

    EXPECT_FALSE(tags.HasAnyExact(TagContainer{ T("Enemy") }));
    EXPECT_TRUE(tags.HasAllExact(TagContainer{ T("Enemy.Boss") }));
}

TEST_F(TagTest, AddAndRemoveOnlyTouchExplicitTags) {
    TagContainer tags;
    EXPECT_TRUE(tags.Add(T("Enemy.Undead")));
    EXPECT_FALSE(tags.Add(T("Enemy.Undead")));
    EXPECT_FALSE(tags.Add(Tag()));
    EXPECT_FALSE(tags.Remove(T("Enemy")));
    EXPECT_EQ(tags.Size(), 1u);
    EXPECT_TRUE(tags.Remove(T("Enemy.Undead")));
    EXPECT_TRUE(tags.IsEmpty());
}

TEST_F(TagTest, RemoveWithChildren) {
    TagContainer tags{ T("Enemy.Boss"), T("Enemy.Undead.Ghost"), T("Damage.Fire") };
    EXPECT_EQ(tags.RemoveWithChildren(T("Enemy")), 2u);
    EXPECT_EQ(tags, (TagContainer{ T("Damage.Fire") }));
}

TEST_F(TagTest, NamesAreWrittenSorted) {
    TagContainer tags{ T("Status.Burning"), T("Enemy.Boss"), T("Damage.Fire") };
    EXPECT_EQ(tags.ToStrings(), (std::vector<std::string>{ "Damage.Fire", "Enemy.Boss", "Status.Burning" }));
    EXPECT_EQ(tags.ToString(), "Damage.Fire, Enemy.Boss, Status.Burning");
}

namespace {
    struct RecordingListener : TagContainer::Listener {
        int calls = 0;
        TagContainer added, removed;
        bool quiet = false;
        void OnTagsChanged(const TagContainer& a, const TagContainer& r, bool q) override {
            ++calls; added = a; removed = r; quiet = q;
        }
    };
}

TEST_F(TagTest, TheListenerHearsWhatChanged) {
    RecordingListener listener;
    TagContainer tags;
    tags.SetListener(&listener);

    tags.Add(T("Enemy.Boss"));
    EXPECT_EQ(listener.calls, 1);
    EXPECT_EQ(listener.added, (TagContainer{ T("Enemy.Boss") }));

    tags.Add(T("Enemy.Boss"));
    EXPECT_EQ(listener.calls, 1);          // nothing changed

    tags.Assign(TagContainer{ T("Damage.Fire") }, true);
    EXPECT_EQ(listener.calls, 2);
    EXPECT_EQ(listener.added, (TagContainer{ T("Damage.Fire") }));
    EXPECT_EQ(listener.removed, (TagContainer{ T("Enemy.Boss") }));
    EXPECT_TRUE(listener.quiet);
}

TEST_F(TagTest, ACopyDoesNotTakeTheListener) {
    RecordingListener listener;
    TagContainer owned;
    owned.SetListener(&listener);

    TagContainer copy(owned);
    copy.Add(T("Enemy"));
    EXPECT_EQ(listener.calls, 0);

    owned = copy;                           // assigning into an owned container is a change of it
    EXPECT_EQ(listener.calls, 1);
}

// ---- Queries ----

TEST_F(TagTest, QueryTextIsParsedAndMatched) {
    const TagQuery hostile = TagQuery::Parse("Enemy & !(Enemy.Boss | Status.Dead)");

    EXPECT_TRUE(hostile.Matches(TagContainer{ T("Enemy.Undead.Skeleton") }));
    EXPECT_FALSE(hostile.Matches(TagContainer{ T("Enemy.Boss") }));
    EXPECT_FALSE(hostile.Matches(TagContainer{ T("Enemy.Undead.Ghost"), T("Status.Dead") }));
    EXPECT_FALSE(hostile.Matches(TagContainer{ T("Damage.Fire") }));
}

TEST_F(TagTest, OrIsLooserThanAnd) {
    const TagQuery q = TagQuery::Parse("Damage | Enemy & Status.Burning");
    EXPECT_TRUE(q.Matches(TagContainer{ T("Damage.Fire") }));
    EXPECT_FALSE(q.Matches(TagContainer{ T("Enemy.Boss") }));
    EXPECT_TRUE(q.Matches(TagContainer{ T("Enemy.Boss"), T("Status.Burning") }));
}

TEST_F(TagTest, AnEqualSignIsExact) {
    const TagQuery q = TagQuery::Parse("=Enemy");
    EXPECT_TRUE(q.Matches(TagContainer{ T("Enemy") }));
    EXPECT_FALSE(q.Matches(TagContainer{ T("Enemy.Boss") }));
}

TEST_F(TagTest, TheEmptyQueryMatchesEverything) {
    EXPECT_TRUE(TagQuery().Matches(TagContainer()));
    EXPECT_TRUE(TagQuery::Parse("  ").Matches(TagContainer{ T("Enemy") }));
    EXPECT_TRUE(TagQuery::Parse("  ").IsEmpty());
}

TEST_F(TagTest, InvalidQueriesAreRefused) {
    TagQuery q;
    std::string error;
    for (const char* text : { "Enemy &", "& Enemy", "(Enemy", "Enemy)", "Enemy Boss", "Enemy..Boss", "!", "Enemy | | Boss" }) {
        EXPECT_FALSE(TagQuery::TryParse(text, q, &error)) << text;
        EXPECT_FALSE(error.empty()) << text;
    }
    EXPECT_TRUE(TagQuery::Parse("Enemy &").IsEmpty());
}

TEST_F(TagTest, QueryTextRoundtrips) {
    for (const char* text : { "Enemy", "=Enemy", "!Enemy", "Enemy & Status.Burning", "Enemy | Damage",
                              "Enemy & !(Enemy.Boss | Status.Dead)", "(Enemy | Damage) & !Status", "()" }) {
        const TagQuery q = TagQuery::Parse(text);
        EXPECT_EQ(q.ToString(), text);
        EXPECT_EQ(TagQuery::Parse(q.ToString()), q) << text;
    }
    EXPECT_EQ(TagQuery::Parse("(Enemy)&((Damage))").ToString(), "Enemy & Damage");
    EXPECT_EQ(TagQuery::Parse("!!Enemy").ToString(), "Enemy");
}

TEST_F(TagTest, TheBuildersMatchTheText) {
    const TagQuery built = TagQuery::All({ "Enemy" }).And(TagQuery::None({ "Enemy.Boss", "Status.Dead" }));
    EXPECT_EQ(built.ToString(), "Enemy & !(Enemy.Boss | Status.Dead)");
    EXPECT_EQ(built, TagQuery::Parse("Enemy & !(Enemy.Boss | Status.Dead)"));

    EXPECT_FALSE(TagQuery::Any(TagContainer()).Matches(TagContainer{ T("Enemy") }));
    EXPECT_TRUE(TagQuery::All(TagContainer()).Matches(TagContainer()));
    EXPECT_FALSE(TagQuery::Has(Tag()).Matches(TagContainer{ T("Enemy") }));
}

TEST_F(TagTest, AQueryWithAnUnknownNameMatchesNothingOfIt) {
    const TagQuery q = TagQuery::All({ "Enemy.Dragon" });
    EXPECT_FALSE(q.IsEmpty());
    EXPECT_FALSE(q.Matches(TagContainer{ T("Enemy.Boss") }));
    EXPECT_EQ(q.ToString(), "Enemy.Dragon");
}

TEST_F(TagTest, TheRequiredTagIsWhatAnIndexStartsFrom) {
    EXPECT_EQ(TagQuery::Parse("Enemy").GetRequiredTag(), T("Enemy"));
    EXPECT_EQ(TagQuery::Parse("!Status.Dead & Enemy").GetRequiredTag(), T("Enemy"));
    EXPECT_FALSE(TagQuery::Parse("Enemy | Damage").GetRequiredTag().IsValid());
    EXPECT_FALSE(TagQuery::Parse("=Enemy").GetRequiredTag().IsValid());
    EXPECT_FALSE(TagQuery().GetRequiredTag().IsValid());
}
