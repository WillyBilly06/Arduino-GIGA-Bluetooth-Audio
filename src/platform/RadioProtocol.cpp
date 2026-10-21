// Public snapshot: historical backend bridge omitted (2026-10-21).
#include "RadioProtocol.h"
#include "../RadioBuildConfig.h"
namespace giga_bredr { namespace detail {
bool radioEndpointAccepts(const CodecConfig& c,Codec expected,uint8_t type,const uint8_t* capabilities,size_t size) {
 if(c.codec!=expected || !capabilities || c.codec_info_size!=size) return false;
 uint8_t expected_type=expected==Codec::Sbc?0:(expected==Codec::Aac || expected==Codec::AacHe)?2:255;
 if(type!=expected_type) return false;
 return type!=255 || (size>=6 && !memcmp(c.codec_info,capabilities,6));
}
Status prototypeBdAddrFromUid(const uint8_t* uid,size_t length,uint8_t address[6]) {
 if(!uid || !address || length!=12) return Status::InvalidArgument;
 bool zero=true,ones=true;uint64_t hash=UINT64_C(14695981039346656037);
 for(size_t i=0;i<length;i++) { zero=zero && uid[i]==0;ones=ones && uid[i]==255;hash^=uid[i];hash*=UINT64_C(1099511628211); }
 if(zero || ones) return Status::InvalidArgument;
 for(unsigned i=0;i<6;i++) address[i]=uint8_t(hash>>(8*(5-i)));
 // Prototype locally administered/unicast convention. This is not an assigned
 // production OUI and FNV is not a cryptographic identity or collision proof.
 address[0]=(address[0]&0xfc)|0x02;
 return Status::Ok;
}
void bcmSetBdAddrCommand(const uint8_t address[6],uint8_t command[9]) {
 command[0]=1;command[1]=0xfc;command[2]=6;
 for(unsigned i=0;i<6;i++) command[3+i]=address[5-i];
}
static bool one(unsigned x) { return x && !(x&(x-1)); }
static uint32_t le32(const uint8_t* p) { return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24); }
Status parseRadioCodec(uint8_t t,const uint8_t* p,size_t n,CodecConfig& out) {
 if(!p || n>sizeof(out.codec_info)) return Status::Unsupported;
 CodecConfig c;
 if(t==0) {
  if(n!=4 || !one(p[0]&0xf0) || !(p[0]&0x30) || !one(p[0]&15) || !one(p[1]&0xf0) || !one(p[1]&12) || !one(p[1]&3) || p[2]<2 || p[3]>250 || p[2]>p[3]) return Status::Unsupported;
  c.codec=Codec::Sbc; c.sample_rate=(p[0]&0x20)?44100:48000; c.channels=(p[0]&8)?1:2;
 } else if(t==2) {
  if(n!=6 || (p[0]!=0x40 && p[0]!=0x80 && p[0]!=0x08) || !one((unsigned(p[1])<<4)|(p[2]>>4)) || !((p[1]==1 && !(p[2]&0xf0)) || (p[1]==0 && (p[2]&0xf0)==0x80)) || !one(p[2]&12) || (p[2]&3)) return Status::Unsupported;
  c.codec=p[0]==0x08?Codec::AacHe:Codec::Aac; c.sample_rate=p[1]?44100:48000; c.channels=(p[2]&4)?2:1;
  c.bitrate=(uint32_t(p[3]&127)<<16)|(unsigned(p[4])<<8)|p[5];
 } else if(t==255 && n>=6) {
  uint32_t v=le32(p); unsigned id=p[4]|(unsigned(p[5])<<8);
  if((v==0x4f && id==1) || (v==0xd7 && id==0x24)) {
   bool hd=v==0xd7;
   if(n!=(hd?11u:7u) || !one(p[6]&0xf0) || !(p[6]&0x30) || (p[6]&15)!=2) return Status::Unsupported;
   if(hd && (p[7]|p[8]|p[9]|p[10])) return Status::Unsupported;
   c.codec=hd?Codec::AptxHd:Codec::Aptx; c.sample_rate=(p[6]&0x20)?44100:48000; c.bits_per_sample=hd?24:16;
  } else if(v==0x12d && id==0xaa) {
   if(n!=8 || !one(p[6]) || !(p[6]&0x3c) || p[7]!=1) return Status::Unsupported;
   c.codec=Codec::Ldac; c.bits_per_sample=24;
   c.sample_rate=p[6]==0x20?44100:p[6]==0x10?48000:p[6]==8?88200:96000;
  } else if(v==0x53a && id==0x4c35) {
   if(n!=11 || !one(p[6]) || !(p[6]&0x35) || !one(p[7]&7) || !(p[7]&6) || (p[7]&8) || p[8]!=0x11 || p[9] || p[10]) return Status::Unsupported;
   c.codec=Codec::LhdcV5; c.sample_rate=p[6]==0x20?44100:p[6]==0x10?48000:p[6]==4?96000:192000; c.bits_per_sample=(p[7]&2)?24:16; c.frame_duration_us=5000;
  } else if(v==0xe0 && id==1) {
   if(n!=7 || p[6]!=0x92) return Status::Unsupported;
   c.codec=Codec::Opus; c.sample_rate=48000; c.frame_duration_us=20000;
  } else if(v==0x5f1 && id==0x1005) {
   if(n!=24 || p[6]!=2 || p[7]!=1 || le32(p+8)!=3 || !one(p[12]) || !(p[12]&0x1c) || p[15] || p[16] || le32(p+17) || p[21] || p[22] || p[23]) return Status::Unsupported;
   c.codec=Codec::Opus; c.sample_rate=48000; c.frame_duration_us=p[12]==4?10000:p[12]==8?20000:40000;
   c.bitrate=unsigned(p[13]|(unsigned(p[14])<<8))*1024;
  } else return Status::Unsupported;
 } else return Status::Unsupported;
 c.codec_info_size=uint8_t(n); memcpy(c.codec_info,p,n); out=c; return Status::Ok;
}
}}
