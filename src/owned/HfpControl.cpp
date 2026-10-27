#include "HfpControl.h"
#include <cstring>
namespace giga_bredr { namespace owned {
namespace {
uint8_t kind(const char* p,size_t n){const char* names[]={"service","call","callsetup","callheld","signal","roam","battchg"};for(uint8_t i=0;i<7;++i)if(std::strlen(names[i])==n&&!std::memcmp(p,names[i],n))return static_cast<uint8_t>(i+1);return 0;}
bool allows(const uint8_t* mask,uint32_t value){return value<256&&(mask[value>>3]&(1u<<(value&7)))!=0;}
uint8_t expectedMax(uint8_t k){const uint8_t max[]={0,1,1,3,2,5,1,5};return max[k];}
bool scalar(const char* p,uint32_t& n){return hfp_at::number(p,n)&&hfp_at::end(p);}
size_t literal(char* p,const char* s){size_t n=std::strlen(s);std::memcpy(p,s,n);return n;}
}
bool HfpControl::begin(uint32_t g,bool wb,uint8_t speaker,uint8_t mic){
 if(!g||speaker>15||mic>15)return false;end();status_.generation=g;status_.speakerGain=speaker;status_.microphoneGain=mic;status_.state=HfpState::Features;wideband_=wb;return true;
}
void HfpControl::end(){status_={};lines_.reset();std::memset(indicatorKinds_,0,sizeof indicatorKinds_);std::memset(indicatorAllowed_,0,sizeof indicatorAllowed_);codecRequest_=codecInFlight_=0;deadline_=0;transaction_=Transaction::None;wideband_=seen_=codecListPending_=false;user_[0]=0;}
bth_status_t HfpControl::next(uint32_t g,uint32_t now,char* out,size_t cap,size_t& written){
 written=0;if(!live(g))return BTH_IGNORED;expire(g,now);if(!live(g))return BTH_IGNORED;if(!out)return BTH_ERR_PARAM;if(transaction_!=Transaction::None)return BTH_MORE;
 char row[48];size_t n=0;Transaction transaction=Transaction::Slc;
 switch(status_.state){
 case HfpState::Features:n=literal(row,wideband_?"AT+BRSF=144\r":"AT+BRSF=16\r");break;
 case HfpState::Codecs:n=literal(row,"AT+BAC=1,2\r");break;
 case HfpState::IndicatorNames:n=literal(row,"AT+CIND=?\r");break;
 case HfpState::IndicatorValues:n=literal(row,"AT+CIND?\r");break;
 case HfpState::EnableEvents:n=literal(row,"AT+CMER=3,0,0,1\r");break;
 case HfpState::SpeakerGain:n=literal(row,"AT+VGS=");n+=hfp_at::decimal(status_.speakerGain,row+n);row[n++]='\r';break;
 case HfpState::MicrophoneGain:n=literal(row,"AT+VGM=");n+=hfp_at::decimal(status_.microphoneGain,row+n);row[n++]='\r';break;
 case HfpState::Ready:
  if(codecRequest_){transaction=Transaction::Codec;n=literal(row,"AT+BCS=");n+=hfp_at::decimal(codecRequest_,row+n);row[n++]='\r';}
  else if(codecListPending_){transaction=Transaction::CodecList;n=literal(row,"AT+BAC=1,2\r");}
  else if(user_[0]){transaction=Transaction::User;n=literal(row,user_);}else return BTH_OK;break;
 default:return BTH_ERR_STATE;
 }
 if(cap<n)return BTH_ERR_CAPACITY;std::memcpy(out,row,n);written=n;transaction_=transaction;seen_=false;deadline_=now+5000;
 if(transaction==Transaction::User)user_[0]=0;
 if(transaction==Transaction::Codec){codecInFlight_=codecRequest_;codecRequest_=0;}
 if(transaction==Transaction::CodecList)codecListPending_=false;
 return BTH_OK;
}
void HfpControl::finish(bool ok){
 Transaction t=transaction_;transaction_=Transaction::None;deadline_=0;
 if(t==Transaction::None)return;
 if(t==Transaction::Slc){
  const bool needsResult=status_.state==HfpState::Features||status_.state==HfpState::IndicatorNames||status_.state==HfpState::IndicatorValues;
  if(!ok||(needsResult&&!seen_)){status_.state=HfpState::Failed;status_.slcEstablished=false;return;}
  switch(status_.state){
  case HfpState::Features:status_.state=wideband_&&(status_.agFeatures&(1u<<9))?HfpState::Codecs:HfpState::IndicatorNames;break;
  case HfpState::Codecs:status_.state=HfpState::IndicatorNames;break;
  case HfpState::IndicatorNames:status_.state=HfpState::IndicatorValues;break;
  case HfpState::IndicatorValues:status_.state=HfpState::EnableEvents;break;
  case HfpState::EnableEvents:status_.slcEstablished=true;status_.state=HfpState::SpeakerGain;break;
  case HfpState::SpeakerGain:status_.state=HfpState::MicrophoneGain;break;
  case HfpState::MicrophoneGain:status_.state=HfpState::Ready;break;
  default:status_.state=HfpState::Failed;status_.slcEstablished=false;break;
  }return;
 }
 status_.lastCommandOk=ok;
 if(t==Transaction::User&&status_.completedCommands!=UINT32_MAX)++status_.completedCommands;
 if(t==Transaction::Codec){if(ok)status_.codec=codecInFlight_==2?VoiceCodec::Msbc:VoiceCodec::Cvsd;codecInFlight_=0;}
}
bool HfpControl::names(const char* p){
 uint8_t kinds[16]{},allowed[16][32]{};uint8_t count=0,mandatory=0;
 do{
  if(count==16||!hfp_at::token(p,'(')||!hfp_at::token(p,'"'))return false;
  const char* name=p;size_t len=0;while(*p&&*p!='"'){if(static_cast<unsigned char>(*p)<32||++len>31)return false;++p;}
  if(!len||!hfp_at::token(p,'"')||!hfp_at::token(p,',')||!hfp_at::token(p,'('))return false;
  do{
   uint32_t first,last;if(!hfp_at::number(p,first)||first>255)return false;last=first;hfp_at::spaces(p);
   if(*p=='-'){++p;if(!hfp_at::number(p,last)||last<first||last>255)return false;}
   for(uint32_t v=first;v<=last;++v)allowed[count][v>>3]|=static_cast<uint8_t>(1u<<(v&7));
   hfp_at::spaces(p);if(*p!=',')break;++p;
  }while(true);
  if(!hfp_at::token(p,')')||!hfp_at::token(p,')'))return false;
  uint8_t k=kind(name,len);if(k){if(mandatory&(1u<<(k-1)))return false;for(uint32_t v=0;v<256;++v)if(allows(allowed[count],v)!=(v<=expectedMax(k)))return false;mandatory|=static_cast<uint8_t>(1u<<(k-1));}
  kinds[count]=k;++count;hfp_at::spaces(p);if(!*p)break;if(*p++!=',')return false;
 }while(true);
 if(mandatory!=0x7f)return false;status_.indicatorCount=count;std::memcpy(indicatorKinds_,kinds,sizeof kinds);std::memcpy(indicatorAllowed_,allowed,sizeof allowed);return true;
}
bool HfpControl::values(const char* p){uint8_t values[16]{};if(!status_.indicatorCount)return false;for(uint8_t i=0;i<status_.indicatorCount;++i){uint32_t v;if(!hfp_at::number(p,v)||!allows(indicatorAllowed_[i],v))return false;values[i]=static_cast<uint8_t>(v);if(i+1<status_.indicatorCount&&!hfp_at::token(p,','))return false;}if(!hfp_at::end(p))return false;std::memcpy(status_.indicators,values,sizeof values);return true;}
bth_status_t HfpControl::line(const char* row){
 using namespace hfp_at;const char* p;uint32_t n;
 if(!std::strcmp(row,"OK")){finish(true);return BTH_OK;}
 if(!std::strcmp(row,"ERROR")||prefix(row,"+CME ERROR:",p)){finish(false);return BTH_OK;}
 if(prefix(row,"+BRSF:",p)){if(status_.state!=HfpState::Features||transaction_!=Transaction::Slc)return BTH_IGNORED;if(seen_||!scalar(p,n))return BTH_ERR_PARAM;status_.agFeatures=n;status_.inBandRing=(n&(1u<<3))!=0;seen_=true;return BTH_OK;}
 if(prefix(row,"+CIND:",p)){if(transaction_!=Transaction::Slc)return BTH_IGNORED;if(seen_)return BTH_ERR_STATE;bool valid=false;if(status_.state==HfpState::IndicatorNames)valid=names(p);else if(status_.state==HfpState::IndicatorValues)valid=values(p);else return BTH_IGNORED;if(!valid)return BTH_ERR_PARAM;seen_=true;return BTH_OK;}
 if(prefix(row,"+CIEV:",p)){uint32_t value;if(!number(p,n)||!token(p,',')||!number(p,value)||!hfp_at::end(p)||!n||n>status_.indicatorCount||!allows(indicatorAllowed_[n-1],value))return BTH_ERR_PARAM;status_.indicators[n-1]=static_cast<uint8_t>(value);return BTH_OK;}
 if(!std::strcmp(row,"RING")){if(status_.rings!=UINT32_MAX)++status_.rings;return BTH_OK;}
 bool speaker=prefix(row,"+VGS:",p);if(speaker||prefix(row,"+VGM:",p)){if(!scalar(p,n)||n>15)return BTH_ERR_PARAM;if(speaker)status_.speakerGain=static_cast<uint8_t>(n);else status_.microphoneGain=static_cast<uint8_t>(n);return BTH_OK;}
 if(prefix(row,"+BSIR:",p)){if(!scalar(p,n)||n>1)return BTH_ERR_PARAM;status_.inBandRing=n!=0;return BTH_OK;}
 if(prefix(row,"+BCS:",p)){if(!wideband_||!(status_.agFeatures&(1u<<9))||!status_.slcEstablished)return BTH_IGNORED;if(!scalar(p,n))return BTH_ERR_PARAM;if(n==1||n==2){codecRequest_=static_cast<uint8_t>(n);codecListPending_=false;}else{codecRequest_=0;codecListPending_=true;}return BTH_OK;}
 return BTH_IGNORED;
}
bth_status_t HfpControl::receive(uint32_t g,const uint8_t* bytes,size_t n,uint32_t now){
 if(!live(g))return BTH_IGNORED;expire(g,now);if(!live(g))return BTH_IGNORED;if(n&&!bytes)return BTH_ERR_PARAM;
 for(size_t i=0;i<n;++i){const char* row;HfpAtLine::Result r=lines_.push(bytes[i],row);if(r==HfpAtLine::Result::Malformed||r==HfpAtLine::Result::Overflow){status_.state=HfpState::Failed;status_.slcEstablished=false;return r==HfpAtLine::Result::Overflow?BTH_ERR_LENGTH:BTH_ERR_PARAM;}
  if(r==HfpAtLine::Result::Line){bth_status_t st=line(row);if(st<0){status_.state=HfpState::Failed;status_.slcEstablished=false;return st;}if(status_.state==HfpState::Failed)return BTH_OK;}}
 return BTH_OK;
}
bth_status_t HfpControl::command(uint32_t g,HfpCommand command,const char* arg){
 if(!live(g))return BTH_IGNORED;if(!ready())return BTH_ERR_STATE;if(transaction_!=Transaction::None||user_[0]||codecRequest_||codecListPending_)return BTH_MORE;
 char row[48];size_t used=0;
 switch(command){
 case HfpCommand::Answer:if(arg&&*arg)return BTH_ERR_PARAM;used=literal(row,"ATA\r");break;
 case HfpCommand::Hangup:if(arg&&*arg)return BTH_ERR_PARAM;used=literal(row,"AT+CHUP\r");break;
 case HfpCommand::Redial:if(arg&&*arg)return BTH_ERR_PARAM;used=literal(row,"AT+BLDN\r");break;
 case HfpCommand::Dial:{if(!arg||!*arg)return BTH_ERR_PARAM;used=literal(row,"ATD");size_t i=0;for(;arg[i];++i){char c=arg[i];if(i>=32||!((c>='0'&&c<='9')||c=='*'||c=='#'||(c=='+'&&i==0)))return BTH_ERR_PARAM;row[used++]=c;}if(i==1&&arg[0]=='+')return BTH_ERR_PARAM;row[used++]=';';row[used++]='\r';break;}
 case HfpCommand::Dtmf:if(!arg||!arg[0]||arg[1]||!((arg[0]>='0'&&arg[0]<='9')||(arg[0]>='A'&&arg[0]<='D')||arg[0]=='*'||arg[0]=='#'))return BTH_ERR_PARAM;used=literal(row,"AT+VTS=");row[used++]=arg[0];row[used++]='\r';break;
 default:return BTH_ERR_PARAM;
 }
 row[used]=0;std::memcpy(user_,row,used+1);status_.lastCommandOk=false;return BTH_OK;
}
bool HfpControl::indicator(HfpIndicator kind,uint8_t& value)const{const uint8_t raw=static_cast<uint8_t>(kind);if(raw>=7)return false;const uint8_t wanted=static_cast<uint8_t>(raw+1);if(!status_.slcEstablished)return false;for(uint8_t i=0;i<status_.indicatorCount;++i)if(indicatorKinds_[i]==wanted){value=status_.indicators[i];return true;}return false;}
bth_status_t HfpControl::expire(uint32_t g,uint32_t now){if(!live(g))return BTH_IGNORED;if(transaction_==Transaction::None||static_cast<int32_t>(now-deadline_)<0)return BTH_MORE;finish(false);status_.state=HfpState::Failed;status_.slcEstablished=false;lines_.reset();return BTH_OK;}
}}
