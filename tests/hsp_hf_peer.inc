struct HfPeer {
 VoiceProfiles voice;Fixture f;bth_rfcomm_t peer{};uint16_t cid=0;uint8_t reply[160]{};size_t read=0,pending_cursor=0;std::string at,pending;bool codec_mode=false,auto_answer=true;
 explicit HfPeer(bool codec):codec_mode(codec){
  f.host.end();f.config.voice_control=&voice;f.config.voice_config.hands_free=true;f.config.voice_config.headset=true;CHECK(f.host.begin(f.config,f.hooks)==Status::Ok);f.boot();f.classicConnect();CHECK(voice.setWidebandBackend(codec));cid=f.channel(3,0x90);CHECK(cid&&voice.cid()==cid);f.output.clear();CHECK(bth_rfcomm_init(&peer,19,true,0,127,2));
  size_t n=0;CHECK(bth_rfcomm_start(&peer,19,f.now,reply,sizeof reply,&n)==BTH_OK);send(reply,n);for(unsigned i=0;i<5;++i)tick();CHECK(bth_rfcomm_open(&peer,19,1,true,f.now,reply,sizeof reply,&n)==BTH_OK);send(reply,n);
  for(unsigned i=0;i<600&&voice.hfpStatus().state!=owned::HfpState::Ready;++i)tick();CHECK(voice.hfpStatus().slcEstablished&&voice.hfpStatus().state==owned::HfpState::Ready);auto_answer=false;
 }
 ~HfPeer(){f.host.end();}
 void send(const uint8_t* p,size_t n){if(n)f.acl(0x25,cid,Bytes(p,p+n));}
 void answer(){
  if(!auto_answer)return;for(;;){const auto cr=at.find('\r');if(cr==std::string::npos)break;const auto row=at.substr(0,cr+1);at.erase(0,cr+1);
   if(row=="AT+BRSF=16\r"||row=="AT+BRSF=144\r")pending+=codec_mode?"\r\n+BRSF: 512\r\nOK\r\n":"\r\n+BRSF: 0\r\nOK\r\n";
   else if(row=="AT+BAC=1,2\r"||row=="AT+CMER=3,0,0,1\r"||row=="AT+VGS=15\r"||row=="AT+VGM=15\r")pending+="\r\nOK\r\n";
   else if(row=="AT+CIND=?\r")pending+="\r\n+CIND: (\"service\",(0,1)),(\"call\",(0,1)),(\"callsetup\",(0-3)),(\"callheld\",(0-2)),(\"signal\",(0-5)),(\"roam\",(0,1)),(\"battchg\",(0-5))\r\nOK\r\n";
   else if(row=="AT+CIND?\r")pending+="\r\n+CIND: 1,0,0,0,5,0,5\r\nOK\r\n";
   else CHECK(false);
  }
 }
 void tick(){
  f.poll();unsigned transport=8;while(transport--&&!f.tx.empty())f.acknowledge(f.pop());
  while(read<f.output.size()){const auto p=f.output[read++];if(p.cid!=0x90)continue;size_t n=0;bth_rfcomm_event_t event{};CHECK(bth_rfcomm_receive(&peer,19,f.now,p.bytes.data(),p.bytes.size(),reply,sizeof reply,&n,&event)>=BTH_OK);
   if(event.type==BTH_RFCOMM_DATA){at.append(reinterpret_cast<const char*>(event.payload.data),event.payload.len);CHECK(bth_rfcomm_release(&peer,19,event.dlci,1)==BTH_OK);answer();}send(reply,n);
  }
  size_t n=0;CHECK(bth_rfcomm_next(&peer,19,f.now,reply,sizeof reply,&n)>=BTH_OK);send(reply,n);
  if(pending_cursor<pending.size()){const size_t remain=pending.size()-pending_cursor,take=remain<23?remain:23;const auto result=bth_rfcomm_send(&peer,19,2,reinterpret_cast<const uint8_t*>(pending.data()+pending_cursor),take,reply,sizeof reply,&n);CHECK(result==BTH_OK||result==BTH_MORE);if(result==BTH_OK){send(reply,n);pending_cursor+=take;}}
 }
};
