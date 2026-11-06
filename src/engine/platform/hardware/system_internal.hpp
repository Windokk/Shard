#pragma once

#include "engine/platform/hardware/system_info.hpp"

namespace Shard::Engine::Core::Platform::Internal {
    void QueryOsInfo(OsInfo& out);
    void QueryCoreCounts(CpuInfo& out);
}
