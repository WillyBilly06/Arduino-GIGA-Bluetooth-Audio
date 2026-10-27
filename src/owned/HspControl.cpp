#include "HspControl.h"
#include <cstring>
namespace giga_bredr { namespace owned {
bool HspControl::begin(uint32_t g,uint8_t speaker,uint8_t mic){if(!g||speaker>15||mic>15)return false;end();status_.generation=g;status_.speakerGain=speaker;status_.microphoneGain=mic;status_.connected=true;return true;}
void HspControl::end(){status_={};lines_.reset();pending_[0]=0;deadline_=0;sent_=false;}
bth_status_t HspControl::queue(uint32_t g,const char* p){if(!live(g))return BTH_IGNORED;if(sent_||pending_[0])return BTH_MORE;std::strcpy(pending_,p);status_.lastCommandOk=false;return BTH_OK;}
bth_status_t HspControl::button(uint32_t g){return queue(g,"AT+CKPD=200\r");}
bth_status_t HspControl::setGain(uint32_t g,bool speaker,uint8_t v){if(v>15)return BTH_ERR_PARAM;char row[16];std::memcpy(row,speaker?"AT+VGS=":"AT+VGM=",7);size_t n=7+hfp_at::decimal(v,row+7);row[n++]='\r';row[n]=0;return queue(g,row);}
bth_status_t HspControl::next(uint32_t g,uint32_t now,char* out,size_t cap,size_t& n){n=0;if(!live(g))return BTH_IGNORED;expire(g,now);if(!live(g))return BTH_IGNORED;if(!out)return BTH_ERR_PARAM;if(sent_)return BTH_MORE;if(!pending_[0])return BTH_OK;size_t len=std::strlen(pending_);if(cap<len)return BTH_ERR_CAPACITY;std::memcpy(out,pending_,len);n=len;pending_[0]=0;sent_=true;deadline_=now+5000;return BTH_OK;}
void HspControl::finish(bool ok){if(!sent_)return;sent_=false;deadline_=0;status_.lastCommandOk=ok;if(status_.completedCommands!=UINT32_MAX)++status_.completedCommands;}
bth_status_t HspControl::receive(uint32_t g,const uint8_t* p,size_t n,uint32_t now){if(!live(g))return BTH_IGNORED;expire(g,now);if(!live(g))return BTH_IGNORED;if(n&&!p)return BTH_ERR_PARAM;
 for(size_t i=0;i<n;++i){const char* row;auto r=lines_.push(p[i],row);if(r==HfpAtLine::Result::Malformed||r==HfpAtLine::Result::Overflow){status_.failed=true;return r==HfpAtLine::Result::Overflow?BTH_ERR_LENGTH:BTH_ERR_PARAM;}
  if(r!=HfpAtLine::Result::Line)continue;
  if(!std::strcmp(row,"OK")){finish(true);continue;}if(!std::strcmp(row,"ERROR")){finish(false);continue;}if(!std::strcmp(row,"RING")){if(status_.rings!=UINT32_MAX)++status_.rings;continue;}
  const char* tail;bool speaker=hfp_at::prefix(row,"+VGS=",tail);if(speaker||hfp_at::prefix(row,"+VGM=",tail)){uint32_t value;if(!hfp_at::number(tail,value)||!hfp_at::end(tail)||value>15){status_.failed=true;return BTH_ERR_PARAM;}if(speaker)status_.speakerGain=static_cast<uint8_t>(value);else status_.microphoneGain=static_cast<uint8_t>(value);}
 }
 return BTH_OK;
}
bth_status_t HspControl::expire(uint32_t g,uint32_t now){if(!live(g))return BTH_IGNORED;if(!sent_||static_cast<int32_t>(now-deadline_)<0)return BTH_MORE;finish(false);status_.failed=true;lines_.reset();return BTH_OK;}
}}
