#include "engine/world/event_system.hpp"

#include <cstring>
#include <sstream>

namespace Shard::Engine::Events {

    namespace {
        template <typename T>
        T Read(const unsigned char* at)
        {
            T value;
            std::memcpy(&value, at, sizeof(T));     // the payload is bytes : no alignment to count on
            return value;
        }

        void WriteFloats(std::ostringstream& out, const unsigned char* at, int count)
        {
            out << "(";
            for (int i = 0; i < count; ++i) out << (i ? ", " : "") << Read<float>(at + i * sizeof(float));
            out << ")";
        }
    }

    uint32_t NamedEventFieldSize(NamedEventLayout::FieldType type)
    {
        using T = NamedEventLayout::FieldType;
        switch (type) {
            case T::F32: case T::I32: case T::U32: return 4;
            case T::F64: case T::I64: case T::U64: return 8;
            case T::Bool: return 1;
            case T::Vec2: return 8;
            case T::Vec3: return 12;
            case T::Vec4: case T::Quat: case T::Color: return 16;
        }
        return 0;
    }

    std::string FormatNamedEvent(const NamedEventLayout& layout, const void* payload, size_t size)
    {
        std::ostringstream out;
        if (size != layout.size || (size && !payload)) {
            out << "(" << size << " bytes)";
            return out.str();
        }
        const auto* bytes = static_cast<const unsigned char*>(payload);
        using T = NamedEventLayout::FieldType;
        out << "{ ";
        for (size_t i = 0; i < layout.fields.size(); ++i) {
            const NamedEventLayout::Field& f = layout.fields[i];
            if (f.offset + NamedEventFieldSize(f.type) > size) continue;
            const unsigned char* at = bytes + f.offset;
            out << (i ? ", " : "") << f.name << ": ";
            switch (f.type) {
                case T::F32: out << Read<float>(at); break;
                case T::F64: out << Read<double>(at); break;
                case T::I32: out << Read<int32_t>(at); break;
                case T::U32: out << Read<uint32_t>(at); break;
                case T::I64: out << Read<int64_t>(at); break;
                case T::U64: out << Read<uint64_t>(at); break;
                case T::Bool: out << (at[0] ? "true" : "false"); break;
                case T::Vec2: WriteFloats(out, at, 2); break;
                case T::Vec3: WriteFloats(out, at, 3); break;
                case T::Vec4: case T::Quat: case T::Color: WriteFloats(out, at, 4); break;
            }
        }
        out << " }";
        return out.str();
    }
}
