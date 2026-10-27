#include "detail/Media.h"
#include "detail/Memory.h"
#include <vector>
#include <cstdio>
#include <cstdlib>
using namespace giga_bredr;
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"RETAINED line%d %s\n",__LINE__,#x);std::abort();}}while(0)
static std::vector<uint8_t> packet(unsigned seq,unsigned ts,const std::vector<uint8_t>&payload){
 std::vector<uint8_t> p(12,0);p[0]=0x80;p[1]=0x60;p[2]=uint8_t(seq>>8);p[3]=uint8_t(seq);p[4]=uint8_t(ts>>24);p[5]=uint8_t(ts>>16);p[6]=uint8_t(ts>>8);p[7]=uint8_t(ts);p.insert(p.end(),payload.begin(),payload.end());return p;
}
static Status feed(detail::MediaAssembler&m,unsigned seq,unsigned ts,std::vector<uint8_t> bytes,detail::MediaView&v){auto p=packet(seq,ts,bytes);return m.feed(p.data(),p.size(),v);}
static void aac(Codec codec){detail::MediaAssembler m;CodecConfig c;c.codec=codec;CHECK(m.configure(c,64)==Status::Ok);detail::MediaView v;
 CHECK(feed(m,1,10,{1,2,3,4},v)==Status::Ok);auto first=m.retainedState();CHECK(first.retained_bytes==4);
 CHECK(feed(m,2,10,{5,6,7,8},v)==Status::Ok);CHECK(m.retainedState().retained_bytes==8&&m.retainedState().assembly_generation==first.assembly_generation);
 CHECK(feed(m,4,10,{9,10,11,12},v)==Status::Ok);auto gap=m.retainedState();CHECK(gap.retained_bytes==4&&gap.assembly_generation!=first.assembly_generation);
 CHECK(feed(m,5,20,{13,14,15,16},v)==Status::Ok);auto changed=m.retainedState();CHECK(changed.retained_bytes==4&&changed.assembly_generation!=gap.assembly_generation);
 m.consumed();CHECK(!m.retainedState().retained_bytes&&m.retainedState().assembly_generation!=changed.assembly_generation);
}
static void fragments(Codec codec){detail::MediaAssembler m;CodecConfig c;c.codec=codec;CHECK(m.configure(c,64)==Status::Ok);detail::MediaView v;
 const uint8_t first=codec==Codec::Sbc?0xc0:0xc2;const uint8_t last=codec==Codec::Sbc?0xa0:0xa1;
 CHECK(feed(m,1,10,{first,1,2,3},v)==Status::NeedMoreData);auto retained=m.retainedState();CHECK(retained.retained_bytes==3);
 CHECK(feed(m,2,20,{first,0x9c,0xfd,2,0},v)==Status::NeedMoreData);auto replaced=m.retainedState();CHECK(replaced.retained_bytes==4&&replaced.assembly_generation!=retained.assembly_generation);
 std::vector<uint8_t> end(14,0);end[0]=last;CHECK(feed(m,3,20,end,v)==Status::Ok);CHECK(v.bytes==17&&m.retainedState().retained_bytes==0);
}
int main(){aac(Codec::Aac);aac(Codec::AacHe);fragments(Codec::Sbc);fragments(Codec::Opus);CHECK(detail::memoryStats().current_blocks==0);std::printf("MEDIA_RETAINED_OBSERVABILITY_PASS checks=%u\n",checks);}
