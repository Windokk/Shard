/* Shard C ABI : base types, versions and rules. Compiles as C11 and as C++17.
 *
 * These headers ARE the ABI (see docs/design-notes/c-abi-sdk.fr.md, section 3). The rules :
 *   - plain C : no classes, references, overloads or `enum class`; no `int`, `long`, `bool` or `size_t` inside a struct
 *   - every struct that crosses the boundary starts with `uint32_t structSize`; fields are only ever added at the END
 *   - the host never keeps a pointer it was handed unless the doc of that function says so
 *   - no exception crosses the boundary; failures are a ShardResult
 * Changing a struct without bumping SHARD_ABI_MINOR is a bug : tests/plugins/abi_layout_tests.cpp pins every layout. */
#ifndef SHARD_ABI_H
#define SHARD_ABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_ABI_MAJOR 1
#define SHARD_ABI_MINOR 5

/* Calling convention of every function pointer in the tables. Empty on x86_64 and arm64. */
#define SHARD_CALL

#if defined(_WIN32) || defined(_WIN64)
#   define SHARD_PLUGIN_EXPORT __declspec(dllexport)
#else
#   define SHARD_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

/* ---- Results ---- */

typedef int32_t ShardResult;

#define SHARD_OK                    0
#define SHARD_E_INVALID_ARG         (-1)
#define SHARD_E_NOT_FOUND           (-2)
#define SHARD_E_ALREADY_EXISTS      (-3)
#define SHARD_E_WRONG_THREAD        (-4)
#define SHARD_E_STALE_HANDLE        (-5)
#define SHARD_E_UNSUPPORTED         (-6)
#define SHARD_E_OUT_OF_MEMORY       (-7)
#define SHARD_E_INTERNAL            (-8)

#define SHARD_FALSE 0
#define SHARD_TRUE  1

/* ---- Strings ----
 * UTF-8, not necessarily zero terminated. A string received through a call is valid for that call only : keeping it means copying it. */

typedef struct ShardStr {
    const char* data;
    uint64_t    size;
} ShardStr;

/* ---- Handles ----
 * Never a pointer to a host object. index + generation, so a stale one fails instead of reading freed memory. 0 is never valid. */

typedef uint64_t ShardPluginCtx;    /* designates a plugin to the host */
typedef uint64_t ShardState;        /* the reload state being written or read : valid during on_prepare_reload / on_restore only (shard_state.h) */

/* ---- Log levels ---- */

#define SHARD_LOG_INFO     0
#define SHARD_LOG_WARNING  1
#define SHARD_LOG_ERROR    2

#ifdef __cplusplus
}
#endif

#endif /* SHARD_ABI_H */
