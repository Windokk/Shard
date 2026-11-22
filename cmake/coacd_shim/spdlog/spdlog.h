#pragma once

#include <memory>
#include <string_view>

namespace spdlog
{
    namespace level
    {
        enum level_enum { trace, debug, info, warn, err, critical, off };
    }

    class logger
    {
        public:
            template<class... Args> void debug(std::string_view, const Args&...) {}
            template<class... Args> void info(std::string_view, const Args&...) {}
            template<class... Args> void warn(std::string_view, const Args&...) {}
            template<class... Args> void error(std::string_view, const Args&...) {}
            template<class... Args> void critical(std::string_view, const Args&...) {}
            void set_level(level::level_enum) {}
    };
}
