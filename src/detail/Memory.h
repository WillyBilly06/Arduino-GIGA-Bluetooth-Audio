#pragma once
#include "../Types.h"
namespace giga_bredr { namespace detail {
using AllocFn = void* (*)(size_t, void*);
using FreeFn = void (*)(void*, void*);
// Set providers before starting workers. Changing a provider with live blocks is rejected.
bool setMemoryProvider(MemoryKind kind, AllocFn allocate, FreeFn release, void* user);
void* allocate(size_t bytes, MemoryKind kind = MemoryKind::Fast, size_t alignment = 8);
void release(void* pointer);
MemoryStats memoryStats();
void resetMemoryStats();
void setAllocationFailureCountdown(int successful_allocations_before_failure);
}}
extern "C" {
void* gbr_malloc(size_t bytes);
void* gbr_calloc(size_t count, size_t bytes);
void gbr_free(void* pointer);
}
