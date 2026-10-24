// Shard C++ SDK : what a plugin puts on the screen (shard_render.h) : meshes it builds, textures it fills, sprites, instances and a 2D overlay.
//
//   // voxels : a chunk is a mesh and an object
//   Shard::MeshBuilder b(SHARD_VERTEX_POSITION | SHARD_VERTEX_NORMAL | SHARD_VERTEX_UV0);
//   b.Quad(corner0, corner1, corner2, corner3, normal);
//   Shard::Mesh mesh = Shard::Mesh::Create(b);                       // from a job if you like : creating records, the GPU work happens later
//   Shard::RenderObject chunk = Shard::RenderObject::Create(mesh, { &material }, { chunkOrigin });
//
//   // particles : a batch of sprites, set again every frame
//   Shard::SpriteBatchOptions o; o.texture = &tex; o.blend = SHARD_BLEND_ALPHA; o.maxSprites = 4096;
//   Shard::SpriteBatch smoke = Shard::SpriteBatch::Create(o);
//   smoke.Set(sprites);
//
//   // a UI : an overlay draws into the frame
//   Shard::Overlay ui = Shard::Overlay::Add("ui", 0, [&](const Shard::OverlayFrame& f, Shard::Canvas& c) { c.DrawGeometry(g, &atlas, { 10.f, 10.f }); });
//
// Every class owns its handle (move only) : the resource goes when the object does, and the host releases whatever is left with the plugin.
#pragma once

#include <cstring>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <shard/shard_render.h>
#include <shard/cpp/actor.hpp>
#include <shard/cpp/detail.hpp>
#include <shard/cpp/math.hpp>

namespace Shard {

    namespace detail {
        inline const ShardRenderApi& RenderApi() { return Require<ShardRenderApi>(SHARD_IFACE_RENDER); }

        /// Owns a handle of shard.render and gives it back with `destroy`
        template <typename Handle, ShardResult (SHARD_CALL *ShardRenderApi::*Destroy)(ShardPluginCtx, Handle)>
        class Owned {
        public:
            Owned() = default;
            explicit Owned(Handle handle) : m_Handle(handle) {}
            Owned(Owned&& other) noexcept : m_Handle(other.m_Handle) { other.m_Handle = 0; }
            Owned& operator=(Owned&& other) noexcept
            {
                if (this != &other) {
                    Reset();
                    m_Handle = other.m_Handle;
                    other.m_Handle = 0;
                }
                return *this;
            }
            Owned(const Owned&) = delete;
            Owned& operator=(const Owned&) = delete;
            ~Owned() { Reset(); }

            Handle Handle_() const { return m_Handle; }
            explicit operator bool() const { return m_Handle != 0; }
            void Reset()
            {
                if (m_Handle && Host()) {
                    if (const ShardRenderApi* api = Interface<ShardRenderApi>(SHARD_IFACE_RENDER)) (api->*Destroy)(PluginHandle(), m_Handle);
                }
                m_Handle = 0;
            }

        private:
            Handle m_Handle = 0;
        };
    }

    constexpr uint32_t PackColor(float r, float g, float b, float a = 1.f)
    {
        const auto byte = [](float v) { return uint32_t(v <= 0.f ? 0.f : v >= 1.f ? 255.f : v * 255.f + 0.5f); };
        return byte(r) | (byte(g) << 8) | (byte(b) << 16) | (byte(a) << 24);
    }
    constexpr uint32_t PackColor(Color c) { return PackColor(c.r, c.g, c.b, c.a); }

    // ---- Meshes ----

    /// A vertex as the builder holds it ; the attributes the builder's flags do not ask for are not sent
    struct MeshVertex {
        Vec3 position;
        Vec3 normal{ 0.f, 1.f, 0.f };
        Vec4 tangent{ 1.f, 0.f, 0.f, 1.f };
        Vec2 uv0, uv1;
        uint32_t color = 0xFFFFFFFFu;
        uint16_t joints[4] = {};
        float weights[4] = { 1.f, 0.f, 0.f, 0.f };
    };

    /// Collects the vertices and triangles of a mesh. Not thread safe (one builder per job).
    class MeshBuilder {
    public:
        /// `flags` : the attributes to send (SHARD_VERTEX_*), POSITION is always there
        explicit MeshBuilder(uint32_t flags = SHARD_VERTEX_POSITION | SHARD_VERTEX_NORMAL | SHARD_VERTEX_UV0 | SHARD_VERTEX_COLOR) : m_Flags(flags | SHARD_VERTEX_POSITION) {}

        uint32_t Flags() const { return m_Flags; }
        void Clear() { m_Vertices.clear(); m_Indices.clear(); m_Submeshes.clear(); }
        bool Empty() const { return m_Indices.empty(); }
        uint32_t VertexCount() const { return uint32_t(m_Vertices.size()); }
        uint32_t IndexCount() const { return uint32_t(m_Indices.size()); }
        void Reserve(size_t vertices, size_t indices) { m_Vertices.reserve(vertices); m_Indices.reserve(indices); }

        /// The index of the vertex
        uint32_t Add(const MeshVertex& v) { m_Vertices.push_back(v); return uint32_t(m_Vertices.size() - 1); }
        uint32_t Add(Vec3 position, Vec3 normal = { 0.f, 1.f, 0.f }, Vec2 uv = {}, uint32_t color = 0xFFFFFFFFu)
        {
            MeshVertex v;
            v.position = position; v.normal = normal; v.uv0 = uv; v.color = color;
            return Add(v);
        }
        void Triangle(uint32_t a, uint32_t b, uint32_t c) { m_Indices.push_back(a); m_Indices.push_back(b); m_Indices.push_back(c); }
        /// Two triangles for the quad a, b, c, d (counter clockwise seen from the front)
        void Quad(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { Triangle(a, b, c); Triangle(a, c, d); }
        /// A flat quad of four corners, counter clockwise, with the uvs of a unit square
        void Quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 normal, uint32_t color = 0xFFFFFFFFu)
        {
            const uint32_t i = Add(a, normal, { 0.f, 0.f }, color);
            Add(b, normal, { 1.f, 0.f }, color);
            Add(c, normal, { 1.f, 1.f }, color);
            Add(d, normal, { 0.f, 1.f }, color);
            Quad(i, i + 1, i + 2, i + 3);
        }
        /// What follows (until the next call) is drawn with material slot `slot`
        void BeginSubmesh(uint32_t materialSlot)
        {
            EndSubmesh();
            m_Open = true;
            m_OpenSlot = materialSlot;
            m_OpenStart = uint32_t(m_Indices.size());
        }
        void SetBounds(Vec3 lo, Vec3 hi) { m_Lo = lo; m_Hi = hi; m_HasBounds = true; }

        /// Fills the ABI's description ; the packed vertices live in `storage` for as long as the description is used
        ShardMeshDesc Describe(std::vector<uint8_t>& storage, std::vector<ShardSubmesh>& parts, uint32_t meshFlags = 0)
        {
            EndSubmesh();
            const uint32_t f = m_Flags;
            const uint32_t stride = 12u + ((f & SHARD_VERTEX_NORMAL) ? 12u : 0u) + ((f & SHARD_VERTEX_TANGENT) ? 16u : 0u) + ((f & SHARD_VERTEX_UV0) ? 8u : 0u) +
                                    ((f & SHARD_VERTEX_UV1) ? 8u : 0u) + ((f & SHARD_VERTEX_COLOR) ? 4u : 0u) + ((f & SHARD_VERTEX_JOINTS) ? 8u : 0u) +
                                    ((f & SHARD_VERTEX_WEIGHTS) ? 16u : 0u);
            storage.resize(size_t(stride) * m_Vertices.size());
            uint8_t* p = storage.data();
            for (const MeshVertex& v : m_Vertices) {
                std::memcpy(p, &v.position, 12); p += 12;
                if (f & SHARD_VERTEX_NORMAL) { std::memcpy(p, &v.normal, 12); p += 12; }
                if (f & SHARD_VERTEX_TANGENT) { std::memcpy(p, &v.tangent, 16); p += 16; }
                if (f & SHARD_VERTEX_UV0) { std::memcpy(p, &v.uv0, 8); p += 8; }
                if (f & SHARD_VERTEX_UV1) { std::memcpy(p, &v.uv1, 8); p += 8; }
                if (f & SHARD_VERTEX_COLOR) { std::memcpy(p, &v.color, 4); p += 4; }
                if (f & SHARD_VERTEX_JOINTS) { std::memcpy(p, v.joints, 8); p += 8; }
                if (f & SHARD_VERTEX_WEIGHTS) { std::memcpy(p, v.weights, 16); p += 16; }
            }
            parts = m_Submeshes;
            ShardMeshDesc desc{};
            desc.structSize = sizeof(ShardMeshDesc);
            desc.vertexFlags = f;
            desc.vertexCount = uint32_t(m_Vertices.size());
            desc.indexCount = uint32_t(m_Indices.size());
            desc.submeshCount = uint32_t(parts.size());
            desc.flags = meshFlags;
            desc.vertices = storage.data();
            desc.indices = m_Indices.data();
            desc.submeshes = parts.empty() ? nullptr : parts.data();
            if (m_HasBounds) {
                desc.boundsMin[0] = m_Lo.x; desc.boundsMin[1] = m_Lo.y; desc.boundsMin[2] = m_Lo.z;
                desc.boundsMax[0] = m_Hi.x; desc.boundsMax[1] = m_Hi.y; desc.boundsMax[2] = m_Hi.z;
            }
            return desc;
        }

    private:
        void EndSubmesh()
        {
            if (!m_Open) return;
            m_Open = false;
            if (m_Indices.size() > m_OpenStart) m_Submeshes.push_back(ShardSubmesh{ m_OpenStart, uint32_t(m_Indices.size()) - m_OpenStart, m_OpenSlot });
        }
        uint32_t m_Flags;
        std::vector<MeshVertex> m_Vertices;
        std::vector<uint32_t> m_Indices;
        std::vector<ShardSubmesh> m_Submeshes;
        bool m_Open = false;
        uint32_t m_OpenSlot = 0, m_OpenStart = 0;
        bool m_HasBounds = false;
        Vec3 m_Lo, m_Hi;
    };

    class Mesh : public detail::Owned<ShardMesh, &ShardRenderApi::mesh_destroy> {
    public:
        Mesh() = default;
        explicit Mesh(ShardMesh handle) : Owned(handle) {}
        ShardMesh Handle() const { return Handle_(); }

        /// Throws Shard::Error for an inconsistent mesh. Safe from a job (thread : any) : the GPU work is done later.
        static Mesh Create(MeshBuilder& builder, bool dynamic = false)
        {
            std::vector<uint8_t> storage;
            std::vector<ShardSubmesh> parts;
            const ShardMeshDesc desc = builder.Describe(storage, parts, dynamic ? SHARD_MESH_DYNAMIC : 0u);
            ShardMesh handle = 0;
            detail::Check(detail::RenderApi().mesh_create(detail::PluginHandle(), &desc, &handle), "a mesh");
            return Mesh(handle);
        }
        /// Replaces the content (the objects that use the mesh follow)
        void Update(MeshBuilder& builder)
        {
            std::vector<uint8_t> storage;
            std::vector<ShardSubmesh> parts;
            const ShardMeshDesc desc = builder.Describe(storage, parts);
            detail::Check(detail::RenderApi().mesh_update(detail::PluginHandle(), Handle(), &desc), "a mesh update");
        }
        /// A model asset of the project (thread : main)
        static Mesh FromAsset(int32_t assetId)
        {
            ShardMesh handle = 0;
            detail::Check(detail::RenderApi().mesh_from_asset(detail::PluginHandle(), assetId, &handle), "a mesh from an asset");
            return Mesh(handle);
        }
    };

    // ---- Textures and materials ----

    class Texture : public detail::Owned<ShardTexture, &ShardRenderApi::texture_destroy> {
    public:
        Texture() = default;
        explicit Texture(ShardTexture handle) : Owned(handle) {}
        ShardTexture Handle() const { return Handle_(); }

        /// `pixels` : width * height texels in `format` (SHARD_TEXTURE_*), rows from the top ; null : transparent black
        static Texture Create(uint32_t width, uint32_t height, uint32_t format = SHARD_TEXTURE_RGBA8, uint32_t flags = 0, const void* pixels = nullptr)
        {
            ShardTextureDesc desc{};
            desc.structSize = sizeof(ShardTextureDesc);
            desc.width = width; desc.height = height; desc.format = format; desc.flags = flags; desc.pixels = pixels;
            ShardTexture handle = 0;
            detail::Check(detail::RenderApi().texture_create(detail::PluginHandle(), &desc, &handle), "a texture");
            return Texture(handle);
        }
        /// An image of the project or a mod:// address (thread : main). Straight alpha.
        static Texture Load(std::string_view path, uint32_t flags = 0)
        {
            ShardTexture handle = 0;
            detail::Check(detail::RenderApi().texture_load(detail::PluginHandle(), Str(path), flags, &handle), "texture '" + std::string(path) + "'");
            return Texture(handle);
        }
        static Texture FromAsset(int32_t assetId)
        {
            ShardTexture handle = 0;
            detail::Check(detail::RenderApi().texture_from_asset(detail::PluginHandle(), assetId, &handle), "a texture from an asset");
            return Texture(handle);
        }
        /// Replaces a rectangle (`rowPitch` bytes between rows, 0 : tight)
        bool Update(uint32_t x, uint32_t y, uint32_t width, uint32_t height, const void* pixels, uint32_t rowPitch = 0)
        {
            ShardTextureRegion r{};
            r.structSize = sizeof(ShardTextureRegion);
            r.x = x; r.y = y; r.width = width; r.height = height; r.rowPitch = rowPitch; r.pixels = pixels;
            return detail::RenderApi().texture_update(detail::PluginHandle(), Handle(), &r) == SHARD_OK;
        }
        /// (0, 0) while a loaded image is not decoded yet
        Vec2 Size() const
        {
            uint32_t w = 0, h = 0;
            detail::RenderApi().texture_size(detail::PluginHandle(), Handle(), &w, &h);
            return { float(w), float(h) };
        }
    };

    class Material : public detail::Owned<ShardMaterial, &ShardRenderApi::material_destroy> {
    public:
        Material() = default;
        explicit Material(ShardMaterial handle) : Owned(handle) {}
        ShardMaterial Handle() const { return Handle_(); }

        /// A private copy of a material asset of the project (thread : main)
        static Material Load(std::string_view path)
        {
            ShardMaterial handle = 0;
            detail::Check(detail::RenderApi().material_load(detail::PluginHandle(), Str(path), &handle), "material '" + std::string(path) + "'");
            return Material(handle);
        }
        bool SetFloat(std::string_view name, float value) const { return detail::RenderApi().material_set_float(detail::PluginHandle(), Handle(), Str(name), value) == SHARD_OK; }
        bool SetVec4(std::string_view name, Vec4 v) const { const float a[4] = { v.x, v.y, v.z, v.w }; return detail::RenderApi().material_set_vec4(detail::PluginHandle(), Handle(), Str(name), a) == SHARD_OK; }
        bool SetColor(std::string_view name, Color c) const { return SetVec4(name, Vec4{ c.r, c.g, c.b, c.a }); }
        bool SetTexture(std::string_view name, const Texture& texture) const { return detail::RenderApi().material_set_texture(detail::PluginHandle(), Handle(), Str(name), texture.Handle()) == SHARD_OK; }
    };

    // ---- Things that stay on screen ----

    /// A position in the world with double precision, a rotation and a scale
    struct RenderPose {
        double position[3] = {};
        Quat rotation;
        Vec3 scale{ 1.f, 1.f, 1.f };
    };

    class RenderObject : public detail::Owned<ShardRenderObject, &ShardRenderApi::object_destroy> {
    public:
        RenderObject() = default;
        explicit RenderObject(ShardRenderObject handle) : Owned(handle) {}
        ShardRenderObject Handle() const { return Handle_(); }

        /// A mesh drawn with `materials` (one per material slot) at `pose`. `pick` : the actor the editor selects when it is clicked.
        static RenderObject Create(const Mesh& mesh, std::initializer_list<const Material*> materials, const RenderPose& pose = {}, uint32_t flags = SHARD_OBJECT_CAST_SHADOWS, Actor pick = Actor())
        {
            std::vector<ShardMaterial> handles;
            for (const Material* m : materials) handles.push_back(m ? m->Handle() : 0);
            ShardRenderObjectDesc desc{};
            desc.structSize = sizeof(ShardRenderObjectDesc);
            desc.flags = flags;
            desc.mesh = mesh.Handle();
            desc.materials = handles.empty() ? nullptr : handles.data();
            desc.materialCount = uint32_t(handles.size());
            desc.pickActor = pick.Handle();
            desc.transform = Convert(pose);
            ShardRenderObject handle = 0;
            detail::Check(detail::RenderApi().object_create(detail::PluginHandle(), &desc, &handle), "a render object");
            return RenderObject(handle);
        }
        bool SetPose(const RenderPose& pose) const { const ShardRenderTransform t = Convert(pose); return detail::RenderApi().object_set_transform(detail::PluginHandle(), Handle(), &t) == SHARD_OK; }
        bool SetMesh(const Mesh& mesh) const { return detail::RenderApi().object_set_mesh(detail::PluginHandle(), Handle(), mesh.Handle()) == SHARD_OK; }
        bool SetFlags(uint32_t flags) const { return detail::RenderApi().object_set_flags(detail::PluginHandle(), Handle(), flags) == SHARD_OK; }
        bool SetVisible(bool visible, uint32_t otherFlags = SHARD_OBJECT_CAST_SHADOWS) const { return SetFlags(otherFlags | (visible ? 0u : SHARD_OBJECT_HIDDEN)); }
        /// The bone matrices of a skinned mesh, 16 floats each (the bone in the mesh's space times its inverse bind pose)
        bool SetBonePose(const float* matrices, uint32_t boneCount) const { return detail::RenderApi().object_set_pose(detail::PluginHandle(), Handle(), matrices, boneCount) == SHARD_OK; }

        static ShardRenderTransform Convert(const RenderPose& pose)
        {
            ShardRenderTransform t{};
            for (int i = 0; i < 3; ++i) t.position[i] = pose.position[i];
            t.rotation[0] = pose.rotation.x; t.rotation[1] = pose.rotation.y; t.rotation[2] = pose.rotation.z; t.rotation[3] = pose.rotation.w;
            t.scale[0] = pose.scale.x; t.scale[1] = pose.scale.y; t.scale[2] = pose.scale.z;
            return t;
        }
    };

    using Sprite = ShardSprite;
    using Instance = ShardInstance;

    struct SpriteBatchOptions {
        const Texture* texture = nullptr;
        uint32_t facing = SHARD_SPRITE_FACE_CAMERA;
        uint32_t blend = SHARD_BLEND_ALPHA;
        uint32_t sort = SHARD_SPRITE_SORT_NONE;
        uint32_t flags = SHARD_SPRITE_DEPTH_TEST;
        uint32_t maxSprites = 4096;
        double origin[3] = {};
    };

    class SpriteBatch : public detail::Owned<ShardSpriteBatch, &ShardRenderApi::sprite_batch_destroy> {
    public:
        SpriteBatch() = default;
        explicit SpriteBatch(ShardSpriteBatch handle) : Owned(handle) {}
        ShardSpriteBatch Handle() const { return Handle_(); }

        static SpriteBatch Create(const SpriteBatchOptions& o)
        {
            ShardSpriteBatchDesc desc{};
            desc.structSize = sizeof(ShardSpriteBatchDesc);
            desc.facing = o.facing; desc.blend = o.blend; desc.sort = o.sort; desc.flags = o.flags; desc.maxSprites = o.maxSprites;
            desc.texture = o.texture ? o.texture->Handle() : 0;
            for (int i = 0; i < 3; ++i) desc.origin[i] = o.origin[i];
            ShardSpriteBatch handle = 0;
            detail::Check(detail::RenderApi().sprite_batch_create(detail::PluginHandle(), &desc, &handle), "a sprite batch");
            return SpriteBatch(handle);
        }
        /// The sprites to draw until the next call. thread : any
        bool Set(const Sprite* sprites, uint32_t count) const { return detail::RenderApi().sprite_batch_set(detail::PluginHandle(), Handle(), sprites, count) == SHARD_OK; }
        bool Set(const std::vector<Sprite>& sprites) const { return Set(sprites.data(), uint32_t(sprites.size())); }
        bool SetFlags(uint32_t flags) const { return detail::RenderApi().sprite_batch_set_flags(detail::PluginHandle(), Handle(), flags) == SHARD_OK; }
    };

    class InstanceBatch : public detail::Owned<ShardInstanceBatch, &ShardRenderApi::instance_batch_destroy> {
    public:
        InstanceBatch() = default;
        explicit InstanceBatch(ShardInstanceBatch handle) : Owned(handle) {}
        ShardInstanceBatch Handle() const { return Handle_(); }

        static InstanceBatch Create(const Mesh& mesh, std::initializer_list<const Material*> materials, uint32_t maxInstances, uint32_t flags = SHARD_OBJECT_CAST_SHADOWS)
        {
            std::vector<ShardMaterial> handles;
            for (const Material* m : materials) handles.push_back(m ? m->Handle() : 0);
            ShardInstanceBatchDesc desc{};
            desc.structSize = sizeof(ShardInstanceBatchDesc);
            desc.flags = flags; desc.mesh = mesh.Handle(); desc.materials = handles.empty() ? nullptr : handles.data();
            desc.materialCount = uint32_t(handles.size()); desc.maxInstances = maxInstances;
            ShardInstanceBatch handle = 0;
            detail::Check(detail::RenderApi().instance_batch_create(detail::PluginHandle(), &desc, &handle), "an instance batch");
            return InstanceBatch(handle);
        }
        bool Set(const Instance* instances, uint32_t count) const { return detail::RenderApi().instance_batch_set(detail::PluginHandle(), Handle(), instances, count) == SHARD_OK; }
        bool Set(const std::vector<Instance>& instances) const { return Set(instances.data(), uint32_t(instances.size())); }
    };

    // ---- The 2D overlay ----

    using Vertex2D = ShardVertex2D;

    /// 2D geometry that an overlay draws again and again (a UI library compiles its elements once)
    class Geometry : public detail::Owned<ShardGeometry, &ShardRenderApi::geometry_destroy> {
    public:
        Geometry() = default;
        explicit Geometry(ShardGeometry handle) : Owned(handle) {}
        ShardGeometry Handle() const { return Handle_(); }
        static Geometry Create(const Vertex2D* vertices, uint32_t vertexCount, const uint32_t* indices, uint32_t indexCount)
        {
            ShardGeometry handle = 0;
            detail::Check(detail::RenderApi().geometry_create(detail::PluginHandle(), vertices, vertexCount, indices, indexCount, &handle), "2D geometry");
            return Geometry(handle);
        }
    };

    struct OverlayFrame {
        uint32_t width = 0, height = 0;
        float dpiScale = 1.f;
        float deltaTime = 0.f;
        uint64_t frame = 0;
        bool playing = false;
        Vec2 origin;        ///< the viewport's corner in the window : subtract it from the positions of input events
    };

    /// What an overlay draws with, valid during its callback
    class Canvas {
    public:
        explicit Canvas(const ShardDrawApi* api) : m_Api(api) {}
        void DrawGeometry(const Geometry& g, const Texture* texture, Vec2 translation = {}) const
        {
            const float t[2] = { translation.x, translation.y };
            m_Api->draw_geometry(g.Handle(), texture ? texture->Handle() : 0, t);
        }
        void SetTransform(const float* matrix16) const { m_Api->set_transform(matrix16); }
        void SetScissor(bool enabled, int x = 0, int y = 0, int width = 0, int height = 0) const { m_Api->set_scissor(enabled ? 1 : 0, x, y, width, height); }
        void SetClipMask(uint32_t operation, const Geometry* g = nullptr, Vec2 translation = {}) const
        {
            const float t[2] = { translation.x, translation.y };
            m_Api->set_clip_mask(operation, g ? g->Handle() : 0, t);
        }
        void Clear(Color c) const { const float rgba[4] = { c.r, c.g, c.b, c.a }; m_Api->clear(rgba); }
        void DrawTriangles(const Vertex2D* vertices, uint32_t vertexCount, const uint32_t* indices, uint32_t indexCount, const Texture* texture = nullptr) const
        {
            m_Api->draw_triangles(vertices, vertexCount, indices, indexCount, texture ? texture->Handle() : 0);
        }
        /// A filled rectangle (premultiplied colour)
        void FillRect(Vec2 min, Vec2 max, uint32_t premultipliedColor) const
        {
            const Vertex2D v[4] = { { { min.x, min.y }, premultipliedColor, { 0.f, 0.f } }, { { max.x, min.y }, premultipliedColor, { 1.f, 0.f } },
                                    { { max.x, max.y }, premultipliedColor, { 1.f, 1.f } }, { { min.x, max.y }, premultipliedColor, { 0.f, 1.f } } };
            const uint32_t i[6] = { 0, 1, 2, 0, 2, 3 };
            DrawTriangles(v, 4, i, 6);
        }

    private:
        const ShardDrawApi* m_Api;
    };

    class Overlay {
    public:
        using DrawFn = std::function<void(const OverlayFrame&, Canvas&)>;

        Overlay() = default;
        Overlay(Overlay&& other) noexcept : m_Handle(other.m_Handle), m_Fn(std::move(other.m_Fn)) { other.m_Handle = 0; }
        Overlay& operator=(Overlay&& other) noexcept
        {
            if (this != &other) {
                Reset();
                m_Handle = other.m_Handle;
                m_Fn = std::move(other.m_Fn);
                other.m_Handle = 0;
            }
            return *this;
        }
        Overlay(const Overlay&) = delete;
        Overlay& operator=(const Overlay&) = delete;
        ~Overlay() { Reset(); }

        /// Draws every frame, in order of `order`, over the picture (and over the overlays of a lower order). `target` : a render-target Texture to draw into
        /// instead of the screen. thread : main. Keep the object alive as long as the overlay should draw.
        static Overlay Add(std::string_view name, int32_t order, DrawFn fn, uint32_t flags = 0, const Texture* target = nullptr)
        {
            Overlay o;
            o.m_Fn = std::make_unique<DrawFn>(std::move(fn));
            ShardOverlayDesc desc{};
            desc.structSize = sizeof(ShardOverlayDesc);
            desc.flags = flags; desc.order = order; desc.name = Str(name);
            desc.target = target ? target->Handle() : 0;
            desc.user = o.m_Fn.get();
            desc.draw = [](void* user, ShardPluginCtx, const ShardOverlayFrame* f, const ShardDrawApi* api) {
                OverlayFrame frame;
                frame.width = f->width; frame.height = f->height; frame.dpiScale = f->dpiScale; frame.deltaTime = f->deltaTime; frame.frame = f->frame;
                frame.playing = f->playing == SHARD_TRUE;
                if (f->structSize >= offsetof(ShardOverlayFrame, originY) + sizeof(float)) frame.origin = Vec2{ f->originX, f->originY };
                Canvas canvas(api);
                detail::NoThrow([&] { (*static_cast<DrawFn*>(user))(frame, canvas); });
            };
            detail::Check(detail::RenderApi().overlay_add(detail::PluginHandle(), &desc, &o.m_Handle), "an overlay");
            return o;
        }
        explicit operator bool() const { return m_Handle != 0; }
        void Reset()
        {
            if (m_Handle && detail::Host()) {
                if (const ShardRenderApi* api = detail::Interface<ShardRenderApi>(SHARD_IFACE_RENDER)) api->overlay_remove(detail::PluginHandle(), m_Handle);
            }
            m_Handle = 0;
            m_Fn.reset();
        }

    private:
        ShardOverlay m_Handle = 0;
        std::unique_ptr<DrawFn> m_Fn;
    };

    // ---- The camera ----

    struct CameraView {
        bool valid = false;
        uint32_t width = 0, height = 0;
        bool orthographic = false;
        float fovDegrees = 60.f, orthoSize = 10.f, nearPlane = 0.1f, farPlane = 100.f;
        double position[3] = {};
        Quat rotation;
        float view[16] = {};            ///< orientation only : positions are brought to the camera before they are multiplied
        float projection[16] = {};
    };

    class Render {
    public:
        /// The active camera of the game's viewport (valid = false when there is none)
        static CameraView Camera()
        {
            ShardCameraInfo raw{};
            raw.structSize = sizeof(ShardCameraInfo);
            CameraView out;
            if (detail::RenderApi().camera_info(&raw) != SHARD_OK) return out;
            out.valid = raw.valid == SHARD_TRUE;
            out.width = raw.width; out.height = raw.height; out.orthographic = raw.orthographic == SHARD_TRUE;
            out.fovDegrees = raw.fovDegrees; out.orthoSize = raw.orthoSize; out.nearPlane = raw.nearPlane; out.farPlane = raw.farPlane;
            for (int i = 0; i < 3; ++i) out.position[i] = raw.position[i];
            out.rotation = Quat{ raw.rotation[0], raw.rotation[1], raw.rotation[2], raw.rotation[3] };
            std::memcpy(out.view, raw.view, sizeof(out.view));
            std::memcpy(out.projection, raw.projection, sizeof(out.projection));
            return out;
        }
        /// A point of the world on the viewport : x, y in pixels from the top left, z the distance along the view axis (negative : behind)
        static Vec3 WorldToScreen(const double position[3])
        {
            float out[3] = {};
            detail::RenderApi().world_to_screen(position, out);
            return { out[0], out[1], out[2] };
        }
        /// The ray through a pixel of the viewport
        static bool ScreenToRay(float x, float y, double origin[3], Vec3& direction)
        {
            float d[3];
            if (detail::RenderApi().screen_to_ray(x, y, origin, d) != SHARD_OK) return false;
            direction = Vec3{ d[0], d[1], d[2] };
            return true;
        }
    };
}
