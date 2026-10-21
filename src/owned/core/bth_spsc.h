#ifndef SPEAKER_BTH_SPSC_H
#define SPEAKER_BTH_SPSC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t *slots;
    uint32_t capacity, mask;
    uint32_t head, tail;
    bool initialized;
} bth_spsc_t;

// One producer and one consumer only. A slot index transfers ownership; the
// corresponding packet must remain valid until its consumer releases it.
// Initialize before starting either participant. Do not reset a live queue.
bool bth_spsc_init(bth_spsc_t *q, uint32_t *slots, uint32_t capacity);
bool bth_spsc_push(bth_spsc_t *q, uint32_t value);
bool bth_spsc_pop(bth_spsc_t *q, uint32_t *value);

#ifdef __cplusplus
}
#endif
#endif
