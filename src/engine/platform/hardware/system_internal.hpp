#pragma once

#include "engine/platform/hardware/system_info.hpp"

// What each OS implements (system_win32.cpp / system_posix.cpp) for the common part of the hardware module.
namespace Shard::Engine::Core::Platform::Internal {
    void QueryOsInfo(OsInfo& out);
    void QueryCoreCounts(CpuInfo& out);
}
