#ifndef SPEAKER_BTH_INIT_H
#define SPEAKER_BTH_INIT_H

#include "bth_hci_flow.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BTH_INIT_RESET, BTH_INIT_VERSION, BTH_INIT_ADDRESS, BTH_INIT_BUFFERS,
    BTH_INIT_LE_BUFFERS, BTH_INIT_READY, BTH_INIT_FAILED
} bth_init_stage_t;
typedef struct {
    uint8_t hci_version, address[6];
    uint16_t manufacturer, classic_length, classic_packets, le_length, le_packets;
} bth_controller_info_t;
typedef struct {
    bth_hci_flow_t flow;
    bth_controller_info_t info;
    bth_init_stage_t stage;
    uint16_t pending_opcode;
    uint8_t error;
    bool pending;
} bth_init_t;

typedef enum {BTH_BOOT_CONTROLLER,BTH_BOOT_CRYPTO,BTH_BOOT_SCAN,BTH_BOOT_READY} bth_boot_phase_t;
typedef struct {uint32_t deadline;bth_boot_phase_t phase;bool initialized;} bth_boot_timer_t;
// Profile startup watchdog:5s controller setup,35s optional crypto preflight,
// then a fresh5s scan-enable budget. Same-phase observations never extend it.
void bth_boot_timer_start(bth_boot_timer_t *timer,uint32_t now_ms);
bool bth_boot_timer_enter(bth_boot_timer_t *timer,bth_boot_phase_t phase,uint32_t now_ms);
bool bth_boot_timer_expired(const bth_boot_timer_t *timer,uint32_t now_ms);

// READY means controller information/credits discovered, not profile readiness.
void bth_init_start(bth_init_t *init);
// Call only when the transport can actually send. A successful encoding commits
// a command credit and pending opcode; the caller must send that command once.
bth_status_t bth_init_next_command(bth_init_t *init, uint8_t *out, size_t capacity, size_t *written);
bth_status_t bth_init_on_event(bth_init_t *init, const bth_h4_frame_t *event);

#ifdef __cplusplus
}
#endif
#endif
