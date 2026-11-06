// Shard C++ SDK : what the other headers share. Not an API of its own.
#pragma once

#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <shard/shard_host.h>
#include <shard/cpp/string.hpp>

// Built for a sandboxed plugin (wasm32 without exceptions) : there is nothing to throw. A refusal that would have been thrown stops the plugin
// instead (the message goes to the host's log, then the module traps : the host sees a failed call, like any other error of the plugin).
#if defined(SHARD_SDK_NO_EXCEPTIONS) || (!defined(__cpp_exceptions) && !defined(__EXCEPTIONS))
#   define SHARD_SDK_NOEXC 1
#else
#   define SHARD_SDK_NOEXC 0
#endif

// For what a system does rarely from inside its row loop (an event, a log line) : kept out of line so that the loop stays small and the compiler
// can optimise it. A call that may throw, written in the loop, costs every row (measured : 1.5x on a trivial system, see c-abi-sdk.fr.md section 13)
#if defined(_MSC_VER) && !defined(__clang__)
#   define SHARD_COLD __declspec(noinline)
#else
#   define SHARD_COLD __attribute__((noinline, cold))
#endif

#if SHARD_SDK_NOEXC
#   define SHARD_THROW(expression) ::Shard::detail::Abort(expression)
#else
#   define SHARD_THROW(expression) throw expression
#endif

namespace Shard {

    /// What a registration of the SDK throws when the host refused it. The SDK turns it into an error of the callback it escaped from (the
    /// host then fails the plugin with the message logged).
    class Error : public std::runtime_error {
    public:
        Error(ShardResult result, const std::string& what) : std::runtime_error(what + " (" + ResultName(result) + ")"), m_Result(result) {}
        ShardResult Result() const { return m_Result; }

        static const char* ResultName(ShardResult r)
        {
            switch (r) {
                case SHARD_OK: return "ok";
                case SHARD_E_INVALID_ARG: return "invalid argument";
                case SHARD_E_NOT_FOUND: return "not found";
                case SHARD_E_ALREADY_EXISTS: return "already exists";
                case SHARD_E_WRONG_THREAD: return "wrong thread";
                case SHARD_E_STALE_HANDLE: return "stale handle";
                case SHARD_E_UNSUPPORTED: return "unsupported";
                case SHARD_E_OUT_OF_MEMORY: return "out of memory";
                case SHARD_E_INTERNAL: return "internal error";
                default: return "error";
            }
        }

    private:
        ShardResult m_Result;
    };

    namespace detail {

#if SHARD_SDK_NOEXC
        [[noreturn]] inline void Abort(const Error& e);
#endif

        /// The plugin's own handle, known from its first callback (a plugin is one context for its whole life): what lets a value type like
        /// Shard::Text or Shard::Actor reach the host without being handed the context
        inline ShardPluginCtx& PluginHandle()
        {
            static ShardPluginCtx handle = 0;
            return handle;
        }

        /// The host's table, given to the entry point
        inline const ShardHostApi*& Host()
        {
            static const ShardHostApi* host = nullptr;
            return host;
        }

        /// An interface of the host (null if it does not offer it, or not in that revision)
        template <typename T>
        const T* Interface(const char* id, uint32_t minMinor = 0)
        {
            const void* table = nullptr;
            if (!Host() || Host()->query_interface(Str(id), 1, minMinor, &table) != SHARD_OK) return nullptr;
            return static_cast<const T*>(table);
        }

        template <typename T>
        const T& Require(const char* id)
        {
            const T* table = Interface<T>(id);
            if (!table) SHARD_THROW(Error(SHARD_E_NOT_FOUND, std::string("this host does not offer the interface ") + id));
            return *table;
        }

        inline void Check(ShardResult r, const std::string& what)
        {
            if (r != SHARD_OK) SHARD_THROW(Error(r, what));
        }

        /// The objects the host calls back (system functions, commands, handlers) : they must outlive their registration. The host drops the
        /// registrations when the plugin's context goes (disable, unload), and never calls them afterwards : they are freed at the next
        /// on_load and at unload.
        struct Kept {
            virtual ~Kept() = default;
        };
        template <typename F>
        struct KeptFn : Kept {
            explicit KeptFn(F f) : fn(std::move(f)) {}
            F fn;
        };
        inline std::vector<std::unique_ptr<Kept>>& KeptObjects()
        {
            static std::vector<std::unique_ptr<Kept>> kept;
            return kept;
        }
#if SHARD_SDK_NOEXC
        [[noreturn]] inline void Abort(const Error& e)
        {
            if (Host()) Host()->log(PluginHandle(), SHARD_LOG_ERROR, Str(std::string(e.what())));
            __builtin_trap();
        }
#endif

        template <typename F>
        F* Keep(F f)
        {
            auto holder = std::make_unique<KeptFn<F>>(std::move(f));
            F* raw = &holder->fn;
            KeptObjects().push_back(std::move(holder));
            return raw;
        }

        /// Runs `fn` and drops what it throws : for the callbacks that have no way to report (a job, a socket handler, a draw function)
        template <typename Fn>
        void NoThrow(Fn&& fn) noexcept
        {
#if SHARD_SDK_NOEXC
            fn();
#else
            try { fn(); } catch (const std::exception& e) {
                if (Host()) Host()->log(PluginHandle(), SHARD_LOG_ERROR, Str(std::string("a callback threw : ") + e.what()));
            } catch (...) {
                if (Host()) Host()->log(PluginHandle(), SHARD_LOG_ERROR, Str("a callback threw"));
            }
#endif
        }

        /// Runs `fn` and turns what it throws into a result, with the message in the host's log
        template <typename Fn>
        ShardResult Guard(ShardPluginCtx ctx, const char* what, Fn&& fn) noexcept
        {
#if SHARD_SDK_NOEXC
            (void)ctx; (void)what;
            fn();
            return SHARD_OK;
#else
            try {
                fn();
                return SHARD_OK;
            } catch (const Error& e) {
                if (Host()) Host()->log(ctx, SHARD_LOG_ERROR, Str(std::string(what) + " : " + e.what()));
                return e.Result() != SHARD_OK ? e.Result() : SHARD_E_INTERNAL;
            } catch (const std::exception& e) {
                if (Host()) Host()->log(ctx, SHARD_LOG_ERROR, Str(std::string(what) + " threw : " + e.what()));
                return SHARD_E_INTERNAL;
            } catch (...) {
                if (Host()) Host()->log(ctx, SHARD_LOG_ERROR, Str(std::string(what) + " threw"));
                return SHARD_E_INTERNAL;
            }
#endif
        }
    }
}
