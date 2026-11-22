#pragma once

#include <memory>
#include <string>

#include "../spdlog.h"

namespace spdlog
{
    inline std::shared_ptr<logger> stdout_color_mt(const std::string&)
    {
        return std::make_shared<logger>();
    }
}
