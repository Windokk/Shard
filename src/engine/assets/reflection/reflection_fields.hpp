#pragma once

#include "reflection_types.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <map>
#include <set>
#include <typeinfo>
#include <cxxabi.h>
#include <memory>

enum FieldFlags : uint32_t {
    Editable     = 1 << 0,
    ReadOnly     = 1 << 1
};

struct Container{
    // element info
    TypeID elementType;
    size_t elementSize;
    
    // SEQUENTIAL containers (vector)
    void* (*GetByIndex)(void* container, size_t index);
    void  (*InsertAt)(void* container, size_t index, const void* element);

    // ASSOCIATIVE containers (map)
    void* (*FindByKey)(void* container, const void* key);
    void  (*InsertByKey)(void* container, const void* key, const void* value);

    // common queries
    size_t (*Size)(void* container);
    bool   (*IsAssociative)();
};

struct FieldInfo;

struct StructDescriptor
{
    std::string name;
    std::vector<FieldInfo*> fields;

    size_t size;

    void (*Construct)(void* data) = nullptr;
    void (*Destroy)(void* data) = nullptr;
    void (*Copy)(void* dst, const void* src) = nullptr;
    bool (*Equals)(const void* a, const void* b);
};

struct FieldInfo {
    // Identity
    const char* name;         // Field name
    TypeID type;              // Type stored in the component

    // Memory access
    uint32_t offset;          // Offset in the struct

    // Editor metadata
    uint32_t flags;           // Editable / ReadOnly
    float min;                // Optional min value for editor widgets
    float max;                // Optional max value for editor widgets

    const Container* container = nullptr;
    const EnumDescriptor* enumDesc = nullptr;

    void (*CopyConstruct)(void* dst, const void* src);
    void (*Assign)(void* dst, const void* src);
    void (*Destroy)(void* obj);
    bool (*Equals)(const void* a, const void* b);
};

struct ClassDescriptor{
    std::string name;
    std::vector<FieldInfo*> fields;
};

struct InstancedStruct
{
    const StructDescriptor* descriptor = nullptr;
    void* data = nullptr;

    inline bool operator==(const InstancedStruct& other) const
    {
        if (descriptor != other.descriptor)
            return false;

        if (!descriptor) // les deux sont null
            return true;

        if (descriptor->Equals)
            return descriptor->Equals(data, other.data);

        return std::memcmp(
            data,
            other.data,
            descriptor->size) == 0;
    }

    inline bool operator==(const InstancedStruct& other)
    {
        return static_cast<const InstancedStruct&>(*this) == other;
    }

    InstancedStruct(const InstancedStruct& other)
    {
        if (other.descriptor)
        {
            descriptor = other.descriptor;
            data = operator new(descriptor->size);
            descriptor->Copy(data, other.data);
        }
    }

    InstancedStruct& operator=(const InstancedStruct& other)
    {
        if (this == &other)
            return *this;

        Reset();

        if (other.descriptor)
        {
            descriptor = other.descriptor;
            data = operator new(descriptor->size);
            descriptor->Copy(data, other.data);
        }

        return *this;
    }

    InstancedStruct(InstancedStruct&& other) noexcept
    {
        descriptor = other.descriptor;
        data = other.data;

        other.descriptor = nullptr;
        other.data = nullptr;
    }

    InstancedStruct& operator=(InstancedStruct&& other) noexcept
    {
        if (this == &other)
            return *this;

        Reset();

        descriptor = other.descriptor;
        data = other.data;

        other.descriptor = nullptr;
        other.data = nullptr;

        return *this;
    }

    inline bool operator!=(const InstancedStruct& other)
    {
        if (descriptor != other.descriptor)
            return true;

        if (!descriptor)
            return false;

        if (descriptor->Equals)
            return !descriptor->Equals(data, other.data);

        return std::memcmp(
            data,
            other.data,
            descriptor->size) != 0;
    }

    inline bool operator!=(const InstancedStruct& other) const
    {
        if (descriptor != other.descriptor)
            return true;

        if (!descriptor)
            return false;

        if (descriptor->Equals)
            return !descriptor->Equals(data, other.data);

        return std::memcmp(
            data,
            other.data,
            descriptor->size) != 0;
    }

    InstancedStruct() = default;

    ~InstancedStruct()
    {
        Reset();
    }

    void Reset()
    {
        if (descriptor && data)
        {
            if (descriptor->Destroy)
                descriptor->Destroy(data);

            operator delete(data);
        }

        descriptor = nullptr;
        data = nullptr;
    }

    void Initialize(const StructDescriptor* desc)
    {
        Reset();

        descriptor = desc;
        data = operator new(desc->size);

        if (desc->Construct)
            desc->Construct(data);
        else
            std::memset(data, 0, desc->size);
    }
};

inline std::string demangle(const char* name) {
    int status = 0;
    std::unique_ptr<char, void(*)(void*)> res{
        abi::__cxa_demangle(name, nullptr, nullptr, &status),
        std::free
    };
    return (status == 0) ? res.get() : name;
}

template<typename StorageT>
Container MakeVectorContainer() {
    Container c{};

    c.elementType = GetTypeIDFromString(demangle(typeid(StorageT).name()));
    c.elementSize = sizeof(StorageT);

    c.Size = [](void* c) -> size_t {
        return static_cast<std::vector<StorageT>*>(c)->size();
    };

    c.IsAssociative = []() { return false; };

    c.GetByIndex = [](void* c, size_t i) -> void* {
        return &(*static_cast<std::vector<StorageT>*>(c))[i];
    };

    c.InsertAt = [](void* c, size_t i, const void* v) {
        auto& vec = *static_cast<std::vector<StorageT>*>(c);
        vec.insert(vec.begin() + i, *static_cast<const StorageT*>(v));
    };

    c.FindByKey = nullptr;
    c.InsertByKey = nullptr;

    return c;
}

template<typename K, typename V>
Container MakeMapContainer() {
    Container c{};

    c.elementType = GetTypeIDFromString(demangle(typeid(V).name()));
    c.elementSize = sizeof(V);

    c.Size = [](void* c) -> size_t {
        return static_cast<std::map<K, V>*>(c)->size();
    };

    c.IsAssociative = []() { return true; };

    c.GetByIndex = nullptr;
    c.InsertAt = nullptr;

    // associative ops
    c.FindByKey = [](void* c, const void* key) -> void* {
        auto& m = *static_cast<std::map<K, V>*>(c);
        auto it = m.find(*static_cast<const K*>(key));
        return it == m.end() ? nullptr : &it->second;
    };

    c.InsertByKey = [](void* c, const void* key, const void* value) {
        auto& m = *static_cast<std::map<K, V>*>(c);
        m[*static_cast<const K*>(key)] =
            *static_cast<const V*>(value);
    };
    
    return c;
}

inline void* FieldRead(const FieldInfo& field, void* object)
{
    return static_cast<uint8_t*>(object) + field.offset;
}

/// @brief Event describing a field change
/// @note This is kept as a struct in case we need to add more context to the event in the future
struct FieldChangedEvent
{
    const FieldInfo* field;
};

template<typename T>
void CopyConstruct(void* dst, const void* src)
{
    new (dst) T(*reinterpret_cast<const T*>(src));
}

template<typename T>
void Assign(void* dst, const void* src)
{
    *reinterpret_cast<T*>(dst) =
        *reinterpret_cast<const T*>(src);
}

template<typename T>
void Destroy(void* obj)
{
    reinterpret_cast<T*>(obj)->~T();
}

template<typename T>
bool Equals(const void* a, const void* b)
{
    return *reinterpret_cast<const T*>(a) ==
           *reinterpret_cast<const T*>(b);
}