#include "reference_frame.hpp"

#include <algorithm>

namespace Shard::Engine::Core {

    ReferenceFrameTree::ReferenceFrameTree()
    {
        ReferenceFrame root;
        root.id = kRootFrame;
        root.parent = kInvalidFrame;
        root.name = "Root";
        m_Frames.push_back(root);
    }

    int ReferenceFrameTree::IndexOf(FrameId id) const
    {
        for (size_t i = 0; i < m_Frames.size(); i++)
            if (m_Frames[i].id == id)
                return int(i);
        return -1;
    }

    const ReferenceFrame* ReferenceFrameTree::Find(FrameId id) const
    {
        const int i = IndexOf(id);
        return i < 0 ? nullptr : &m_Frames[size_t(i)];
    }

    ReferenceFrame* ReferenceFrameTree::FindMutable(FrameId id)
    {
        const int i = IndexOf(id);
        return i < 0 ? nullptr : &m_Frames[size_t(i)];
    }

    const ReferenceFrame* ReferenceFrameTree::FindByName(const std::string& name) const
    {
        for (const ReferenceFrame& f : m_Frames)
            if (f.name == name)
                return &f;
        return nullptr;
    }

    FrameId ReferenceFrameTree::Create(const std::string& name, FrameId parent, const WorldPosition& origin, const DQuat& rotation)
    {
        while (Exists(m_NextId))
            m_NextId++;
        const FrameId id = m_NextId;
        return CreateWithId(id, name, parent, origin, rotation) ? id : kInvalidFrame;
    }

    bool ReferenceFrameTree::CreateWithId(FrameId id, const std::string& name, FrameId parent, const WorldPosition& origin, const DQuat& rotation)
    {
        if (id == kInvalidFrame || Exists(id) || !Exists(parent))
            return false;

        ReferenceFrame f;
        f.id = id;
        f.parent = parent;
        f.name = name;
        f.origin = origin;
        f.rotation = glm::normalize(rotation);
        m_Frames.push_back(std::move(f));
        m_NextId = std::max(m_NextId, id + 1);
        m_Version++;
        return true;
    }

    bool ReferenceFrameTree::Remove(FrameId id)
    {
        if (id == kRootFrame)
            return false;
        const ReferenceFrame* frame = Find(id);
        if (!frame)
            return false;

        const FrameId parent = frame->parent;
        // The children keep their place in the world : rewritten against the grandparent before the frame goes
        std::vector<FrameId> children;
        for (const ReferenceFrame& f : m_Frames)
            if (f.parent == id)
                children.push_back(f.id);
        for (FrameId child : children)
            SetParent(child, parent, true);

        m_Frames.erase(m_Frames.begin() + IndexOf(id));
        m_Version++;
        return true;
    }

    void ReferenceFrameTree::Clear()
    {
        m_Frames.resize(1);
        m_NextId = 1;
        m_Version++;
    }

    bool ReferenceFrameTree::SetOrigin(FrameId id, const WorldPosition& origin)
    {
        if (id == kRootFrame)
            return false;
        ReferenceFrame* f = FindMutable(id);
        if (!f)
            return false;
        f->origin = origin;
        m_Version++;
        return true;
    }

    bool ReferenceFrameTree::SetRotation(FrameId id, const DQuat& rotation)
    {
        if (id == kRootFrame)
            return false;
        ReferenceFrame* f = FindMutable(id);
        if (!f)
            return false;
        f->rotation = glm::normalize(rotation);
        m_Version++;
        return true;
    }

    bool ReferenceFrameTree::SetParent(FrameId id, FrameId newParent, bool keepWorldPose)
    {
        if (id == kRootFrame || !Exists(newParent) || IsAncestor(id, newParent))
            return false;
        ReferenceFrame* f = FindMutable(id);
        if (!f)
            return false;

        if (keepWorldPose) {
            const WorldPosition rootOrigin = GetOriginInRoot(id);
            const DQuat rootRotation = GetRotationInRoot(id);
            const WorldPosition parentOrigin = GetOriginInRoot(newParent);
            const DQuat parentInverse = glm::inverse(GetRotationInRoot(newParent));

            f->origin = WorldPosition::FromDVec3(parentInverse * WorldPosition::Offset(rootOrigin, parentOrigin));
            f->rotation = glm::normalize(parentInverse * rootRotation);
        }
        f->parent = newParent;
        m_Version++;
        return true;
    }

    bool ReferenceFrameTree::IsAncestor(FrameId ancestor, FrameId frame) const
    {
        // A frame counts as its own ancestor
        for (const ReferenceFrame* f = Find(frame); f; f = f->parent == kInvalidFrame ? nullptr : Find(f->parent))
            if (f->id == ancestor)
                return true;
        return false;
    }

    int ReferenceFrameTree::Depth(FrameId id) const
    {
        int depth = 0;
        for (const ReferenceFrame* f = Find(id); f && f->parent != kInvalidFrame; f = Find(f->parent))
            depth++;
        return depth;
    }

    FrameId ReferenceFrameTree::CommonAncestor(FrameId a, FrameId b) const
    {
        if (!Exists(a) || !Exists(b))
            return kInvalidFrame;
        int da = Depth(a);
        int db = Depth(b);
        while (da > db) { a = Find(a)->parent; da--; }
        while (db > da) { b = Find(b)->parent; db--; }
        while (a != b) {
            a = Find(a)->parent;
            b = Find(b)->parent;
        }
        return a;
    }

    WorldPosition ReferenceFrameTree::GetOriginInRoot(FrameId frame) const
    {
        return ToRoot(frame, DVec3(0.0));
    }

    DQuat ReferenceFrameTree::GetRotationInRoot(FrameId frame) const
    {
        DQuat q(1.0, 0.0, 0.0, 0.0);
        for (const ReferenceFrame* f = Find(frame); f && f->parent != kInvalidFrame; f = Find(f->parent))
            q = f->rotation * q;
        return q;
    }

    WorldPosition ReferenceFrameTree::ToRoot(FrameId frame, const DVec3& p) const
    {
        // Climb one frame at a time. The first step adds a small vector to the frame's exact origin ; later steps rotate the
        // (possibly large) position, which only costs the double's precision at that distance.
        WorldPosition wp = WorldPosition::FromDVec3(p);
        for (const ReferenceFrame* f = Find(frame); f && f->parent != kInvalidFrame; f = Find(f->parent)) {
            const bool rotates = f->rotation != DQuat(1.0, 0.0, 0.0, 0.0);
            const DVec3 inParent = rotates ? f->rotation * wp.ToDVec3() : wp.ToDVec3();
            wp = f->origin + inParent;
        }
        return wp;
    }

    DVec3 ReferenceFrameTree::FromRoot(FrameId frame, const WorldPosition& p) const
    {
        // The chain from the root down to `frame`
        std::vector<const ReferenceFrame*> chain;
        for (const ReferenceFrame* f = Find(frame); f && f->parent != kInvalidFrame; f = Find(f->parent))
            chain.push_back(f);

        WorldPosition wp = p;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            const ReferenceFrame& f = **it;
            const DVec3 rel = WorldPosition::Offset(wp, f.origin);          // exact when wp is near the frame
            wp = WorldPosition::FromDVec3(glm::inverse(f.rotation) * rel);
        }
        return wp.ToDVec3();
    }

    DVec3 ReferenceFrameTree::ConvertPoint(FrameId from, FrameId to, const DVec3& p) const
    {
        if (from == to)
            return p;
        const FrameId ancestor = CommonAncestor(from, to);
        if (ancestor == kInvalidFrame)
            return p;

        // Up : from `from` to the common ancestor
        WorldPosition wp = WorldPosition::FromDVec3(p);
        for (const ReferenceFrame* f = Find(from); f && f->id != ancestor; f = Find(f->parent)) {
            const bool rotates = f->rotation != DQuat(1.0, 0.0, 0.0, 0.0);
            const DVec3 inParent = rotates ? f->rotation * wp.ToDVec3() : wp.ToDVec3();
            wp = f->origin + inParent;
        }

        // Down : from the common ancestor to `to`
        std::vector<const ReferenceFrame*> chain;
        for (const ReferenceFrame* f = Find(to); f && f->id != ancestor; f = Find(f->parent))
            chain.push_back(f);
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            const ReferenceFrame& f = **it;
            const DVec3 rel = WorldPosition::Offset(wp, f.origin);
            wp = WorldPosition::FromDVec3(glm::inverse(f.rotation) * rel);
        }
        return wp.ToDVec3();
    }

    DVec3 ReferenceFrameTree::ConvertDirection(FrameId from, FrameId to, const DVec3& d) const
    {
        if (from == to)
            return d;
        // from -> root -> to : only the rotations count
        return glm::inverse(GetRotationInRoot(to)) * (GetRotationInRoot(from) * d);
    }

    DQuat ReferenceFrameTree::ConvertRotation(FrameId from, FrameId to, const DQuat& q) const
    {
        if (from == to)
            return q;
        return glm::normalize(glm::inverse(GetRotationInRoot(to)) * GetRotationInRoot(from) * q);
    }

    RelativePose ReferenceFrameTree::GetRelativePose(FrameId source, FrameId target) const
    {
        RelativePose pose;
        if (source == target)
            return pose;
        pose.translation = ConvertPoint(source, target, DVec3(0.0));
        pose.rotation = ConvertRotation(source, target, DQuat(1.0, 0.0, 0.0, 0.0));
        return pose;
    }
}
