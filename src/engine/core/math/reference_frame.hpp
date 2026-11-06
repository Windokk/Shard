#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/math/large_world.hpp"

namespace Shard::Engine::Core {

    using FrameId = uint32_t;

    inline constexpr FrameId kRootFrame = 0;
    inline constexpr FrameId kInvalidFrame = 0xFFFFFFFFu;

    struct ReferenceFrame {
        FrameId id = kRootFrame;
        FrameId parent = kInvalidFrame;
        std::string name;
        WorldPosition origin;
        DQuat rotation = DQuat(1.0, 0.0, 0.0, 0.0);
    };

    struct RelativePose {
        DVec3 translation = DVec3(0.0);
        DQuat rotation = DQuat(1.0, 0.0, 0.0, 0.0);

        bool IsIdentity() const { return translation == DVec3(0.0) && rotation == DQuat(1.0, 0.0, 0.0, 0.0); }
        DVec3 TransformPoint(const DVec3& p) const { return translation + rotation * p; }
        DVec3 TransformDirection(const DVec3& d) const { return rotation * d; }
        DMat4 ToMatrix() const { return ComposeTRS(translation, rotation, DVec3(1.0)); }
    };

    // Nested floating origins. Conversions go through the common ancestor in WorldPosition form, so near frames stay precise. Not thread-safe.
    class ReferenceFrameTree {
    public:
        ReferenceFrameTree();

        FrameId Create(const std::string& name, FrameId parent = kRootFrame,
                       const WorldPosition& origin = WorldPosition(), const DQuat& rotation = DQuat(1.0, 0.0, 0.0, 0.0));

        bool CreateWithId(FrameId id, const std::string& name, FrameId parent, const WorldPosition& origin, const DQuat& rotation);

        bool Remove(FrameId id);

        void Clear();

        bool Exists(FrameId id) const { return Find(id) != nullptr; }
        const ReferenceFrame* Find(FrameId id) const;
        const ReferenceFrame* FindByName(const std::string& name) const;
        const std::vector<ReferenceFrame>& GetFrames() const { return m_Frames; }
        size_t Count() const { return m_Frames.size(); }

        bool SetOrigin(FrameId id, const WorldPosition& origin);
        bool SetRotation(FrameId id, const DQuat& rotation);

        bool SetParent(FrameId id, FrameId newParent, bool keepWorldPose = true);

        uint64_t GetVersion() const { return m_Version; }

        bool IsAncestor(FrameId ancestor, FrameId frame) const;
        FrameId CommonAncestor(FrameId a, FrameId b) const;
        int Depth(FrameId id) const;

        WorldPosition GetOriginInRoot(FrameId frame) const;
        DQuat GetRotationInRoot(FrameId frame) const;

        WorldPosition ToRoot(FrameId frame, const DVec3& p) const;
        DVec3 FromRoot(FrameId frame, const WorldPosition& p) const;

        DVec3 ConvertPoint(FrameId from, FrameId to, const DVec3& p) const;
        DVec3 ConvertDirection(FrameId from, FrameId to, const DVec3& d) const;
        DQuat ConvertRotation(FrameId from, FrameId to, const DQuat& q) const;

        RelativePose GetRelativePose(FrameId source, FrameId target) const;

    private:
        ReferenceFrame* FindMutable(FrameId id);
        int IndexOf(FrameId id) const;

        std::vector<ReferenceFrame> m_Frames;
        FrameId m_NextId = 1;
        uint64_t m_Version = 1;
    };
}
