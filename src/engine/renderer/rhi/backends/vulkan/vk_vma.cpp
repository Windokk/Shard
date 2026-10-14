// The implementation of VMA : one translation unit, warnings of the library left alone

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wparentheses"
#pragma GCC diagnostic ignored "-Wnullability-completeness"
#endif

#define VMA_IMPLEMENTATION
#include "vk_vma.hpp"
