// Shard C++ SDK : console variables (shard_cvars.h) and commands (shard_commands.h).
#pragma once

#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <shard/shard_commands.h>
#include <shard/shard_cvars.h>
#include <shard/cpp/detail.hpp>

namespace Shard {

    namespace detail {
        inline const ShardCVarsApi& CVarsApi() { return Require<ShardCVarsApi>(SHARD_IFACE_CVARS); }
    }

    /// A variable of this plugin, named "<plugin id>.<name>". Its handle goes stale with the plugin's context (disable, unload).
    template <typename T>
    class CVar;

    template <>
    class CVar<bool> {
    public:
        CVar() = default;
        CVar(ShardPluginCtx ctx, ShardCVar handle) : m_Ctx(ctx), m_Handle(handle) {}
        bool Get() const { int32_t v = 0; detail::CVarsApi().get_bool(m_Ctx, m_Handle, &v); return v != 0; }
        bool Set(bool value) const { return detail::CVarsApi().set_bool(m_Ctx, m_Handle, value ? 1 : 0) == SHARD_OK; }
        explicit operator bool() const { return m_Handle != 0; }
    private:
        ShardPluginCtx m_Ctx = 0;
        ShardCVar m_Handle = 0;
    };

    template <>
    class CVar<int32_t> {
    public:
        CVar() = default;
        CVar(ShardPluginCtx ctx, ShardCVar handle) : m_Ctx(ctx), m_Handle(handle) {}
        int32_t Get() const { int32_t v = 0; detail::CVarsApi().get_int(m_Ctx, m_Handle, &v); return v; }
        bool Set(int32_t value) const { return detail::CVarsApi().set_int(m_Ctx, m_Handle, value) == SHARD_OK; }
        explicit operator bool() const { return m_Handle != 0; }
    private:
        ShardPluginCtx m_Ctx = 0;
        ShardCVar m_Handle = 0;
    };

    template <>
    class CVar<float> {
    public:
        CVar() = default;
        CVar(ShardPluginCtx ctx, ShardCVar handle) : m_Ctx(ctx), m_Handle(handle) {}
        float Get() const { float v = 0.f; detail::CVarsApi().get_float(m_Ctx, m_Handle, &v); return v; }
        bool Set(float value) const { return detail::CVarsApi().set_float(m_Ctx, m_Handle, value) == SHARD_OK; }
        explicit operator bool() const { return m_Handle != 0; }
    private:
        ShardPluginCtx m_Ctx = 0;
        ShardCVar m_Handle = 0;
    };

    template <>
    class CVar<std::string> {
    public:
        CVar() = default;
        CVar(ShardPluginCtx ctx, ShardCVar handle) : m_Ctx(ctx), m_Handle(handle) {}
        std::string Get() const
        {
            std::string out;
            ReadText([&](char* b, uint64_t c, uint64_t* s) { return detail::CVarsApi().get_string(m_Ctx, m_Handle, b, c, s); }, out);
            return out;
        }
        bool Set(std::string_view value) const { return detail::CVarsApi().set_string(m_Ctx, m_Handle, Str(value)) == SHARD_OK; }
        explicit operator bool() const { return m_Handle != 0; }
    private:
        ShardPluginCtx m_Ctx = 0;
        ShardCVar m_Handle = 0;
    };

    class CVars {
    public:
        explicit CVars(ShardPluginCtx ctx) : m_Ctx(ctx) {}

        // Throw Shard::Error if the host refuses (the name is taken...). thread : main
        CVar<bool> Bool(const std::string& name, bool value, const std::string& help = {}, uint32_t flags = 0) const
        {
            ShardCVar h = 0;
            detail::Check(detail::CVarsApi().create_bool(m_Ctx, Str(name), value ? 1 : 0, Str(help), flags, &h), "cvar '" + name + "'");
            return CVar<bool>(m_Ctx, h);
        }
        CVar<int32_t> Int(const std::string& name, int32_t value, const std::string& help = {}, int32_t min = std::numeric_limits<int32_t>::lowest(),
                          int32_t max = std::numeric_limits<int32_t>::max(), uint32_t flags = 0) const
        {
            ShardCVar h = 0;
            detail::Check(detail::CVarsApi().create_int(m_Ctx, Str(name), value, Str(help), flags, min, max, &h), "cvar '" + name + "'");
            return CVar<int32_t>(m_Ctx, h);
        }
        CVar<float> Float(const std::string& name, float value, const std::string& help = {}, float min = std::numeric_limits<float>::lowest(),
                          float max = std::numeric_limits<float>::max(), uint32_t flags = 0) const
        {
            ShardCVar h = 0;
            detail::Check(detail::CVarsApi().create_float(m_Ctx, Str(name), value, Str(help), flags, min, max, &h), "cvar '" + name + "'");
            return CVar<float>(m_Ctx, h);
        }
        CVar<std::string> String(const std::string& name, const std::string& value, const std::string& help = {}, uint32_t flags = 0) const
        {
            ShardCVar h = 0;
            detail::Check(detail::CVarsApi().create_string(m_Ctx, Str(name), Str(value), Str(help), flags, &h), "cvar '" + name + "'");
            return CVar<std::string>(m_Ctx, h);
        }

    private:
        ShardPluginCtx m_Ctx;
    };

    /// A command's words (after its name), and the text it answers ; return false for a failure
    using CommandFn = std::function<bool(const std::vector<std::string_view>& args, std::string& output)>;

    class Commands {
    public:
        explicit Commands(ShardPluginCtx ctx) : m_Ctx(ctx) {}
        /// Named as given : prefix it with your own ("weather.set"). Throws Shard::Error if this plugin has it already. thread : main
        void Add(const std::string& name, const std::string& help, CommandFn fn) const
        {
            const auto& api = detail::Require<ShardCommandsApi>(SHARD_IFACE_COMMANDS);
            detail::Check(api.add(m_Ctx, Str(name), Str(help), &Trampoline, detail::Keep(Command{ std::move(fn), {} })), "command '" + name + "'");
        }

        /// The outcome of running a console line
        struct Result {
            bool ok = false;            ///< the command ran and succeeded
            bool found = true;          ///< false : the line names no command
            std::string output;
        };
        /// Runs a console line ("world.load arena") as if it was typed (ABI 1.7). A command that changes what the game simulates is the caller's to run
        /// from the simulation thread. thread : main
        Result Execute(std::string_view line) const
        {
            const ShardCommandsApi* api = detail::Interface<ShardCommandsApi>(SHARD_IFACE_COMMANDS, 1);
            Result r;
            if (!api) { r.found = false; return r; }
            const ShardResult code = ReadText([&](char* b, uint64_t c, uint64_t* s) { return api->execute(m_Ctx, Str(line), b, c, s); }, r.output);
            r.ok = code == SHARD_OK;
            r.found = code != SHARD_E_NOT_FOUND;
            return r;
        }

    private:
        struct Command {
            CommandFn fn;
            std::string output;         // kept until the host copied it
        };
        static ShardResult SHARD_CALL Trampoline(void* user, ShardPluginCtx ctx, const ShardStr* args, uint32_t argCount, ShardStr* output)
        {
            auto* command = static_cast<Command*>(user);
            bool ok = false;
            command->output.clear();
            const ShardResult r = detail::Guard(ctx, "a command", [&] {
                std::vector<std::string_view> words;
                for (uint32_t i = 0; i < argCount; ++i) words.push_back(View(args[i]));
                ok = command->fn(words, command->output);
            });
            *output = Str(command->output);
            if (r != SHARD_OK) return r;
            return ok ? SHARD_OK : SHARD_E_INVALID_ARG;
        }
        ShardPluginCtx m_Ctx;
    };
}
