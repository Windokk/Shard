#if !defined(_WIN32)

#include "engine/platform/lib_loader/dynlib.hpp"

#include <dlfcn.h>

namespace Shard::Engine::Core::Platform {

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

        m_Handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!m_Handle)
        {
            const char* error = dlerror();
            m_LastError = error ? error : "unknown error";
        }
        return m_Handle != nullptr;
    }

    void DynLib::Close()
    {
        if (m_Handle)
        {
            dlclose(m_Handle);
            m_Handle = nullptr;
        }
    }

    void* DynLib::Symbol(const std::string& name) const
    {
        return m_Handle ? dlsym(m_Handle, name.c_str()) : nullptr;
    }

#if defined(__APPLE__)
    const char* DynLib::Extension() { return ".dylib"; }
    std::string DynLib::FileName(const std::string& name) { return "lib" + name + ".dylib"; }
#else
    const char* DynLib::Extension() { return ".so"; }
    std::string DynLib::FileName(const std::string& name) { return "lib" + name + ".so"; }
#endif
}

#endif
