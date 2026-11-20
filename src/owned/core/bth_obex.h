#ifndef GIGA_BREDR_BTH_OBEX_H
#define GIGA_BREDR_BTH_OBEX_H
#include "bth_packet.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
// OBEX (IrDA OBEX 1.5 / GOEP 1.1 over RFCOMM) packet codec. Independently
// written; no allocation, clock, callback or transport. Single Response Mode
// (GOEP 2.0 / OBEX over L2CAP) is not implemented. Every view borrows the
// caller's packet bytes and is valid only while those bytes are unchanged.
enum {
 BTH_OBEX_VERSION=0x10, BTH_OBEX_FINAL=0x80,
 BTH_OBEX_MIN_PACKET=255, // Smallest maximum packet length a peer may negotiate.
 // Request opcodes (final bit included where the opcode is always final).
 BTH_OBEX_CONNECT=0x80, BTH_OBEX_DISCONNECT=0x81, BTH_OBEX_PUT=0x02, BTH_OBEX_GET=0x03,
 BTH_OBEX_SETPATH=0x85, BTH_OBEX_ABORT=0xff,
 // Response codes (always sent with the final bit).
 BTH_OBEX_CONTINUE=0x90, BTH_OBEX_SUCCESS=0xa0, BTH_OBEX_BAD_REQUEST=0xc0, BTH_OBEX_UNAUTHORIZED=0xc1,
 BTH_OBEX_FORBIDDEN=0xc3, BTH_OBEX_NOT_FOUND=0xc4, BTH_OBEX_NOT_ACCEPTABLE=0xc6,
 BTH_OBEX_PRECONDITION_FAILED=0xcc, BTH_OBEX_ENTITY_TOO_LARGE=0xcd, BTH_OBEX_UNSUPPORTED_MEDIA=0xcf,
 BTH_OBEX_INTERNAL_ERROR=0xd0, BTH_OBEX_NOT_IMPLEMENTED=0xd1, BTH_OBEX_SERVICE_UNAVAILABLE=0xd3,
 // Header identifiers. The top two bits select the encoding: 00 UTF-16BE text,
 // 01 byte sequence, 10 one byte, 11 four-byte big-endian quantity.
 BTH_OBEX_H_COUNT=0xc0, BTH_OBEX_H_NAME=0x01, BTH_OBEX_H_TYPE=0x42, BTH_OBEX_H_LENGTH=0xc3,
 BTH_OBEX_H_TIME=0x44, BTH_OBEX_H_DESCRIPTION=0x05, BTH_OBEX_H_TARGET=0x46, BTH_OBEX_H_HTTP=0x47,
 BTH_OBEX_H_BODY=0x48, BTH_OBEX_H_END_OF_BODY=0x49, BTH_OBEX_H_WHO=0x4a, BTH_OBEX_H_CONNECTION_ID=0xcb,
 BTH_OBEX_H_APP_PARAMS=0x4c, BTH_OBEX_H_AUTH_CHALLENGE=0x4d, BTH_OBEX_H_AUTH_RESPONSE=0x4e,
 BTH_OBEX_H_OBJECT_CLASS=0x4f
};
typedef struct { uint8_t id; bth_span_t data; uint32_t value; } bth_obex_header_t;
// One parsed request. connect/setpath fields are zero for other opcodes.
typedef struct {
 uint8_t opcode; // Without the final bit for PUT/GET; CONNECT/DISCONNECT/SETPATH/ABORT as sent.
 bool final;
 uint8_t version, flags, constants; uint16_t max_packet;
 bth_span_t headers;
} bth_obex_request_t;
typedef struct { uint8_t code; bool final; uint8_t version, flags; uint16_t max_packet; bth_span_t headers; } bth_obex_response_t;
// Total packet length from the 3-byte prefix; 0 when fewer than 3 bytes.
size_t bth_obex_packet_length(const uint8_t*,size_t);
// Checked whole header list: every header is complete, text headers have an
// even payload, and nothing trails the last header.
bth_status_t bth_obex_headers_validate(bth_span_t);
// BTH_OK yields one header and advances rest; BTH_MORE when rest is empty.
bth_status_t bth_obex_header_next(bth_span_t* rest,bth_obex_header_t*);
// First header with this id (BTH_OK) or BTH_MORE. The list must be validated.
bth_status_t bth_obex_header_find(bth_span_t headers,uint8_t id,bth_obex_header_t*);
// Complete-packet parsers: the length field must equal len and the header list
// must validate. connect selects the 7-byte CONNECT response layout (success
// responses must carry it; failures may omit it).
bth_status_t bth_obex_parse_request(const uint8_t*,size_t,bth_obex_request_t*);
bth_status_t bth_obex_parse_response(const uint8_t*,size_t,bool connect,bth_obex_response_t*);
// Bounded writer. Any overflow is sticky and reported by finish (no partial packet).
typedef struct { uint8_t* data; size_t capacity, used; bool overflow; } bth_obex_writer_t;
void bth_obex_writer_init(bth_obex_writer_t*,uint8_t* buffer,size_t capacity,uint8_t opcode);
void bth_obex_put_u8(bth_obex_writer_t*,uint8_t);
void bth_obex_put_u16(bth_obex_writer_t*,uint16_t);
void bth_obex_add_u8(bth_obex_writer_t*,uint8_t id,uint8_t);
void bth_obex_add_u32(bth_obex_writer_t*,uint8_t id,uint32_t);
void bth_obex_add_bytes(bth_obex_writer_t*,uint8_t id,const uint8_t*,size_t);
// UTF-8 text -> UTF-16BE text header with NUL terminator. An empty string
// emits the 3-byte empty header. Invalid UTF-8 is an overflow (rejected).
void bth_obex_add_text(bth_obex_writer_t*,uint8_t id,const char* utf8,size_t length);
bth_status_t bth_obex_finish(bth_obex_writer_t*,size_t* length);
// UTF-16BE (stops at NUL) -> NUL-terminated UTF-8, truncated only on a code
// point boundary. Unpaired surrogates become '?'. Returns bytes before NUL.
size_t bth_obex_text_to_utf8(bth_span_t,char* out,size_t capacity);
// Application-parameter TLV iteration (tag, length, value): BTH_OK, BTH_MORE at
// end, BTH_ERR_LENGTH if a triplet is truncated.
bth_status_t bth_obex_tlv_next(bth_span_t* rest,uint8_t* tag,bth_span_t* value);
// Reassembles one packet from transport fragments. BTH_OK: complete packet of
// *length bytes at data. BTH_MORE: more bytes needed. BTH_ERR_LENGTH: declared
// length <3, above capacity, or bytes beyond the declared packet (a peer may
// not pipeline another request before this response). State is unchanged on error.
typedef struct { uint8_t* data; size_t capacity, used; } bth_obex_assembler_t;
void bth_obex_assembler_init(bth_obex_assembler_t*,uint8_t* buffer,size_t capacity);
bth_status_t bth_obex_assemble(bth_obex_assembler_t*,const uint8_t*,size_t,size_t* length);
bool bth_obex_assembler_complete(const bth_obex_assembler_t*);
void bth_obex_assembler_reset(bth_obex_assembler_t*);
#ifdef __cplusplus
}
#endif
#endif
