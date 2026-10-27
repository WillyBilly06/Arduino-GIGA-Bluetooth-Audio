// Actual actor scheduling probe. The burst intentionally admits more than the
// advertised controller window; it is not a controller-conformance simulation.
#define private public
#include "../src/owned/OwnedHost.h"
#undef private
#define main owned_host_fixture_main
#include "test_owned_host.cpp"
#undef main
static void run(bool service,bool ready,unsigned bytes=244){Fixture f;f.boot();f.leConnect();const unsigned mtu=bytes==512?517:247;f.acl(0x26,4,{2,uint8_t(mtu),uint8_t(mtu>>8)});f.settle();f.ready=ready;
 Bytes sdu={uint8_t(bytes+3),uint8_t((bytes+3)>>8),4,0,0x12,9,0};for(unsigned i=0;i<bytes;++i)sdu.push_back(uint8_t(i));unsigned fragments=0;
 for(size_t pos=0;pos<sdu.size();pos+=27){size_t n=sdu.size()-pos;if(n>27)n=27;f.rawAcl(0x26,pos?1:2,Bytes(sdu.begin()+pos,sdu.begin()+pos+n));++fragments;if(service){f.poll();while(!f.tx.empty())f.acknowledge(f.pop());}if(f.host.radioStats().transport_errors)break;}
 if(!service||!ready){CHECK(f.host.radioStats().transport_errors==1&&fragments==5);bool exact=false;for(const auto& m:f.messages)if(m.find(service?"used=0 busy=0":"used=4 busy=4")!=std::string::npos)exact=true;CHECK(exact);}
 else {f.settle();BleWrite v;CHECK(f.host.radioStats().transport_errors==0&&f.host.bleReadWrite(v)&&v.length==bytes&&fragments==(bytes+33)/27);for(unsigned i=0;i<bytes;++i)CHECK(v.data[i]==uint8_t(i));}
 printf("SERVICE probe per_frame=%u tx_ready=%u bytes=%u fragments=%u errors=%lu\n",service,ready,bytes,fragments,(unsigned long)f.host.radioStats().transport_errors);
}
int main(){run(false,true);run(true,true);run(true,false);run(false,true,512);run(true,true,512);run(true,false,512);puts("Actual27-byte ATT fragmentation: deferred-service failure, per-frame progress and stalled-TX limit checks passed");return 0;}
