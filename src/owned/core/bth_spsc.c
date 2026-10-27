#include "bth_spsc.h"
#include <string.h>

_Static_assert(__atomic_always_lock_free(sizeof(uint32_t), 0),
               "The host queue requires native lock-free 32-bit atomics");

bool bth_spsc_init(bth_spsc_t *q, uint32_t *slots, uint32_t capacity) {
    if (!q) return false;
    memset(q, 0, sizeof *q);
    if (!slots || !capacity || capacity > 0x40000000u || (capacity & (capacity - 1u))) return false;
    q->slots = slots;
    q->capacity = capacity;
    q->mask = capacity - 1u;
    q->initialized = true;
    return true;
}

bool bth_spsc_push(bth_spsc_t *q, uint32_t value) {
    if (!q || !q->initialized) return false;
    const uint32_t head = __atomic_load_n(&q->head, __ATOMIC_RELAXED);
    const uint32_t tail = __atomic_load_n(&q->tail, __ATOMIC_ACQUIRE);
    if (head - tail >= q->capacity) return false;
    q->slots[head & q->mask] = value;
    __atomic_store_n(&q->head, head + 1u, __ATOMIC_RELEASE);
    return true;
}

bool bth_spsc_pop(bth_spsc_t *q, uint32_t *value) {
    if (!q || !q->initialized || !value) return false;
    const uint32_t tail = __atomic_load_n(&q->tail, __ATOMIC_RELAXED);
    const uint32_t head = __atomic_load_n(&q->head, __ATOMIC_ACQUIRE);
    if (head == tail) return false;
    *value = q->slots[tail & q->mask];
    __atomic_store_n(&q->tail, tail + 1u, __ATOMIC_RELEASE);
    return true;
}
