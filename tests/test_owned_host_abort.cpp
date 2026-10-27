// Read private state for assertions only. All protocol transitions use public H4.
#define private public
#include "owned/OwnedHost.h"
#undef private
#define main original_fixture_main
#include "test_owned_host.cpp"
#undef main
struct MediaNegotiation{uint16_t local=0,remote=0;uint8_t config=0;};
static Bytes configurePdu(uint8_t label){return {uint8_t(label<<4),3,4,8,1,0,7,6,0,0,0x21,0x15,2,53};}
static bool reply(Fixture&f,uint16_t cid,const Bytes&wanted){auto remote=cid;for(const auto&c:f.host.classic_.channels_)if(c.local==cid)remote=c.remote;for(const auto&p:f.output)if(p.cid==remote&&p.bytes==wanted)return true;return false;}
static void signal(Fixture&f,uint16_t cid,const Bytes&p){f.output.clear();f.acl(0x25,cid,p);f.settle();}
static uint16_t setup(Fixture&f,bool opened=true){f.boot();f.classicConnect();auto cid=f.channel(0x19,0x81);signal(f,cid,configurePdu(1));CHECK(reply(f,cid,{0x12,3}));if(opened){signal(f,cid,{0x20,6,4});CHECK(reply(f,cid,{0x22,6}));}return cid;}
static MediaNegotiation beginMedia(Fixture&f,uint16_t remote){
 f.output.clear();f.acl(0x25,1,{2,0x55,4,0,0x19,0,uint8_t(remote),uint8_t(remote>>8)});f.settle();MediaNegotiation m;m.remote=remote;
 for(const auto&p:f.output)if(p.cid==1){const auto&b=p.bytes;if(b.size()>=12&&b[0]==3)m.local=bth_read_le16(b.data()+4);if(b.size()>=4&&b[0]==4)m.config=b[1];}
 CHECK(m.local>=0x40&&m.config);auto*c=bth_l2cap_sig_find(&f.host.signaling_,m.local);CHECK(c&&c->state==BTH_CHANNEL_CONFIGURING);CHECK(!f.host.classic_.mediaCid());return m;
}
static void finishMedia(Fixture&f,const MediaNegotiation&m){
 f.acl(0x25,1,{5,m.config,6,0,uint8_t(m.local),uint8_t(m.local>>8),0,0,0,0});f.settle();
 f.acl(0x25,1,{4,0x56,8,0,uint8_t(m.local),uint8_t(m.local>>8),0,0,1,2,0xa0,2});f.settle();
}
static uint8_t disconnectId(Fixture&f,uint16_t local){for(const auto&p:f.output)if(p.cid==1&&p.bytes.size()==8&&p.bytes[0]==6&&bth_read_le16(p.bytes.data()+6)==local)return p.bytes[1];return 0;}
static void finishDisconnect(Fixture&f,const MediaNegotiation&m,uint8_t id){CHECK(id);f.acl(0x25,1,{7,id,4,0,uint8_t(m.remote),uint8_t(m.remote>>8),uint8_t(m.local),uint8_t(m.local>>8)});f.settle();}
static void assertHealthy(Fixture&f){CHECK(f.host.radioStats().controller_ready&&f.host.radioStats().connected&&!f.host.radioStats().transport_errors);}
static void no_attempt(){Fixture f;auto s=setup(f);signal(f,s,{0x30,10,4});CHECK(reply(f,s,{0x32,10}));signal(f,s,configurePdu(4));CHECK(reply(f,s,{0x42,3}));CHECK(f.configured==2);assertHealthy(f);}
static void partial_and_late(){Fixture f;auto s=setup(f);auto old=beginMedia(f,0x82);signal(f,s,{0x30,10,4});CHECK(reply(f,s,{0x32,10}));auto cancel=disconnectId(f,old.local);CHECK(cancel);
 CHECK(f.host.classic_.avdtp_.state==BTH_AVDTP_ABORTING);signal(f,s,configurePdu(4));CHECK(reply(f,s,{0x43,3,0,0x13}));CHECK(!f.host.classic_.configured());
 finishMedia(f,old);CHECK(!f.host.classic_.mediaCid()&&f.host.classic_.avdtp_.state==BTH_AVDTP_ABORTING);
 finishDisconnect(f,old,cancel);signal(f,s,configurePdu(5));CHECK(reply(f,s,{0x52,3}));signal(f,s,{0x60,6,4});CHECK(reply(f,s,{0x62,6}));
 auto fresh=beginMedia(f,0x83);CHECK(fresh.local==old.local&&fresh.config!=old.config); // Deliberate local CID reuse.
 f.acl(0x25,1,{5,old.config,6,0,uint8_t(old.local),uint8_t(old.local>>8),0,0,0,0});f.settle();CHECK(!f.host.classic_.mediaCid());
 f.acl(0x25,1,{7,cancel,4,0,uint8_t(old.remote),uint8_t(old.remote>>8),uint8_t(old.local),uint8_t(old.local>>8)});f.settle();CHECK(bth_l2cap_sig_find(&f.host.signaling_,fresh.local));
 finishMedia(f,fresh);CHECK(f.host.classic_.mediaCid()==fresh.local);signal(f,s,{0x70,7,4});CHECK(reply(f,s,{0x72,7})&&f.playing);assertHealthy(f);}
static void established(bool streaming,bool closing){Fixture f;auto s=setup(f);auto media=beginMedia(f,0x82);finishMedia(f,media);CHECK(f.host.classic_.mediaCid()==media.local);
 if(streaming){signal(f,s,{0x30,7,4});CHECK(f.playing);signal(f,s,{0x40,9,4});CHECK(!f.playing&&f.host.classic_.configured());signal(f,s,{0x50,7,4});CHECK(f.playing);}
 signal(f,s,{0x60,uint8_t(closing?8:10),4});CHECK(reply(f,s,{0x62,uint8_t(closing?8:10)})&&!f.playing);auto id=disconnectId(f,media.local);CHECK(id);CHECK(!f.host.classic_.configured());
 if(!closing){CHECK(f.host.classic_.avdtp_.state==BTH_AVDTP_ABORTING);signal(f,s,configurePdu(7));CHECK(reply(f,s,{0x73,3,0,0x13}));}
 finishDisconnect(f,media,id);signal(f,s,configurePdu(8));CHECK(reply(f,s,{0x82,3}));assertHealthy(f);}
static void other_aborts(){Fixture f;auto s=setup(f);signal(f,s,{0x30,10,8});CHECK(reply(f,s,{0x32,10})&&f.host.classic_.configured()&&f.host.classic_.wantsMediaChannel());
 signal(f,s,{0x40,10,0xfc});CHECK(!reply(f,s,{0x42,10})&&f.host.classic_.configured());signal(f,s,{0x50,10,4});CHECK(reply(f,s,{0x52,10}));signal(f,s,{0x60,10,4});CHECK(reply(f,s,{0x62,10}));signal(f,s,configurePdu(7));CHECK(reply(f,s,{0x72,3}));assertHealthy(f);}
static void control_backpressure(){Fixture f;auto s=setup(f);auto m=beginMedia(f,0x82);
 // Stop transport after Abort arrives. Its accepted reply takes one real slot;
 // earlier control replies occupy the remaining bounded capacity.
 f.ready=false;f.output.clear();f.acl(0x25,s,{0x30,2,4});f.poll();f.acl(0x25,s,{0x40,2,4});f.poll();CHECK(f.host.control_.peers[0].owned==2);
 f.acl(0x25,s,{0x50,10,4});f.poll();CHECK(f.host.control_.peers[0].owned==3&&f.host.classic_.avdtp_.state==BTH_AVDTP_ABORTING);f.poll();CHECK(f.host.classic_.avdtp_.state==BTH_AVDTP_ABORTING);
 auto*c=bth_l2cap_sig_find(&f.host.signaling_,m.local);CHECK(c&&c->state==BTH_CHANNEL_CONFIGURING);f.ready=true;f.settle();auto id=disconnectId(f,m.local);CHECK(id);finishDisconnect(f,m,id);signal(f,s,configurePdu(6));CHECK(reply(f,s,{0x62,3}));assertHealthy(f);}
static void signaling_teardown(){Fixture f;auto s=setup(f);auto m=beginMedia(f,0x82);signal(f,s,{0x30,10,4});auto id=disconnectId(f,m.local);CHECK(id);
 f.acl(0x25,1,{6,0x57,4,0,uint8_t(s),uint8_t(s>>8),0x81,0});f.settle();CHECK(!f.host.classic_.signalCid()&&f.host.classic_.avdtp_.state==BTH_AVDTP_ABORTING);finishMedia(f,m);CHECK(!f.host.classic_.signalCid()&&!f.host.classic_.mediaCid());finishDisconnect(f,m,id);
 auto freshSignal=f.channel(0x19,0x91);signal(f,freshSignal,configurePdu(4));CHECK(reply(f,freshSignal,{0x42,3}));assertHealthy(f);}
static void hook_guards(){ClassicProfiles stopped;CHECK(!stopped.abortTransportPending());stopped.abortTransportRetired();CHECK(!stopped.abortTransportPending());Fixture f;auto s=setup(f);CHECK(!f.host.classic_.abortTransportPending());f.host.classic_.abortTransportRetired();CHECK(f.host.classic_.avdtp_.state==BTH_AVDTP_OPENING&&f.host.classic_.configured());
 auto m=beginMedia(f,0x82);finishMedia(f,m);f.acl(0x25,s,{0x30,10,4});f.poll();CHECK(f.host.classic_.abortTransportPending()&&f.host.classic_.mediaCid()==m.local);f.host.classic_.abortTransportRetired();CHECK(f.host.classic_.avdtp_.state==BTH_AVDTP_ABORTING);f.event(5,{0,0x25,0,0x13});f.settle();CHECK(!f.host.classic_.abortTransportPending());auto stoppedCalls=f.stops;f.host.classic_.abortTransportRetired();CHECK(f.stops==stoppedCalls);}
static void deferred_event_gate(){Fixture f;auto s=setup(f);auto m=beginMedia(f,0x82);auto owner=f.host.peers_[0].owner;
 // Fill the real command queue through its checked enqueue API. No credit,
 // pending flag, owner or lifecycle state is assigned by this fixture.
 CHECK(bth_cmdq_push_owned(&f.host.commands_,0x1001,nullptr,0,false,owner)==BTH_OK);f.poll();CHECK(Fixture::op(f.pop())==0x1001&&f.host.flow_.command_credits==0);
 for(auto op:{0x1002,0x1003,0x1005,0x1009,0x100b,0x1001})CHECK(bth_cmdq_push_owned(&f.host.commands_,uint16_t(op),nullptr,0,false,owner)==BTH_OK);uint8_t page=0;CHECK(bth_cmdq_push_owned(&f.host.commands_,0x1004,&page,1,false,owner)==BTH_OK);CHECK(f.host.commands_.used==7);
 f.ready=false;f.output.clear();f.acl(0x25,s,{0x30,2,4});f.poll();f.acl(0x25,s,{0x40,2,4});f.poll();f.acl(0x25,s,{0x50,10,4});f.poll();CHECK(f.host.control_.peers[0].owned==3&&f.host.classic_.avdtp_.state==BTH_AVDTP_ABORTING);
 f.event(8,{0,0x25,0,0});CHECK(f.host.deferred_count_==1);f.ready=true;f.settle();CHECK(f.host.deferred_count_==1&&f.host.peers_[0].owner==owner&&f.host.control_.peers[0].owned==0);const auto*c=bth_l2cap_sig_find(&f.host.signaling_,m.local);CHECK(c&&c->state==BTH_CHANNEL_CONFIGURING&&!disconnectId(f,m.local));
 f.output.clear();f.complete(0x1001,f.response(0x1001));f.settle();CHECK(f.host.deferred_count_==0&&f.host.peers_[0].owner==owner);auto id=disconnectId(f,m.local);CHECK(id);finishDisconnect(f,m,id);CHECK(f.host.classic_.avdtp_.state==BTH_AVDTP_IDLE);assertHealthy(f);}
int main(int argc,char**argv){if(argc==1){no_attempt();partial_and_late();established(false,false);established(true,false);established(true,true);other_aborts();control_backpressure();signaling_teardown();hook_guards();deferred_event_gate();std::printf("PASS Abort retirement lifecycle checks=%u\n",checks);return 0;}CHECK(argc==2);std::string c=argv[1];if(c=="no_attempt")no_attempt();else if(c=="partial_late")partial_and_late();else if(c=="open_abort")established(false,false);else if(c=="stream_abort")established(true,false);else if(c=="close")established(true,true);else if(c=="other_aborts")other_aborts();else if(c=="backpressure")control_backpressure();else if(c=="signal_close")signaling_teardown();else if(c=="guards")hook_guards();else if(c=="deferred")deferred_event_gate();else CHECK(false);std::printf("PASS %s checks=%u\n",c.c_str(),checks);}
