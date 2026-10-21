// Shard C++ SDK : strings across the C ABI. Header-only, C++17 : compiled into the plugin, so it may use the standard library freely
// without touching the ABI (`sdk/include/shard/*.h` is the ABI ; nothing here crosses it but C types).
#pragma once

#include <cstring>
#include <string>
#include <string_view>

#include <shard/shard_abi.h>

namespace shard {

    /// A view as the ABI's string. The view must outlive the call it is passed to.
    inline ShardStr Str(std::string_view s) { return ShardStr{ s.data(), uint64_t(s.size()) }; }
    inline ShardStr Str(const char* s) { return ShardStr{ s, s ? uint64_t(std::strlen(s)) : 0u }; }
    inline ShardStr Str(const std::string& s) { return ShardStr{ s.data(), uint64_t(s.size()) }; }

    /// The ABI's string as a view : valid as long as the call that handed it
    inline std::string_view View(ShardStr s) { return s.data ? std::string_view(s.data, size_t(s.size)) : std::string_view(); }
    inline std::string ToString(ShardStr s) { return std::string(View(s)); }

    /// Reads a "text out" function of the ABI (buffer, capacity, *size) into a std::string : asks for the length, then the text.
    template <typename Fn>
    ShardResult ReadText(Fn&& call, std::string& out)
    {
        uint64_t size = 0;
        ShardResult r = call(static_cast<char*>(nullptr), uint64_t(0), &size);
        if (r != SHARD_OK) return r;
        out.assign(size_t(size), '\0');
        if (size == 0) return SHARD_OK;
        r = call(&out[0], size, &size);
        out.resize(size_t(size));
        return r;
    }
}
