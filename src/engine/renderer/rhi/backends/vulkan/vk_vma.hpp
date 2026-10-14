#pragma once

// The memory allocator, configured the way every file of the backend sees it (the implementation is in vk_vma.cpp, once).
// VMA finds the entry points itself through vkGetInstanceProcAddr / vkGetDeviceProcAddr, which volk loaded.

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_VULKAN_VERSION 1003000

#include <volk.h>
#include <vk_mem_alloc.h>
