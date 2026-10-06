#if defined(_WIN32)

#include "engine/platform/lib_loader/dynlib.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace Shard::Engine::Core::Platform {

    namespace {
        std::string ErrorMessage(DWORD code)
        {
            char* buffer = nullptr;
            const DWORD length = FormatMessageA(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr, code, 0, reinterpret_cast<char*>(&buffer), 0, nullptr);

            std::string message = (length && buffer) ? std::string(buffer, length) : "unknown error";
            if (buffer)
                LocalFree(buffer);
            while (!message.empty() && (message.back() == '\n' || message.back() == '\r' || message.back() == ' '))
                message.pop_back();
            return message + " (code " + std::to_string(code) + ")";
        }
    }

    DynLib& DynLib::operator=(DynLib&& other) noexcept
    {
        if (this != &other)
        {
            Close();
            m_Handle = other.m_Handle;
            other.m_Handle = nullptr;
        }
        return *this;
    }

    bool DynLib::Open(const std::string& path)
    {
        Close();
        m_LastError.clear();

        m_Handle = LoadLibraryA(path.c_str());
        if (!m_Handle)
            m_LastError = ErrorMessage(GetLastError());
        return m_Handle != nullptr;
    }

    void DynLib::Close()
    {
        if (m_Handle)
        {
            FreeLibrary(static_cast<HMODULE>(m_Handle));
            m_Handle = nullptr;
        }
    }

    void* DynLib::Symbol(const std::string& name) const
    {
        if (!m_Handle)
            return nullptr;
        return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(m_Handle), name.c_str()));
    }

    const char* DynLib::Extension() { return ".dll"; }

    std::string DynLib::FileName(const std::string& name) { return name + ".dll"; }
}

#endif
