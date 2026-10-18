#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/math/large_world.hpp"

namespace Shard::Engine::Core {

    using FrameId = uint32_t;

    /// The root frame : always there, never moves, every other frame descends from it
    inline constexpr FrameId kRootFrame = 0;
    inline constexpr FrameId kInvalidFrame = 0xFFFFFFFFu;

    /// @brief One reference frame : a coordinate system placed inside its parent's.
    /// Things that live in a frame use small float / double numbers measured from the frame's origin ; the big numbers are
    /// only in the frames' origins (WorldPosition : cell + local, so they stay exact).
    struct ReferenceFrame {
        FrameId id = kRootFrame;
        FrameId parent = kInvalidFrame;                 ///< invalid for the root only
        std::string name;
        WorldPosition origin;                           ///< where this frame's origin is, in the PARENT frame
        DQuat rotation = DQuat(1.0, 0.0, 0.0, 0.0);     ///< this frame's axes, in the parent frame
    };

    /// @brief The pose of one frame in another : p_in_target = rotation * p_in_source + translation (as an exact offset
    /// from the target frame's origin when the frames are near, see ReferenceFrameTree::GetRelativePose).
    struct RelativePose {
        DVec3 translation = DVec3(0.0);
        DQuat rotation = DQuat(1.0, 0.0, 0.0, 0.0);

        bool IsIdentity() const { return translation == DVec3(0.0) && rotation == DQuat(1.0, 0.0, 0.0, 0.0); }
        DVec3 TransformPoint(const DVec3& p) const { return translation + rotation * p; }
        DVec3 TransformDirection(const DVec3& d) const { return rotation * d; }
        DMat4 ToMatrix() const { return ComposeTRS(translation, rotation, DVec3(1.0)); }
    };

    /// @brief The nested reference frames of a world : a tree whose root is the universe, and whose nodes are
    /// floating origins (a planet, a ship, a station, a streaming cell...).
    ///
    /// Converting between two frames climbs to their nearest common ancestor and comes down again. The two legs are
    /// carried in WorldPosition form, so two frames that are far from the root but near each other (a ship above a planet,
    /// a planet in a distant system) convert with the precision of their small distance, not of their distance from the root.
    ///
    /// Not thread-safe : the simulation owns it, the Extract stage reads it on the simulation thread.
    class ReferenceFrameTree {
    public:
        ReferenceFrameTree();

        /// @brief Adds a frame under `parent`. The origin and the rotation are expressed in the parent. Returns kInvalidFrame
        /// if the parent does not exist.
        FrameId Create(const std::string& name, FrameId parent = kRootFrame,
                       const WorldPosition& origin = WorldPosition(), const DQuat& rotation = DQuat(1.0, 0.0, 0.0, 0.0));

        /// @brief Same, but with an id the caller chose (loading a saved world). False if the id is taken or the parent is missing.
        bool CreateWithId(FrameId id, const std::string& name, FrameId parent, const WorldPosition& origin, const DQuat& rotation);

        /// @brief Removes a frame (not the root). Its children are re-parented to its parent and keep their place in the world.
        bool Remove(FrameId id);

        /// @brief Removes every frame but the root
        void Clear();

        bool Exists(FrameId id) const { return Find(id) != nullptr; }
        const ReferenceFrame* Find(FrameId id) const;
        const ReferenceFrame* FindByName(const std::string& name) const;
        const std::vector<ReferenceFrame>& GetFrames() const { return m_Frames; }
        size_t Count() const { return m_Frames.size(); }

        /// @brief Moves / turns a frame inside its parent. Everything that lives in the frame (and in its descendants) moves with it.
        bool SetOrigin(FrameId id, const WorldPosition& origin);
        bool SetRotation(FrameId id, const DQuat& rotation);

        /// @brief Puts `id` under `newParent`. With keepWorldPose the frame's origin and rotation are rewritten so that it does not
        /// move in the world. Refused (false) if it would make the frame its own ancestor.
        bool SetParent(FrameId id, FrameId newParent, bool keepWorldPose = true);

        /// @brief Incremented by every change of the tree : lets a cache know when to rebuild
        uint64_t GetVersion() const { return m_Version; }

        bool IsAncestor(FrameId ancestor, FrameId frame) const;
        FrameId CommonAncestor(FrameId a, FrameId b) const;
        int Depth(FrameId id) const;

        // ---- Conversions. `p` is relative to the origin of the frame it is given in.

        /// @brief The frame's origin in the root frame, as an exact position
        WorldPosition GetOriginInRoot(FrameId frame) const;
        /// @brief The frame's axes in the root frame
        DQuat GetRotationInRoot(FrameId frame) const;

        /// @brief A point of `frame` as an exact position in the root frame
        WorldPosition ToRoot(FrameId frame, const DVec3& p) const;
        /// @brief The same point of the root frame, expressed in `frame`
        DVec3 FromRoot(FrameId frame, const WorldPosition& p) const;

        /// @brief A point of `from`, expressed in `to`
        DVec3 ConvertPoint(FrameId from, FrameId to, const DVec3& p) const;
        /// @brief A direction (or an extent) of `from`, expressed in `to` : only the rotations apply
        DVec3 ConvertDirection(FrameId from, FrameId to, const DVec3& d) const;
        DQuat ConvertRotation(FrameId from, FrameId to, const DQuat& q) const;

        /// @brief Where `source`'s origin and axes are, seen from `target` (target = parent of source gives the source's own origin
        /// and rotation). Exact for near frames : the offset is a difference of WorldPositions.
        RelativePose GetRelativePose(FrameId source, FrameId target) const;

    private:
        ReferenceFrame* FindMutable(FrameId id);
        int IndexOf(FrameId id) const;

        std::vector<ReferenceFrame> m_Frames;           // m_Frames[0] is the root ; sparse ids are allowed
        FrameId m_NextId = 1;
        uint64_t m_Version = 1;
    };
}
