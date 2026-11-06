#pragma once

#include <string>

namespace Shard::Engine::Core::Platform {

    class DynLib {
    public:
        DynLib() = default;
        DynLib(DynLib&& other) noexcept : m_Handle(other.m_Handle) { other.m_Handle = nullptr; }
        DynLib& operator=(DynLib&& other) noexcept;
        ~DynLib() { Close(); }

        DynLib(const DynLib&) = delete;
        DynLib& operator=(const DynLib&) = delete;

        bool Open(const std::string& path);

        bool OpenIfLoaded(const std::string& name);
        void Close();
        bool IsOpen() const { return m_Handle != nullptr; }

        void* Symbol(const std::string& name) const;

        template <typename T>
        T Symbol(const std::string& name) const { return reinterpret_cast<T>(Symbol(name)); }

        const std::string& LastError() const { return m_LastError; }

        static const char* Extension();

        static std::string FileName(const std::string& name);

    private:
        void* m_Handle = nullptr;
        std::string m_LastError;
    };
}
