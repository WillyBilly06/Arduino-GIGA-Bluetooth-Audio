#ifndef GIGA_BREDR_BTH_SDP_CLIENT_H
#define GIGA_BREDR_BTH_SDP_CLIENT_H
#include "bth_packet.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
// Minimal SDP client: one ServiceSearchAttributeRequest for a 16-bit service
// class UUID, continuation reassembly into caller storage, and a parser for
// the attributes an RFCOMM/GOEP client needs. No allocation or transport.
enum { BTH_SDP_CLIENT_MAX_ATTRIBUTE_BYTES=576, BTH_SDP_CLIENT_CONTINUATION=16 };
typedef struct {
 uint8_t* buffer; size_t capacity, used;
 uint16_t uuid, transaction;
 uint8_t continuation[1+BTH_SDP_CLIENT_CONTINUATION];
 uint8_t rounds; // Requests issued; bounds a peer that never ends continuation.
 bool awaiting, complete, initialized;
} bth_sdp_client_t;
// Results for the first matching record that carries an RFCOMM channel.
typedef struct {
 uint8_t rfcomm_channel;   // 1..30
 uint16_t profile_version; // BluetoothProfileDescriptorList version, 0 if absent.
 uint16_t goep_l2cap_psm;  // Attribute 0x0200 (GOEP 2.0); reported, not used.
 uint32_t supported_features; uint8_t supported_repositories; // 0x0317 / 0x0314 when present.
 bool found;
} bth_sdp_service_t;
bool bth_sdp_client_init(bth_sdp_client_t*,uint16_t service_uuid,uint8_t* buffer,size_t capacity,uint16_t transaction);
// Builds the next request (first or continuation). BTH_ERR_STATE while a
// response is awaited or after completion.
bth_status_t bth_sdp_client_request(bth_sdp_client_t*,uint8_t* out,size_t capacity,size_t* written);
// Consumes one SDP response PDU. BTH_OK: attribute lists complete. BTH_MORE:
// issue another (continuation) request. Negative: protocol error, an SDP Error
// Response (BTH_UNSUPPORTED) or storage exhausted; the client stays failed.
bth_status_t bth_sdp_client_response(bth_sdp_client_t*,const uint8_t*,size_t);
// Parses a complete AttributeLists data element (sequence of per-record
// attribute lists). BTH_OK with found=false when no record has an RFCOMM channel.
bth_status_t bth_sdp_client_parse(const uint8_t*,size_t,bth_sdp_service_t*);
#ifdef __cplusplus
}
#endif
#endif
