#pragma once
#include "../Types.h"
#include "../GigaBle.h"
namespace giga_bredr { namespace detail {
struct RadioCallbacks {
    Status (*configure)(const CodecConfig&, void*) = nullptr;
    void (*media)(const uint8_t*, size_t, void*) = nullptr; // unmodified AVDTP packet: RTP except ordinary aptX
    void (*stream)(bool playing, void*) = nullptr;
    void (*diagnostic)(const char*, void*) = nullptr;
    void* user = nullptr;
    void (*volume)(uint8_t absolute_volume_0_127, void*) = nullptr;
    void (*metadata)(uint8_t attribute_id, const char* text, size_t bytes, void*) = nullptr;
    // Internal ISR-safe owner notification. It only signals the existing owner;
    // user must remain alive until radioEnd has detached both UART IRQs.
    void (*owner_wake)(void*) = nullptr;
};
Status radioBegin(const char* local_name, uint32_t codec_mask, const RadioCallbacks& callbacks);
Status radioBegin(const char* local_name, uint32_t codec_mask, const RadioCallbacks& callbacks, const BleConfig& ble);
void radioPoll();
void radioEnd();
RadioStats radioStats();
BleStats radioBleStats();
Status radioBleNotify(const uint8_t* data, size_t length);
bool radioBleReadWrite(BleWrite& value);
Status radioBleDisconnect(uint32_t expected_generation,bool advertise_after_disconnect);
Status radioBleResumeAdvertising();
}}
