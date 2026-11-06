// C ABI base types, versions, rules. Compiles as C11 and C++17.
// Plain C only, every crossing struct starts with uint32_t structSize and grows only at the END, no exception crosses. Changing a struct without bumping SHARD_ABI_MINOR is a bug (abi_layout_tests.cpp pins layouts).
#ifndef SHARD_ABI_H
#define SHARD_ABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHARD_ABI_MAJOR 1
#define SHARD_ABI_MINOR 0

#define SHARD_CALL

#if defined(_WIN32) || defined(_WIN64)
#   define SHARD_PLUGIN_EXPORT __declspec(dllexport)
#else
#   define SHARD_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

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

// UTF-8, not necessarily zero terminated. Valid for that call only, copy to keep.

typedef struct ShardStr {
    const char* data;
    uint64_t    size;
} ShardStr;

// Never a host pointer: index + generation, so a stale handle fails instead of reading freed memory. 0 is never valid.

typedef uint64_t ShardPluginCtx;
typedef uint64_t ShardState;

#define SHARD_LOG_INFO     0
#define SHARD_LOG_WARNING  1
#define SHARD_LOG_ERROR    2

#ifdef __cplusplus
}
#endif

#endif
