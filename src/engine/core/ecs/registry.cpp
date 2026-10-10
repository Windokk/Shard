#include "engine/core/ecs/registry.hpp"

#include <algorithm>
#include <cstring>

namespace Shard::Engine::Core::Ecs {

    // ---------------------------------------------------------------------------------- ComponentRegistry

    ComponentRegistry& ComponentRegistry::Global() {
        static ComponentRegistry instance;
        return instance;
    }

    ComponentId ComponentRegistry::Register(const char* uniqueName, size_t size, size_t align) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = m_ByName.find(uniqueName);
        if (it != m_ByName.end()) return it->second;
        const ComponentId id = static_cast<ComponentId>(m_Infos.size());
        ComponentInfo* info = new ComponentInfo{id, size, align, uniqueName};
        m_Infos.push_back(info);                                    // lives for the whole process
        m_ByName.emplace(uniqueName, id);
        return id;
    }

    const ComponentInfo& ComponentRegistry::Info(ComponentId id) const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return *m_Infos[id];
    }

    size_t ComponentRegistry::Count() const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Infos.size();
    }

    // ---------------------------------------------------------------------------------- Registry

    Registry::Registry() {
        m_EmptyArchetype = FindOrCreateArchetype(ComponentSet{});
    }

    Registry::~Registry() = default;

    const Registry::Record* Registry::RecordOf(Entity e) const {
        if (e.IsNull() || e.Index() >= m_Records.size()) return nullptr;
        const Record& record = m_Records[e.Index()];
        return (record.alive && record.generation == e.Generation()) ? &record : nullptr;
    }

    Registry::Record* Registry::RecordOf(Entity e) {
        return const_cast<Record*>(static_cast<const Registry*>(this)->RecordOf(e));
    }

    bool Registry::IsAlive(Entity e) const { return RecordOf(e) != nullptr; }

    Archetype* Registry::FindOrCreateArchetype(const ComponentSet& signature) {
        auto it = m_Archetypes.find(signature);
        if (it != m_Archetypes.end()) return it->second.get();
        auto archetype = std::make_unique<Archetype>(signature);
        Archetype* raw = archetype.get();
        m_Archetypes.emplace(signature, std::move(archetype));
        m_ArchetypeList.push_back(raw);
        return raw;
    }

    Archetype* Registry::ArchetypeWith(Archetype* from, ComponentId id) {
        if (Archetype* cached = from->AddEdge(id)) return cached;
        ComponentSet signature = from->Signature();
        signature.push_back(id);
        Normalize(signature);
        Archetype* target = FindOrCreateArchetype(signature);
        from->SetAddEdge(id, target);
        return target;
    }

    Archetype* Registry::ArchetypeWithout(Archetype* from, ComponentId id) {
        if (Archetype* cached = from->RemoveEdge(id)) return cached;
        ComponentSet signature = from->Signature();
        signature.erase(std::remove(signature.begin(), signature.end(), id), signature.end());
        Archetype* target = FindOrCreateArchetype(signature);
        from->SetRemoveEdge(id, target);
        return target;
    }

    std::vector<Archetype*> Registry::Matching(const ComponentSet& required, const ComponentSet& excluded) const {
        std::vector<Archetype*> out;
        for (Archetype* archetype : m_ArchetypeList) {
            if (archetype->Count() == 0) continue;
            if (!IsSubset(required, archetype->Signature())) continue;
            if (!excluded.empty() && Intersects(excluded, archetype->Signature())) continue;
            out.push_back(archetype);
        }
        return out;
    }

    Entity Registry::AllocateEntity() {
        uint32_t index;
        if (!m_FreeIndices.empty()) {
            index = m_FreeIndices.back();
            m_FreeIndices.pop_back();
        } else {
            index = static_cast<uint32_t>(m_Records.size());
            m_Records.emplace_back();
        }
        Record& record = m_Records[index];
        record.alive = true;
        ++m_AliveCount;
        ++m_StructuralVersion;
        return Entity::Make(index, record.generation);
    }

    void Registry::ReleaseEntity(Entity e) {
        Record& record = m_Records[e.Index()];
        record = Record{record.generation, false};
        // The next user of this slot gets a different generation, so `e` is dead from now on. The all-ones generation is reserved
        // for command buffer placeholders, and 0 for the null entity.
        record.generation = (record.generation + 1 >= CommandBuffer::kPlaceholderGeneration) ? 1 : record.generation + 1;
        m_FreeIndices.push_back(e.Index());
        --m_AliveCount;
        ++m_StructuralVersion;
    }

    Entity Registry::Create() {
        if (IsIterating()) return kNullEntity;
        const Entity e = AllocateEntity();
        Record& record = m_Records[e.Index()];
        record.archetype = m_EmptyArchetype;
        record.row = m_EmptyArchetype->Push(e);
        return e;
    }

    void Registry::EraseFromArchetype(Record& record) {
        const Entity moved = record.archetype->RemoveSwap(record.row);
        if (!moved.IsNull()) m_Records[moved.Index()].row = record.row;
        record.archetype = nullptr;
    }

    void Registry::Unlink(Entity e) {
        Record& record = m_Records[e.Index()];
        if (record.parent.IsNull()) return;
        Record& parent = m_Records[record.parent.Index()];
        if (record.prev) m_Records[record.prev.Index()].next = record.next; else parent.firstChild = record.next;
        if (record.next) m_Records[record.next.Index()].prev = record.prev; else parent.lastChild = record.prev;
        --parent.childCount;
        record.parent = record.prev = record.next = kNullEntity;
    }

    bool Registry::Destroy(Entity e) {
        if (IsIterating() || !IsAlive(e)) return false;

        std::vector<Entity> doomed{e};
        ForEachDescendant(e, [&doomed](Entity d) { doomed.push_back(d); });
        Unlink(e);                                                  // only the root is attached to something that survives

        for (Entity d : doomed) {
            Record& record = m_Records[d.Index()];
            EraseFromArchetype(record);
            ReleaseEntity(d);
        }
        return true;
    }

    void Registry::MoveTo(Entity e, Archetype* target) {
        Record& record = m_Records[e.Index()];
        Archetype* source = record.archetype;
        const uint32_t sourceRow = record.row;
        const uint32_t newRow = target->Push(e);
        target->CopyCommon(newRow, *source, sourceRow);
        const Entity moved = source->RemoveSwap(sourceRow);
        if (!moved.IsNull()) m_Records[moved.Index()].row = sourceRow;
        record.archetype = target;
        record.row = newRow;
        ++m_StructuralVersion;
    }

    bool Registry::HasRaw(Entity e, ComponentId id) const {
        const Record* record = RecordOf(e);
        return record && record->archetype->Has(id);
    }

    void* Registry::GetRaw(Entity e, ComponentId id) const {
        const Record* record = RecordOf(e);
        return record ? record->archetype->ComponentPtr(record->row, id) : nullptr;
    }

    bool Registry::AddRaw(Entity e, ComponentId id, const void* data) {
        Record* record = RecordOf(e);
        if (!record) return false;
        const size_t size = ComponentRegistry::Global().Info(id).size;

        if (record->archetype->Has(id)) {                           // not structural : just a new value
            std::memcpy(record->archetype->ComponentPtr(record->row, id), data, size);
            return true;
        }
        if (IsIterating()) return false;

        MoveTo(e, ArchetypeWith(record->archetype, id));
        record = RecordOf(e);
        void* slot = record->archetype->ComponentPtr(record->row, id);
        if (data) std::memcpy(slot, data, size); else std::memset(slot, 0, size);
        return true;
    }

    bool Registry::RemoveRaw(Entity e, ComponentId id) {
        Record* record = RecordOf(e);
        if (!record || IsIterating() || !record->archetype->Has(id)) return false;
        MoveTo(e, ArchetypeWithout(record->archetype, id));
        return true;
    }

    // ---------------------------------------------------------------------------------- hierarchy

    bool Registry::IsAncestor(Entity ancestor, Entity e) const {
        const Record* record = RecordOf(e);
        while (record && !record->parent.IsNull()) {
            if (record->parent == ancestor) return true;
            record = RecordOf(record->parent);
        }
        return false;
    }

    bool Registry::SetParent(Entity child, Entity parent) {
        Record* childRecord = RecordOf(child);
        if (!childRecord) return false;
        if (!parent.IsNull()) {
            if (!IsAlive(parent) || parent == child || IsAncestor(child, parent)) return false;     // a cycle would never end
        }
        if (childRecord->parent == parent) return true;

        Unlink(child);
        if (parent.IsNull()) return true;

        Record& parentRecord = m_Records[parent.Index()];
        childRecord = &m_Records[child.Index()];
        childRecord->parent = parent;
        childRecord->prev = parentRecord.lastChild;
        childRecord->next = kNullEntity;
        if (parentRecord.lastChild) m_Records[parentRecord.lastChild.Index()].next = child; else parentRecord.firstChild = child;
        parentRecord.lastChild = child;
        ++parentRecord.childCount;
        return true;
    }

    Entity Registry::GetParent(Entity e) const { const Record* r = RecordOf(e); return r ? r->parent : kNullEntity; }
    Entity Registry::FirstChild(Entity e) const { const Record* r = RecordOf(e); return r ? r->firstChild : kNullEntity; }
    Entity Registry::NextSibling(Entity e) const { const Record* r = RecordOf(e); return r ? r->next : kNullEntity; }
    size_t Registry::ChildCount(Entity e) const { const Record* r = RecordOf(e); return r ? r->childCount : 0; }

    void Registry::PushChildrenReversed(Entity parent, std::vector<Entity>& stack) const {
        const size_t first = stack.size();
        for (Entity c = FirstChild(parent); c; c = NextSibling(c)) stack.push_back(c);
        std::reverse(stack.begin() + static_cast<std::ptrdiff_t>(first), stack.end());           // so the first child is popped first
    }

    // ---------------------------------------------------------------------------------- command buffers

    Registry::PlaybackResult Registry::Playback(CommandBuffer& buffer) {
        PlaybackResult result;
        if (IsIterating()) {
            result.skipped = buffer.m_Commands.size();
            return result;
        }

        std::vector<Entity> created;                                // placeholder ordinal -> real entity
        auto resolve = [&created](Entity e) -> Entity {
            if (!CommandBuffer::IsPlaceholder(e)) return e;
            return e.Index() < created.size() ? created[e.Index()] : kNullEntity;
        };

        for (const CommandBuffer::Command& command : buffer.m_Commands) {
            bool done = false;
            switch (command.op) {
                case CommandBuffer::Op::Create:
                    created.push_back(Create());
                    done = true;
                    break;
                case CommandBuffer::Op::Destroy:
                    done = Destroy(resolve(command.target));
                    break;
                case CommandBuffer::Op::Add:
                    done = AddRaw(resolve(command.target), command.component, buffer.m_Data.data() + command.dataOffset);
                    break;
                case CommandBuffer::Op::Remove:
                    done = RemoveRaw(resolve(command.target), command.component);
                    break;
                case CommandBuffer::Op::SetParent:
                    done = SetParent(resolve(command.target), resolve(command.other));
                    break;
            }
            if (done) ++result.applied; else ++result.skipped;
        }
        buffer.Clear();
        return result;
    }
}
