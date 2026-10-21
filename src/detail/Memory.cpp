#include "Memory.h"
#include <atomic>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
namespace giga_bredr { namespace detail {
namespace {
void* systemAllocate(size_t n, void*) { return malloc(n); }
void systemRelease(void* p, void*) { free(p); }
struct Provider { AllocFn allocate; FreeFn release; void* user; };
Provider providers[3] = {{systemAllocate, systemRelease, nullptr},
                         {systemAllocate, systemRelease, nullptr}, {nullptr, nullptr, nullptr}};
struct Header { void* raw; FreeFn free_fn; void* user; size_t size; uint32_t magic; uint8_t kind; };
const uint32_t Magic = 0x47425241;
std::atomic<size_t> current{0}, peak{0}, blocks{0}, category[3];
std::atomic<uint32_t> failures{0};
std::atomic<int> fail_after{-1};
bool failureRequested() {
    int n = fail_after.load();
    while (n >= 0) {
        if (n == 0) return true;
        if (fail_after.compare_exchange_weak(n, n - 1)) break;
    }
    return false;
}
}
bool setMemoryProvider(MemoryKind kind, AllocFn alloc, FreeFn free_fn, void* user) {
    auto i = static_cast<unsigned>(kind);
    if (i >= 3 || blocks.load() || !alloc || !free_fn) return false;
    providers[i] = {alloc, free_fn, user};
    return true;
}
void* allocate(size_t n, MemoryKind kind, size_t alignment) {
    const unsigned k = static_cast<unsigned>(kind);
    if (alignment < alignof(Header)) alignment = alignof(Header);
    if (!n || k >= 3 || (alignment & (alignment - 1)) ||
        alignment > SIZE_MAX - sizeof(Header) || n > SIZE_MAX - sizeof(Header) - alignment ||
        !providers[k].allocate || failureRequested()) { ++failures; return nullptr; }
    Provider p = providers[k];
    void* raw = p.allocate(n + sizeof(Header) + alignment - 1, p.user);
    if (!raw) { ++failures; return nullptr; }
    uintptr_t address = (reinterpret_cast<uintptr_t>(raw) + sizeof(Header) + alignment - 1) & ~(uintptr_t(alignment) - 1);
    Header* h = reinterpret_cast<Header*>(address) - 1;
    *h = {raw, p.release, p.user, n, Magic, uint8_t(k)};
    size_t total = current.fetch_add(n) + n;
    size_t maximum = peak.load();
    while (total > maximum && !peak.compare_exchange_weak(maximum, total)) {}
    ++blocks; category[k].fetch_add(n);
    return reinterpret_cast<void*>(address);
}
void release(void* pointer) {
    if (!pointer) return;
    Header* h = static_cast<Header*>(pointer) - 1;
    if (h->magic != Magic) abort();
    const Header copy = *h; h->magic = 0;
    current.fetch_sub(copy.size); --blocks; category[copy.kind].fetch_sub(copy.size);
    copy.free_fn(copy.raw, copy.user);
}
MemoryStats memoryStats() {
    MemoryStats s; s.current_bytes = current.load(); s.peak_bytes = peak.load();
    s.current_blocks = blocks.load(); s.failures = failures.load();
    for (unsigned i = 0; i < 3; ++i) s.category_bytes[i] = category[i].load();
    return s;
}
void resetMemoryStats() { peak.store(current.load()); failures.store(0); }
void setAllocationFailureCountdown(int n) { fail_after.store(n); }
}}
extern "C" void* gbr_malloc(size_t n) { return giga_bredr::detail::allocate(n); }
extern "C" void* gbr_calloc(size_t count, size_t n) {
    if (!count || !n || count > SIZE_MAX / n) return nullptr;
    void* p = gbr_malloc(count * n); if (p) memset(p, 0, count * n); return p;
}
extern "C" void gbr_free(void* p) { giga_bredr::detail::release(p); }
