#include "engine/platform/thread/thread.hpp"

#include "engine/platform/hardware/system_info.hpp"

#include <algorithm>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#else
    #include <pthread.h>
    #include <sched.h>
    #include <sys/resource.h>
    #include <sys/syscall.h>
    #include <unistd.h>
#endif

namespace Shard::Engine::Core::Platform {

    unsigned HardwareConcurrency()
    {
        return GetSystemInfo().cpu.logicalCores;
    }

    unsigned WorkerThreadCount()
    {
        const unsigned count = HardwareConcurrency();
        return count > 1 ? count - 1 : 1;
    }

    uint64_t CurrentThreadId()
    {
    #if defined(_WIN32)
        return GetCurrentThreadId();
    #else
        return static_cast<uint64_t>(syscall(SYS_gettid));
    #endif
    }

    void SetCurrentThreadName(const std::string& name)
    {
    #if defined(_WIN32)
        // SetThreadDescription only exists since Windows 10 1607 : looked up at runtime
        using SetThreadDescriptionFn = HRESULT (WINAPI*)(HANDLE, PCWSTR);
        static const auto setDescription = reinterpret_cast<SetThreadDescriptionFn>(reinterpret_cast<void*>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription")));
        if (!setDescription)
            return;

        const int length = MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, nullptr, 0);
        if (length <= 0)
            return;
        std::wstring wide(static_cast<size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, wide.data(), length);
        setDescription(GetCurrentThread(), wide.c_str());
    #elif defined(__APPLE__)
        pthread_setname_np(name.substr(0, 63).c_str());
    #else
        // 15 characters + terminator on Linux
        pthread_setname_np(pthread_self(), name.substr(0, 15).c_str());
    #endif
    }

    bool SetCurrentThreadPriority(ThreadPriority priority)
    {
    #if defined(_WIN32)
        int value = THREAD_PRIORITY_NORMAL;
        switch (priority)
        {
            case ThreadPriority::Lowest:  value = THREAD_PRIORITY_LOWEST; break;
            case ThreadPriority::Low:     value = THREAD_PRIORITY_BELOW_NORMAL; break;
            case ThreadPriority::Normal:  value = THREAD_PRIORITY_NORMAL; break;
            case ThreadPriority::High:    value = THREAD_PRIORITY_ABOVE_NORMAL; break;
            case ThreadPriority::Highest: value = THREAD_PRIORITY_HIGHEST; break;
        }
        return SetThreadPriority(GetCurrentThread(), value) != 0;
    #else
        // nice value of the calling thread (Linux threads have their own). Raising it needs privileges.
        int nice = 0;
        switch (priority)
        {
            case ThreadPriority::Lowest:  nice = 19; break;
            case ThreadPriority::Low:     nice = 10; break;
            case ThreadPriority::Normal:  nice = 0; break;
            case ThreadPriority::High:    nice = -5; break;
            case ThreadPriority::Highest: nice = -10; break;
        }
        return setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), nice) == 0;
    #endif
    }

    bool SetCurrentThreadAffinity(uint64_t mask)
    {
        if (mask == 0)
            return false;
    #if defined(_WIN32)
        return SetThreadAffinityMask(GetCurrentThread(), static_cast<DWORD_PTR>(mask)) != 0;
    #elif defined(__linux__)
        cpu_set_t set;
        CPU_ZERO(&set);
        for (int cpu = 0; cpu < 64; ++cpu)
            if (mask & (1ull << cpu))
                CPU_SET(cpu, &set);
        return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
    #else
        return false;   // macOS has no thread affinity
    #endif
    }

    Thread::Thread(const ThreadDesc& desc, std::function<void()> function)
    {
        m_Thread = std::thread([desc, function = std::move(function)]
        {
            if (!desc.name.empty())
                SetCurrentThreadName(desc.name);
            if (desc.priority != ThreadPriority::Normal)
                SetCurrentThreadPriority(desc.priority);
            if (desc.affinityMask != 0)
                SetCurrentThreadAffinity(desc.affinityMask);
            function();
        });
    }

    Thread& Thread::operator=(Thread&& other) noexcept
    {
        if (this != &other)
        {
            Join();
            m_Thread = std::move(other.m_Thread);
        }
        return *this;
    }
}
