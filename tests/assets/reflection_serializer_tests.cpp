#include <gtest/gtest.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "engine/assets/serialization/reflection/reflection_serializer.hpp"

using namespace Shard::Engine;
using nlohmann::json;
using nlohmann::ordered_json;

namespace {

    enum class Mode : int { Off = 0, On = 1, Auto = 2 };

    struct Sample {
        float speed = 1.0f;
        bool flag = false;
        int count = 3;
        std::string label = "default";
        glm::vec3 position = glm::vec3(0.0f);
        glm::quat rotation = glm::quat(glm::vec3(0.0f));
        Mode mode = Mode::Off;
        std::vector<float> weights;
        float runtime = 5.0f;
    };

    EnumDescriptor ModeDescriptor = {
        "Mode",
        { { 0, "Off" }, { 1, "On" }, { 2, "Auto" } },
        sizeof(Mode)
    };

    Container WeightsContainer = MakeVectorContainer<float>();

    template<typename T>
    FieldInfo MakeField(const char* name, TypeID type, uint32_t offset, uint32_t flags = Editable)
    {
        return FieldInfo{ name, type, offset, flags, 0, 0, nullptr, nullptr, &CopyConstruct<T>, &Assign<T>, &Destroy<T>, nullptr };
    }

    struct SampleFields {
        FieldInfo speed    = MakeField<float>("speed", TypeID::Float, offsetof(Sample, speed));
        FieldInfo flag     = MakeField<bool>("flag", TypeID::Bool, offsetof(Sample, flag));
        FieldInfo count    = MakeField<int>("count", TypeID::Int32, offsetof(Sample, count));
        FieldInfo label    = MakeField<std::string>("label", TypeID::String, offsetof(Sample, label));
        FieldInfo position = MakeField<glm::vec3>("position", TypeID::Vec3, offsetof(Sample, position));
        FieldInfo rotation = MakeField<glm::quat>("rotation", TypeID::Quat, offsetof(Sample, rotation));
        FieldInfo mode     = MakeField<Mode>("mode", TypeID::Enum, offsetof(Sample, mode));
        FieldInfo weights  = MakeField<std::vector<float>>("weights", TypeID::Vector, offsetof(Sample, weights));
        FieldInfo runtime  = MakeField<float>("runtime", TypeID::Float, offsetof(Sample, runtime), ReadOnly);

        std::vector<FieldInfo*> all;

        SampleFields()
        {
            mode.enumDesc = &ModeDescriptor;
            weights.container = &WeightsContainer;
            all = { &speed, &flag, &count, &label, &position, &rotation, &mode, &weights, &runtime };
        }
    };

    class ReflectionSerializerTest : public ::testing::Test {
    protected:
        SampleFields reflected;
        Serialization::ReflectionContext context;
    };
}

TEST_F(ReflectionSerializerTest, WritesOneKeyPerFieldInTheEditorsShape)
{
    Sample s;
    s.speed = 2.5f;
    s.flag = true;
    s.label = "hello";
    s.position = glm::vec3(1, 2, 3);
    s.mode = Mode::Auto;
    s.weights = { 0.5f, 1.5f };

    ordered_json out;
    Serialization::WriteFields(reflected.all, &s, out, context);

    EXPECT_FLOAT_EQ(out["speed"], 2.5f);
    EXPECT_TRUE(out["flag"]);
    EXPECT_EQ(out["count"], 3);
    EXPECT_EQ(out["label"], "hello");
    EXPECT_FLOAT_EQ(out["position"]["y"], 2.0f);
    EXPECT_EQ(out["mode"], "Auto");
    ASSERT_EQ(out["weights"].size(), 2u);
    EXPECT_FLOAT_EQ(out["weights"][1], 1.5f);
}

TEST_F(ReflectionSerializerTest, ReadOnlyFieldsAreNotPersisted)
{
    Sample s;

    ordered_json out;
    Serialization::WriteFields(reflected.all, &s, out, context);
    EXPECT_FALSE(out.contains("runtime"));

    Sample loaded;
    Serialization::ReadFields(reflected.all, &loaded, json{ { "runtime", 99.0f } }, context);
    EXPECT_FLOAT_EQ(loaded.runtime, 5.0f);
}

TEST_F(ReflectionSerializerTest, RoundTripsEveryField)
{
    Sample s;
    s.speed = -4.0f;
    s.flag = true;
    s.count = 42;
    s.label = "round trip";
    s.position = glm::vec3(-1, 0.5f, 8);
    s.rotation = glm::quat(glm::radians(glm::vec3(10, 20, 30)));
    s.mode = Mode::On;
    s.weights = { 1.0f, 2.0f, 3.0f };

    ordered_json out;
    Serialization::WriteFields(reflected.all, &s, out, context);

    Sample loaded;
    Serialization::ReadFields(reflected.all, &loaded, json::parse(out.dump()), context);

    EXPECT_FLOAT_EQ(loaded.speed, s.speed);
    EXPECT_EQ(loaded.flag, s.flag);
    EXPECT_EQ(loaded.count, s.count);
    EXPECT_EQ(loaded.label, s.label);
    EXPECT_EQ(loaded.position, s.position);
    EXPECT_EQ(loaded.mode, s.mode);
    EXPECT_EQ(loaded.weights, s.weights);
    EXPECT_NEAR(glm::abs(glm::dot(loaded.rotation, s.rotation)), 1.0f, 1e-5f);
}

TEST_F(ReflectionSerializerTest, MissingOrIllTypedKeysKeepTheDefault)
{
    Sample loaded;
    Serialization::ReadFields(reflected.all, &loaded, json{ { "speed", "fast" }, { "mode", "Nope" }, { "count", 9 } }, context);

    EXPECT_FLOAT_EQ(loaded.speed, 1.0f);
    EXPECT_EQ(loaded.mode, Mode::Off);
    EXPECT_EQ(loaded.count, 9);
    EXPECT_EQ(loaded.label, "default");
}

TEST_F(ReflectionSerializerTest, ReadingAVectorReplacesItsContent)
{
    Sample loaded;
    loaded.weights = { 9.0f, 9.0f, 9.0f, 9.0f };

    Serialization::ReadFields(reflected.all, &loaded, json{ { "weights", { 1.0f, 2.0f } } }, context);

    EXPECT_EQ(loaded.weights, (std::vector<float>{ 1.0f, 2.0f }));
}

TEST_F(ReflectionSerializerTest, ReportsEachFieldItRead)
{
    Sample loaded;
    std::vector<std::string> changed;

    Serialization::ReadFields(reflected.all, &loaded, json{ { "speed", 2.0f }, { "flag", true } }, context,
                              [&](const FieldInfo& field) { changed.push_back(field.name); });

    EXPECT_EQ(changed, (std::vector<std::string>{ "speed", "flag" }));
}

TEST_F(ReflectionSerializerTest, ReadsARotationAsEulerDegreesOrAsAQuaternion)
{
    Sample euler;
    Serialization::ReadFields(reflected.all, &euler, json{ { "rotation", { { "x", 0.0f }, { "y", 90.0f }, { "z", 0.0f } } } }, context);

    const glm::quat expected = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0));
    EXPECT_NEAR(glm::abs(glm::dot(euler.rotation, expected)), 1.0f, 1e-5f);

    Sample raw;
    Serialization::ReadFields(reflected.all, &raw, json{ { "rotation", { { "x", 0.0f }, { "y", 0.0f }, { "z", 0.0f }, { "w", 1.0f } } } }, context);
    EXPECT_NEAR(glm::abs(glm::dot(raw.rotation, glm::quat(1, 0, 0, 0))), 1.0f, 1e-5f);
}
