#pragma once

#include <string>

namespace Shard::Engine::Core::Platform {

    /// @brief A shared library (.dll / .so / .dylib) loaded in the process.
    class DynLib {
    public:
        DynLib() = default;
        DynLib(DynLib&& other) noexcept : m_Handle(other.m_Handle) { other.m_Handle = nullptr; }
        DynLib& operator=(DynLib&& other) noexcept;
        ~DynLib() { Close(); }

        DynLib(const DynLib&) = delete;
        DynLib& operator=(const DynLib&) = delete;

        /// @brief Loads the library. On failure LastError() tells why.
        bool Open(const std::string& path);
        void Close();
        bool IsOpen() const { return m_Handle != nullptr; }

        /// @brief Address of an exported symbol, nullptr if there is none.
        void* Symbol(const std::string& name) const;

        template <typename T>
        T Symbol(const std::string& name) const { return reinterpret_cast<T>(Symbol(name)); }

        const std::string& LastError() const { return m_LastError; }

        /// @brief Platform extension of a shared library, with the dot (".dll", ".so", ".dylib").
        static const char* Extension();

        /// @brief File name of the library called `name` : "name.dll" on Windows, "libname.so" elsewhere.
        static std::string FileName(const std::string& name);

    private:
        void* m_Handle = nullptr;
        std::string m_LastError;
    };
}
