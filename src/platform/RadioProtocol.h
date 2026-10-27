#pragma once
#include "../Types.h"
#include <string.h>
namespace giga_bredr { namespace detail {
// Separate A2DP endpoints: HE v1 never widens the existing AAC-LC endpoint.
constexpr uint8_t RadioAacLcCapabilities[]={0xc0,0x01,0x8c,0x80,0,0};
constexpr uint8_t RadioAacHeCapabilities[]={0x08,0x01,0x8c,0x80,0,0};
// Only this parser sees raw UART bytes. No allocation and no partial dispatch.
class H4Parser {
public:
 static constexpr unsigned Capacity=2048;
 bool feed(uint8_t b) {
  if (discard_) { --discard_; return false; }
  if (!used_) {
   type_=b; header_=(b==4?2:(b==2?4:(b==3?3:0)));
   if (!header_) { ++errors_; return false; }
   used_=1; received_=0; total_=header_; return false;
  }
  buffer_[received_++]=b;
  if (received_==header_) {
   unsigned payload=(type_==4?buffer_[1]:(type_==2?(unsigned(buffer_[3])<<8)|buffer_[2]:buffer_[2]));
   if (payload>Capacity-header_) { ++errors_; discard_=payload; used_=0; return false; }
   total_+=payload;
  }
  if(received_==total_) { used_=0; return true; }
  return false;
 }
 void reset() { used_=received_=total_=discard_=0; }
 bool incomplete() const { return used_ || discard_; }
 uint8_t type() const { return type_; }
 uint16_t size() const { return received_; }
 uint8_t* data() { return buffer_; }
 uint32_t errors() const { return errors_; }
private:
 uint8_t buffer_[Capacity]={}, type_=0, header_=0;
 unsigned used_=0,received_=0,total_=0,discard_=0;
 uint32_t errors_=0;
};
class HcdReader {
public:
 HcdReader(const uint8_t* p,size_t n): data_(p),remaining_(n) {}
 int next(uint8_t* out) {
  if (!remaining_) return 0;
  if (remaining_<3 || size_t(data_[2])+3>remaining_) return -1;
  unsigned n=unsigned(data_[2])+3; memcpy(out,data_,n); data_+=n; remaining_-=n; return int(n);
 }
private: const uint8_t* data_; size_t remaining_;
};
bool radioEndpointAccepts(const CodecConfig& configuration,Codec endpoint_codec,uint8_t media_codec_type,const uint8_t* capabilities,size_t capabilities_size);
Status prototypeBdAddrFromUid(const uint8_t* uid,size_t length,uint8_t address[6]);
void bcmSetBdAddrCommand(const uint8_t address[6],uint8_t command[9]);
Status parseRadioConfigurationEvent(const uint8_t* event,uint16_t size,CodecConfig& config);
Status parseRadioCodec(uint8_t type,const uint8_t* info,size_t length,CodecConfig& config);
}}
