// Shard C++ SDK : the engine's own components (model, light, physics body...) as typed handles, over shard.actors' field functions.
//
//   shard::Actor bullet = GetScene().Create("Bullet");
//
//   auto model = bullet.Add<shard::Model>();
//   model.SetMesh(mesh);                                       // an AssetId (a field of the script), or the asset's name in the project
//   model.SetMaterial(0, material);
//
//   auto body = bullet.Add<shard::PhysicsBody>();
//   body.SetShape(shard::Shape::Capsule(0.1f, 0.3f));
//   body.SetMotionType(shard::MotionType::Dynamic);            // the body of the new kind exists at once : the velocity has something to push
//   body.SetFriction(0.8f);
//   bullet.SetVelocity(forward * 30.f);
//
//   if (auto light = bullet.Get<shard::Light>()) light.SetIntensity(3.f);
//
// A handle is a pair (actor, component type) : copying it is free, and it stays safe when the actor goes. Every setter changes that one field and
// nothing else, the way an edit in the inspector does (the component's own change hook runs : a body keeps its velocity when its friction changes)
// and throws shard::Error when the engine refuses it. The getters return a zero when the component or the field is gone.
//
// Not every component has a class here : shard::ComponentRef reaches any field of any component of the engine by its name (the one the inspector
// and the world file use) :
//
//   shard::ComponentRef probe(actor, "probeVolume");
//   probe.SetFloat("blendDistance", 2.f);
//
// Assets read as their name in the project (the way a world file stores them) and are set from an AssetId or from that name.
#pragma once

#include <cstdint>
#include <initializer_list>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>

#include <shard/cpp/actor.hpp>
#include <shard/cpp/component.hpp>

namespace shard {

    namespace detail {

        // ---- JSON text, as little of it as the setters and getters need (the host owns the real parsing) ----

        inline std::string JsonNumber(double value)
        {
            std::ostringstream out;
            out.imbue(std::locale::classic());
            out.precision(std::numeric_limits<float>::max_digits10 + 3);
            out << value;
            return out.str();
        }
        inline std::string JsonNumber(float value)
        {
            std::ostringstream out;
            out.imbue(std::locale::classic());
            out.precision(std::numeric_limits<float>::max_digits10);
            out << value;
            return out.str();
        }
        inline std::string JsonNumber(int64_t value) { return std::to_string(value); }

        inline std::string JsonString(std::string_view text)
        {
            std::string out = "\"";
            for (const char c : text) {
                switch (c) {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default:
                        if (static_cast<unsigned char>(c) < 0x20) {
                            static const char* digits = "0123456789abcdef";
                            out += "\\u00";
                            out += digits[(c >> 4) & 0xF];
                            out += digits[c & 0xF];
                        } else {
                            out += c;
                        }
                }
            }
            out += '"';
            return out;
        }

        inline bool ParseNumber(const std::string& text, size_t from, double& out, size_t* end = nullptr)
        {
            std::istringstream in(text.substr(from));
            in.imbue(std::locale::classic());
            double value = 0.0;
            if (!(in >> value)) return false;
            out = value;
            if (end) *end = in.eof() ? text.size() : from + size_t(in.tellg());
            return true;
        }

        /// The number after "key": in a JSON object's text
        inline bool ParseKeyedNumber(const std::string& text, const char* key, float& out)
        {
            const std::string needle = std::string("\"") + key + "\"";
            size_t at = text.find(needle);
            if (at == std::string::npos) return false;
            at = text.find(':', at + needle.size());
            if (at == std::string::npos) return false;
            double value = 0.0;
            if (!ParseNumber(text, at + 1, value)) return false;
            out = float(value);
            return true;
        }

        inline bool ParseString(const std::string& text, std::string& out)
        {
            if (text.size() < 2 || text.front() != '"' || text.back() != '"') return false;
            std::string value;
            for (size_t i = 1; i + 1 < text.size(); ++i) {
                char c = text[i];
                if (c == '\\' && i + 2 < text.size()) {
                    c = text[++i];
                    switch (c) {
                        case 'n': c = '\n'; break;
                        case 'r': c = '\r'; break;
                        case 't': c = '\t'; break;
                        default: break;
                    }
                }
                value += c;
            }
            out = std::move(value);
            return true;
        }

        /// How many elements the array text holds
        inline size_t CountElements(const std::string& text)
        {
            size_t count = 0;
            int depth = 0;
            bool inString = false, any = false;
            for (size_t i = 0; i < text.size(); ++i) {
                const char c = text[i];
                if (inString) {
                    if (c == '\\') ++i;
                    else if (c == '"') inString = false;
                    continue;
                }
                if (c == '"') { inString = true; any = true; }
                else if (c == '[' || c == '{') { if (depth++ >= 1) any = true; }
                else if (c == ']' || c == '}') --depth;
                else if (c == ',' && depth == 1) ++count;
                else if (depth >= 1 && c != ' ' && c != '\t' && c != '\n' && c != '\r') any = true;
            }
            return any ? count + 1 : 0;
        }
    }

    /// An enum of the engine's components, as the names the inspector shows
    enum class MotionType { Static, Kinematic, Dynamic };
    enum class GravityMode { Fields, World, Custom };
    enum class LightType { Directional, Point, Spot };
    enum class BakeMode { Dynamic, Stationary, Static };
    enum class BillboardFacing { Camera, AxisY, Fixed };

    namespace detail {
        inline const char* EnumName(MotionType v) { return v == MotionType::Static ? "Static" : v == MotionType::Kinematic ? "Kinematic" : "Dynamic"; }
        inline const char* EnumName(GravityMode v) { return v == GravityMode::Fields ? "Fields" : v == GravityMode::World ? "World" : "Custom"; }
        inline const char* EnumName(LightType v) { return v == LightType::Directional ? "Directional" : v == LightType::Point ? "Point" : "Spot"; }
        inline const char* EnumName(BakeMode v) { return v == BakeMode::Dynamic ? "Dynamic" : v == BakeMode::Stationary ? "Stationary" : "Static"; }
        inline const char* EnumName(BillboardFacing v) { return v == BillboardFacing::Camera ? "Camera" : v == BillboardFacing::AxisY ? "AxisY" : "Fixed"; }
    }

    /// One component of an actor, reached by the names of its fields. The classes below (Model, PhysicsBody...) are this with the names written
    /// for you ; use it directly for a component that has no class here.
    class ComponentRef {
    public:
        ComponentRef() = default;
        /// `type` is the name the world file gives the component ("light", "physics_body", or "<plugin id>.<script>"). It must outlive the handle
        /// (a literal).
        ComponentRef(Actor actor, const char* type) : m_Actor(actor), m_Type(type) {}

        const Actor& Owner() const { return m_Actor; }
        const char* Type() const { return m_Type; }
        /// The actor is alive and still has the component
        bool IsValid() const { return m_Type && m_Actor.IsValid() && m_Actor.HasComponent(m_Type); }
        explicit operator bool() const { return IsValid(); }
        /// Removes the component from its actor
        bool Remove() const { return m_Type && m_Actor.RemoveComponent(m_Type); }

        /// A deactivated component does nothing, as if it were not there
        bool IsActive() const { return GetBool("active", false); }
        void SetActive(bool active) const { SetBool("active", active); }

        // ---- A field, by its name (or a path into it : "materials/0", "shapes/0/params/radius") ----
        /// The value as JSON text ; empty if the component or the field is not there
        std::string GetJson(std::string_view path) const
        {
            std::string out;
            if (!m_Type) return out;
            ReadText([&](char* b, uint64_t c, uint64_t* s) { return detail::Actors().get_component_field(m_Actor.Handle(), Str(m_Type), Str(path), b, c, s); }, out);
            return out;
        }
        /// Sets the field to a value written as JSON text. Throws shard::Error.
        void SetJson(std::string_view path, std::string_view valueJson) const
        {
            Check(detail::Actors().set_component_field(m_Actor.Handle(), Str(Needed()), Str(path), Str(valueJson)), "set", path);
        }
        /// Removes an element of an array field (path "shapes/1") or a key of an object. Throws shard::Error.
        void Erase(std::string_view path) const
        {
            Check(detail::Actors().erase_component_field(m_Actor.Handle(), Str(Needed()), Str(path)), "erase", path);
        }

        // ---- Typed fields ----
        void SetFloat(std::string_view path, float value) const { SetJson(path, detail::JsonNumber(value)); }
        void SetInt(std::string_view path, int64_t value) const { SetJson(path, detail::JsonNumber(value)); }
        void SetBool(std::string_view path, bool value) const { SetJson(path, value ? "true" : "false"); }
        void SetString(std::string_view path, std::string_view value) const { SetJson(path, detail::JsonString(value)); }
        void SetVec3(std::string_view path, Vec3 v) const
        {
            SetJson(path, "{\"x\":" + detail::JsonNumber(v.x) + ",\"y\":" + detail::JsonNumber(v.y) + ",\"z\":" + detail::JsonNumber(v.z) + "}");
        }
        void SetColor(std::string_view path, Color c) const
        {
            SetJson(path, "{\"r\":" + detail::JsonNumber(c.r) + ",\"g\":" + detail::JsonNumber(c.g) + ",\"b\":" + detail::JsonNumber(c.b) +
                          ",\"a\":" + detail::JsonNumber(c.a) + "}");
        }
        /// An asset, by its id (0 : none) or by its name in the project
        void SetAsset(std::string_view path, AssetId asset) const { SetInt(path, asset.id); }
        void SetAsset(std::string_view path, std::string_view assetName) const { SetString(path, assetName); }
        /// An enum, by the name the inspector shows ("Dynamic")
        void SetEnum(std::string_view path, std::string_view name) const { SetString(path, name); }

        float GetFloat(std::string_view path, float fallback = 0.f) const
        {
            double value = 0.0;
            return detail::ParseNumber(GetJson(path), 0, value) ? float(value) : fallback;
        }
        int64_t GetInt(std::string_view path, int64_t fallback = 0) const
        {
            double value = 0.0;
            return detail::ParseNumber(GetJson(path), 0, value) ? int64_t(value) : fallback;
        }
        bool GetBool(std::string_view path, bool fallback = false) const
        {
            const std::string json = GetJson(path);
            return json == "true" ? true : json == "false" ? false : fallback;
        }
        std::string GetString(std::string_view path) const
        {
            std::string out;
            return detail::ParseString(GetJson(path), out) ? out : std::string();
        }
        Vec3 GetVec3(std::string_view path) const
        {
            Vec3 v;
            const std::string json = GetJson(path);
            detail::ParseKeyedNumber(json, "x", v.x);
            detail::ParseKeyedNumber(json, "y", v.y);
            detail::ParseKeyedNumber(json, "z", v.z);
            return v;
        }
        Color GetColor(std::string_view path) const
        {
            Color c;
            const std::string json = GetJson(path);
            detail::ParseKeyedNumber(json, "r", c.r);
            detail::ParseKeyedNumber(json, "g", c.g);
            detail::ParseKeyedNumber(json, "b", c.b);
            detail::ParseKeyedNumber(json, "a", c.a);
            return c;
        }
        /// The name in the project of an asset field ; empty for none
        std::string GetAsset(std::string_view path) const { return GetString(path); }
        /// The name of the value of an enum field ("Dynamic")
        std::string GetEnum(std::string_view path) const { return GetString(path); }
        /// How many elements an array field holds
        size_t Count(std::string_view path) const { return detail::CountElements(GetJson(path)); }

    private:
        const char* Needed() const
        {
            if (!m_Type) throw Error(SHARD_E_INVALID_ARG, "a component handle that was never given a type");
            return m_Type;
        }
        void Check(ShardResult result, const char* what, std::string_view path) const
        {
            detail::Check(result, std::string(what) + " " + (m_Type ? m_Type : "?") + "." + std::string(path));
        }

        Actor m_Actor;
        const char* m_Type = nullptr;
    };

    /// A shape of a physics body. Dimensions are in the body's own space, before the actor's scale.
    struct Shape {
        enum class Kind { Sphere, Box, Capsule, Cylinder };
        Kind kind = Kind::Box;
        float radius = 0.5f;                ///< sphere, capsule, cylinder
        float halfHeight = 0.5f;            ///< capsule (without its caps), cylinder : it lies along the body's Y
        Vec3 halfExtent{ 0.5f, 0.5f, 0.5f };    ///< box
        Vec3 offset;                        ///< where the shape sits in the body
        Quat rotation;                      ///< how it is turned in the body

        static Shape Sphere(float radius = 0.5f) { Shape s; s.kind = Kind::Sphere; s.radius = radius; return s; }
        static Shape Box(Vec3 halfExtent = { 0.5f, 0.5f, 0.5f }) { Shape s; s.kind = Kind::Box; s.halfExtent = halfExtent; return s; }
        static Shape Capsule(float radius = 0.5f, float halfHeight = 0.5f) { Shape s; s.kind = Kind::Capsule; s.radius = radius; s.halfHeight = halfHeight; return s; }
        static Shape Cylinder(float radius = 0.5f, float halfHeight = 0.5f) { Shape s; s.kind = Kind::Cylinder; s.radius = radius; s.halfHeight = halfHeight; return s; }
        /// The same shape, moved and turned inside the body
        Shape At(Vec3 where, Quat turned = Quat()) const { Shape s = *this; s.offset = where; s.rotation = turned; return s; }

        std::string ToJson() const
        {
            using detail::JsonNumber;
            std::string params;
            const char* name = "box";
            switch (kind) {
                case Kind::Sphere: name = "sphere"; params = "\"radius\":" + JsonNumber(radius); break;
                case Kind::Capsule: name = "capsule"; params = "\"radius\":" + JsonNumber(radius) + ",\"halfHeight\":" + JsonNumber(halfHeight); break;
                case Kind::Cylinder: name = "cylinder"; params = "\"radius\":" + JsonNumber(radius) + ",\"halfHeight\":" + JsonNumber(halfHeight); break;
                case Kind::Box:
                    params = "\"halfExtent\":{\"x\":" + JsonNumber(halfExtent.x) + ",\"y\":" + JsonNumber(halfExtent.y) + ",\"z\":" + JsonNumber(halfExtent.z) + "}";
                    break;
            }
            return std::string("{\"shape\":\"") + name + "\",\"params\":{" + params + "},\"offset\":{\"x\":" + JsonNumber(offset.x) + ",\"y\":" +
                   JsonNumber(offset.y) + ",\"z\":" + JsonNumber(offset.z) + "},\"rotation\":{\"x\":" + JsonNumber(rotation.x) + ",\"y\":" +
                   JsonNumber(rotation.y) + ",\"z\":" + JsonNumber(rotation.z) + ",\"w\":" + JsonNumber(rotation.w) + "}}";
        }
    };

    /// "model" : a mesh and its materials, drawn where the actor is
    class Model : public ComponentRef {
    public:
        static constexpr const char* kType = "model";
        Model() = default;
        explicit Model(Actor actor) : ComponentRef(actor, kType) {}

        void SetMesh(AssetId mesh) const { SetAsset("mesh", mesh); }
        void SetMesh(std::string_view assetName) const { SetAsset("mesh", assetName); }
        std::string Mesh() const { return GetAsset("mesh"); }

        /// The material of a submesh. A slot past the end grows the list (the ones in between hold no material).
        void SetMaterial(uint32_t slot, AssetId material) const { SetAsset("materials/" + std::to_string(slot), material); }
        void SetMaterial(uint32_t slot, std::string_view assetName) const { SetAsset("materials/" + std::to_string(slot), assetName); }
        std::string Material(uint32_t slot) const { return GetAsset("materials/" + std::to_string(slot)); }
        /// Replaces all the materials
        void SetMaterials(std::initializer_list<AssetId> materials) const
        {
            std::string list = "[";
            for (const AssetId& m : materials) list += (list.size() > 1 ? "," : "") + std::to_string(m.id);
            SetJson("materials", list + "]");
        }
        size_t MaterialCount() const { return Count("materials"); }

        /// Takes part in the baked lighting (turn it off for what moves)
        void SetStaticLighting(bool on) const { SetBool("staticLighting", on); }
        bool StaticLighting() const { return GetBool("staticLighting"); }
        void SetLightmapResolution(int texels) const { SetInt("lightmapResolution", texels); }
        int LightmapResolution() const { return int(GetInt("lightmapResolution")); }
    };

    /// "physics_body" : the actor is simulated (or collides, if it is static). Velocities and forces are on the Actor.
    class PhysicsBody : public ComponentRef {
    public:
        static constexpr const char* kType = "physics_body";
        PhysicsBody() = default;
        explicit PhysicsBody(Actor actor) : ComponentRef(actor, kType) {}

        /// Static never moves, Kinematic is moved by code only, Dynamic is driven by the simulation. The body is rebuilt as the new kind at once.
        void SetMotionType(MotionType type) const { SetEnum("motionType", detail::EnumName(type)); }
        MotionType GetMotionType() const
        {
            const std::string name = GetEnum("motionType");
            return name == "Dynamic" ? MotionType::Dynamic : name == "Kinematic" ? MotionType::Kinematic : MotionType::Static;
        }

        /// Replaces the shapes of the body by this one (the body is rebuilt, which drops its velocity)
        void SetShape(const Shape& shape) const { SetJson("shapes", "[" + shape.ToJson() + "]"); }
        /// Adds a shape to the body (a compound : the body is rebuilt)
        void AddShape(const Shape& shape) const { SetJson("shapes/-", shape.ToJson()); }
        /// Removes a shape ; a body keeps at least one
        void RemoveShape(size_t index) const { Erase("shapes/" + std::to_string(index)); }
        size_t ShapeCount() const { return Count("shapes"); }

        /// The mass in kg. Without it the mass comes from the shape (and the engine's density).
        void SetMass(float kilograms) const { SetBool("overrideMass", true); SetFloat("mass", kilograms); }
        float Mass() const { return GetFloat("mass"); }
        void UseShapeMass() const { SetBool("overrideMass", false); }

        void SetLinearDamping(float damping) const { SetFloat("linearDamping", damping); }
        float LinearDamping() const { return GetFloat("linearDamping"); }
        void SetAngularDamping(float damping) const { SetFloat("angularDamping", damping); }
        float AngularDamping() const { return GetFloat("angularDamping"); }
        /// A multiplier on the gravity the body feels : 0 floats, a negative one falls upwards
        void SetGravityFactor(float factor) const { SetFloat("gravityFactor", factor); }
        float GravityFactor() const { return GetFloat("gravityFactor", 1.f); }
        /// Fields : the world's gravity changed by the gravity fields around. World ignores the fields. Custom is SetCustomGravity's.
        void SetGravityMode(GravityMode mode) const { SetEnum("gravityMode", detail::EnumName(mode)); }
        void SetCustomGravity(Vec3 acceleration) const { SetVec3("customGravity", acceleration); SetGravityMode(GravityMode::Custom); }
        /// Resistance when sliding : 0 is ice, 1 is very grippy
        void SetFriction(float friction) const { SetFloat("friction", friction); }
        float Friction() const { return GetFloat("friction"); }
        /// Bounciness : 0 does not bounce, 1 gives back all its energy
        void SetRestitution(float restitution) const { SetFloat("restitution", restitution); }
        float Restitution() const { return GetFloat("restitution"); }
    };

    /// "light" : directional, point or spot
    class Light : public ComponentRef {
    public:
        static constexpr const char* kType = "light";
        Light() = default;
        explicit Light(Actor actor) : ComponentRef(actor, kType) {}

        void SetType(LightType type) const { SetEnum("lightType", detail::EnumName(type)); }
        void SetIntensity(float intensity) const { SetFloat("intensity", intensity); }
        float Intensity() const { return GetFloat("intensity"); }
        /// Distance at which a point or spot light stops reaching
        void SetRadius(float radius) const { SetFloat("radius", radius); }
        float Radius() const { return GetFloat("radius"); }
        void SetColor(Color color) const { ComponentRef::SetColor("color", color); }
        Color GetColor() const { return ComponentRef::GetColor("color"); }
        /// Spot light only : the angles, in degrees, of the cone that fades out and of the one at full brightness
        void SetCone(float innerDegrees, float outerDegrees) const { SetFloat("innerCutoff", innerDegrees); SetFloat("outerCutoff", outerDegrees); }
        void SetCastShadows(bool on) const { SetBool("castShadows", on); }
        bool CastShadows() const { return GetBool("castShadows"); }
        /// Size of the emitter, for the soft shadows of baking and raytracing
        void SetSourceSize(float size) const { SetFloat("sourceSize", size); }
        void SetBakeMode(BakeMode mode) const { SetEnum("bakeMode", detail::EnumName(mode)); }
    };

    /// "camera"
    class Camera : public ComponentRef {
    public:
        static constexpr const char* kType = "camera";
        Camera() = default;
        explicit Camera(Actor actor) : ComponentRef(actor, kType) {}

        /// Vertical field of view in degrees (perspective)
        void SetFov(float degrees) const { SetFloat("fov", degrees); }
        float Fov() const { return GetFloat("fov"); }
        void SetNearPlane(float distance) const { SetFloat("nearPlane", distance); }
        void SetFarPlane(float distance) const { SetFloat("farPlane", distance); }
        void SetOrthographic(bool on) const { SetBool("orthographic", on); }
        /// Half of the vertical size of the view, in world units (orthographic)
        void SetOrthoSize(float size) const { SetFloat("orthoSize", size); }
        void SetFrustumCulling(bool on) const { SetBool("frustumCulling", on); }
    };

    /// "audio" : a sound that plays from the actor
    class AudioSource : public ComponentRef {
    public:
        static constexpr const char* kType = "audio";
        AudioSource() = default;
        explicit AudioSource(Actor actor) : ComponentRef(actor, kType) {}

        void SetClip(AssetId clip) const { SetAsset("assetID", clip); }
        void SetClip(std::string_view assetName) const { SetAsset("assetID", assetName); }
        std::string Clip() const { return GetAsset("assetID"); }
        /// 1 is the clip at its original level
        void SetVolume(float volume) const { SetFloat("volume", volume); }
        float Volume() const { return GetFloat("volume", 1.f); }
        void SetPlayOnStart(bool on) const { SetBool("playOnStart", on); }
        /// Play in 3D : the volume and the panning follow the distance and direction to the listener
        void SetSpatialize(bool on) const { SetBool("spatialize", on); }
        void SetDopplerLevel(float level) const { SetFloat("dopplerLevel", level); }
        void SetReverbSend(float amount) const { SetFloat("reverbSend", amount); }
        bool Play() const { return Owner().PlayAudio(); }
        bool Pause() const { return Owner().PauseAudio(); }
    };

    /// "audio_listener" : where the scene is heard from
    class AudioListener : public ComponentRef {
    public:
        static constexpr const char* kType = "audio_listener";
        AudioListener() = default;
        explicit AudioListener(Actor actor) : ComponentRef(actor, kType) {}

        void SetDopplerFactor(float factor) const { SetFloat("dopplerFactor", factor); }
        float DopplerFactor() const { return GetFloat("dopplerFactor"); }
        void SetSpeedOfSound(float unitsPerSecond) const { SetFloat("speedOfSound", unitsPerSecond); }
    };

    /// "billboard" : a quad of a sprite that faces the camera
    class Billboard : public ComponentRef {
    public:
        static constexpr const char* kType = "billboard";
        Billboard() = default;
        explicit Billboard(Actor actor) : ComponentRef(actor, kType) {}

        /// Pixels when the size is on the screen, meters otherwise
        void SetSize(float size) const { SetFloat("size", size); }
        void SetFacing(BillboardFacing facing) const { SetEnum("facingType", detail::EnumName(facing)); }
        void SetScreenSpaceSize(bool on) const { SetBool("ScreenSpaceSize", on); }
        void SetDepthTest(bool on) const { SetBool("DepthTest", on); }
        /// Image of the project, such as "textures/icons.png" ; empty for the generic marker
        void SetSpritePath(std::string_view path) const { SetString("spritePath", path); }
        /// How many cells the image holds side by side, and which one is drawn
        void SetSpriteSheet(int cells, uint32_t cell = 0) const { SetInt("spritesCount", cells); SetInt("sprite", cell); }
        void SetTint(Color tint) const { ComponentRef::SetColor("tint", tint); }
    };
}
