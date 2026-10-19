#pragma once

// A plugin is only loaded by an executable built the same way : the same compiler family, the same build type and the same
// options that change what a struct looks like or what the simulation computes. The host compares the fingerprint the plugin
// was built with to its own, character by character.

#define SHARD_FP_STR2(x) #x
#define SHARD_FP_STR(x) SHARD_FP_STR2(x)

#if defined(_WIN32)
#   define SHARD_FP_OS "windows"
#elif defined(__APPLE__)
#   define SHARD_FP_OS "macos"
#else
#   define SHARD_FP_OS "linux"
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
#   define SHARD_FP_ARCH "arm64"
#else
#   define SHARD_FP_ARCH "x86_64"
#endif

#if defined(__clang__)
#   define SHARD_FP_COMPILER "clang" SHARD_FP_STR(__clang_major__)
#elif defined(__GNUC__)
#   define SHARD_FP_COMPILER "gcc" SHARD_FP_STR(__GNUC__)
#elif defined(_MSC_VER)
#   define SHARD_FP_COMPILER "msvc"
#else
#   define SHARD_FP_COMPILER "unknown"
#endif

#if defined(NDEBUG)
#   define SHARD_FP_BUILD "release"
#else
#   define SHARD_FP_BUILD "debug"
#endif

#if defined(SHARD_DOUBLE_PRECISION_PHYSICS)
#   define SHARD_FP_DOUBLE "double"
#else
#   define SHARD_FP_DOUBLE "single"
#endif

#if defined(SHARD_DETERMINISTIC_PHYSICS)
#   define SHARD_FP_DET "det"
#else
#   define SHARD_FP_DET "nodet"
#endif

#define SHARD_BUILD_FINGERPRINT SHARD_FP_OS "-" SHARD_FP_ARCH "|" SHARD_FP_COMPILER "|" SHARD_FP_BUILD "|" SHARD_FP_DOUBLE "|" SHARD_FP_DET

namespace Shard::Engine::Plugins {
    /// The fingerprint of the code that is being compiled
    inline constexpr const char* kBuildFingerprint = SHARD_BUILD_FINGERPRINT;
}
