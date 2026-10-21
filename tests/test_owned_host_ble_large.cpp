#define main owned_host_fixture_main
#include "test_owned_host.cpp"
#undef main
#include "../src/owned/core/bth_l2cap_rx.h"
static Bytes value(unsigned seed){Bytes v(512);for(unsigned i=0;i<v.size();++i)v[i]=uint8_t(seed+37*i);return v;}
static void fragmented(Fixture& f,const Bytes& bytes){Bytes sdu={uint8_t(bytes.size()),uint8_t(bytes.size()>>8),4,0};sdu.insert(sdu.end(),bytes.begin(),bytes.end());for(size_t pos=0;pos<sdu.size();pos+=251){size_t n=sdu.size()-pos;if(n>251)n=251;f.rawAcl(0x26,pos?1:2,Bytes(sdu.begin()+pos,sdu.begin()+pos+n));}}
static Bytes write(const Bytes& v){Bytes p={0x12,9,0};p.insert(p.end(),v.begin(),v.end());return p;}
int main(){CHECK(BleConfig{}.maximum_mtu==517&&BleValueCapacity==512);Fixture f;f.boot();f.leConnect();f.acl(0x26,4,{2,5,2});f.settle();CHECK(f.host.bleStats().att_mtu==517);
 auto a=value(1),b=value(2),c=value(3);fragmented(f,write(a));f.settle();CHECK(f.host.bleStats().writes==1);fragmented(f,write(b));f.settle();CHECK(f.host.bleStats().writes==1);fragmented(f,write(c));f.settle();CHECK(f.host.bleStats().writes==1);
 BleWrite received;CHECK(f.host.bleReadWrite(received)&&received.length==512&&!memcmp(received.data,a.data(),512));f.settle();CHECK(f.host.bleReadWrite(received)&&received.length==512&&!memcmp(received.data,b.data(),512));f.settle();CHECK(f.host.bleReadWrite(received)&&received.length==512&&!memcmp(received.data,c.data(),512));CHECK(f.host.radioStats().transport_errors==0);
 f.acl(0x26,4,{0x12,12,0,1,0});f.settle();CHECK(f.host.bleNotify(a.data(),a.size())==Status::Ok);a[0]^=1;CHECK(f.host.bleNotify(b.data(),b.size())==Status::Busy);
 uint8_t storage[600];bth_l2cap_rx_t assembly;CHECK(bth_l2cap_rx_init(&assembly,0x26,storage,sizeof storage));Bytes notification;unsigned fragments=0;
 for(unsigned i=0;i<12;++i){f.poll();while(!f.tx.empty()){auto packet=f.pop();if(packet[0]!=2){f.acknowledge(packet);continue;}++fragments;CHECK(packet.size()<=256);bth_acl_view_t acl={0x26,uint8_t((packet[2]>>4)&3),0,{packet.data()+5,packet.size()-5}};bth_l2cap_pdu_t pdu;auto status=bth_l2cap_rx_feed(&assembly,&acl,&pdu);CHECK(status==BTH_MORE||status==BTH_OK);if(status==BTH_OK){CHECK(pdu.cid==4);notification.assign(pdu.payload.data,pdu.payload.data+pdu.payload.len);}f.event(0x13,{1,0x26,0,1,0});}}
 auto expected=value(1);CHECK(fragments==3&&notification.size()==515&&notification[0]==0x1b&&!memcmp(notification.data()+3,expected.data(),512));CHECK(f.host.bleStats().notifications_queued==1);Bytes tooLarge(513);CHECK(f.host.bleNotify(tooLarge.data(),tooLarge.size())==Status::InvalidArgument);f.output.clear();fragmented(f,write(tooLarge));f.settle();CHECK(!f.host.bleReadWrite(received)&&f.host.bleStats().writes==3);CHECK(!f.output.empty()&&f.output.back().bytes.size()==5&&f.output.back().bytes[0]==1&&f.output.back().bytes[4]==13);
 Fixture smaller;smaller.boot();smaller.leConnect();smaller.acl(0x26,4,{2,247,0});smaller.settle();CHECK(smaller.host.bleStats().att_mtu==247);smaller.acl(0x26,4,{0x12,12,0,1,0});smaller.settle();CHECK(smaller.host.bleNotify(expected.data(),512)==Status::InvalidArgument);CHECK(smaller.host.bleNotify(expected.data(),244)==Status::Ok);
 printf("Owned BLE517/512 actual actor fragmentation, retained retry, exact copied echo and247 fallback passed %u checks\n",checks);return 0;
}
