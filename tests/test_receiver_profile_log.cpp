#include "../examples/Receiver/ProfileLog.h"
#include "../examples/Receiver/Console.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
TestSerial _SerialUSB;
struct Fake {
 unsigned dropped=0; bool reject=false; std::vector<std::string> lines;
 bool print(const char *fmt,...) { char line[1024]; va_list args; va_start(args,fmt); int n=vsnprintf(line,sizeof(line),fmt,args); va_end(args); assert(n>0&&n<1024); if(reject){++dropped;return false;}lines.emplace_back(line);return true; }
 bool has(const char*s)const{for(const auto&l:lines)if(l.find(s)!=std::string::npos)return true;return false;}
};
using namespace giga_bredr;
static void tick(receiver_log::ProfileLog& p,Fake& c,uint32_t n,const AvrcpStatus&a,const AvrcpMetadata&m,const ReceiverStats&s,const BleStats&b){size_t before=c.lines.size();unsigned dropped=c.dropped;p.poll(c,n,a,m,s,b);assert(c.lines.size()-before+c.dropped-dropped<=1);}
int main(){
 unsigned tests=0;
 { ReceiverConsole c; assert(c.print("%s", "ready")); assert(c.dropped==0); assert(!c.print("%s", "")); assert(c.dropped==1); char too_long[1100];memset(too_long,'x',1099);too_long[1099]=0;assert(!c.print("%s",too_long));assert(c.dropped==2); int accepted=0;while(c.print("%01000d",0)){++accepted;assert(accepted<10);}assert(accepted==4);assert(c.dropped==3);++tests; }
 { auto e=receiver_log::escapeText("\"\\\n\xff",4); assert(std::string(e.text)=="\\\"\\\\\\u000a\\u00ff");assert(e.invalid_utf8_bytes==1); char t[127]={};auto x=receiver_log::escapeText(t,sizeof t);assert(strlen(x.text)==762); Fake c;receiver_log::metadataEvent(c,1,1,t,sizeof t);assert(c.lines[0].size()<1024&&c.has("source=poll_callback"));receiver_log::volumeEvent(c,1,127);assert(c.has("value=127 range=0..127 value_in_range=1"));++tests; }
 { receiver_log::ProfileLog p;Fake c;AvrcpStatus a;AvrcpMetadata m;ReceiverStats s;BleStats b;
   tick(p,c,1,a,m,s,b);tick(p,c,251,a,m,s,b);tick(p,c,501,a,m,s,b);assert(c.has("cached_volume=127 cached_volume_in_range=1"));assert(c.has("playback=255 playback_name=unknown playback_known=0"));assert(c.has("GBR_METADATA_STATE")&&c.has("GBR_LE_TRAFFIC"));assert(!c.has("cached_volume_valid"));++tests; }
 { receiver_log::ProfileLog p;Fake c;AvrcpStatus a;AvrcpMetadata m;ReceiverStats s;BleStats b;
   for(uint32_t n=1;n<=501;n+=250)tick(p,c,n,a,m,s,b);c.lines.clear();a.connected=true;a.generation=3;a.playback_status=1;c.reject=true;tick(p,c,751,a,m,s,b);assert(c.dropped==1);a.playback_status=2;c.reject=false;
   for(uint32_t n=1001;n<=3001;n+=250)tick(p,c,n,a,m,s,b);assert(c.has("avrcp_connected=1 avrcp_gen=3 playback=2 playback_name=paused"));assert(!c.has("playback=1 playback_name=playing"));++tests; }
 { receiver_log::ProfileLog p;Fake c;AvrcpStatus a;AvrcpMetadata m;ReceiverStats s;BleStats b;
   for(uint32_t n=1;n<=501;n+=250)tick(p,c,n,a,m,s,b);c.lines.clear();m.generation=13;m.valid_attributes=3;m.truncated_attributes=1;strcpy(m.title,"title one");m.artist[0]=0;c.reject=true;
   for(uint32_t n=751;n<=2251;n+=250)tick(p,c,n,a,m,s,b);assert(c.dropped>=3);strcpy(m.title,"new title");c.reject=false;
   for(uint32_t n=2501;n<=5501;n+=250)tick(p,c,n,a,m,s,b);assert(c.has("source=snapshot generation=13 valid=03 truncated=01"));assert(c.has("source=snapshot generation=13 attribute=1 name=title valid=1 upstream_truncated=1"));assert(c.has("text=\"new title\""));assert(c.has("attribute=2 name=artist valid=1 upstream_truncated=0 bytes_before_nul=0 invalid_utf8_bytes=0"));assert(!c.has("title one"));
   c.lines.clear();m.valid_attributes=0;for(uint32_t n=5751;n<=7501;n+=250)tick(p,c,n,a,m,s,b);assert(c.has("generation=13 valid=00"));m.valid_attributes=1;for(uint32_t n=7751;n<=10001;n+=250)tick(p,c,n,a,m,s,b);assert(c.has("attribute=1 name=title"));++tests; }
 { receiver_log::ProfileLog p;Fake c;AvrcpStatus a;AvrcpMetadata m;ReceiverStats s;BleStats b;
   tick(p,c,0xffffff80u,a,m,s,b);assert(!p.due(10));assert(p.due(122));for(uint32_t n=122;n<=1872;n+=250)tick(p,c,n,a,m,s,b);assert(c.has("GBR_METADATA_STATE")&&c.has("GBR_LE_TRAFFIC"));c.lines.clear();a.song_position_ms=42;for(uint32_t n=2122;n<=3872;n+=250)tick(p,c,n,a,m,s,b);assert(c.has("position_known=1 position_ms=42"));++tests; }
 { receiver_log::ProfileLog p;Fake c;AvrcpStatus a;AvrcpMetadata m;ReceiverStats s;BleStats b;
   for(uint32_t n=1;n<=501;n+=250)tick(p,c,n,a,m,s,b);c.lines.clear();b.connected=true;b.notify_enabled=true;b.generation=7;b.writes=10;c.reject=true;tick(p,c,751,a,m,s,b);assert(c.dropped==1);b.writes=13;c.reject=false;for(uint32_t n=1001;n<=3001;n+=250)tick(p,c,n,a,m,s,b);assert(c.has("generation=7 connected=1 subscribed=1")&&c.has("received_writes=13"));++tests; }
 { receiver_log::ProfileLog p;Fake c;AvrcpStatus a;AvrcpMetadata m;ReceiverStats s;BleStats b;
   m.generation=5;m.valid_attributes=1;strcpy(m.title,"old");for(uint32_t n=1;n<=2001;n+=250)tick(p,c,n,a,m,s,b);assert(c.has("text=\"old\""));c.lines.clear();strcpy(m.title,"replacement");for(uint32_t n=2251;n<=4001;n+=250)tick(p,c,n,a,m,s,b);assert(c.has("text=\"replacement\""));c.lines.clear();m.generation=6;for(uint32_t n=4251;n<=6001;n+=250)tick(p,c,n,a,m,s,b);assert(c.has("generation=6 attribute=1"));++tests; }
 { receiver_log::ProfileLog p;Fake c;AvrcpStatus a;AvrcpMetadata m;ReceiverStats s;BleStats b;
   m.generation=22;m.valid_attributes=1;memset(m.title,1,127);m.title[127]=0;for(uint32_t n=1;n<=2001;n+=250)tick(p,c,n,a,m,s,b);assert(c.has("generation=22 attribute=1")&&c.has("bytes_before_nul=127 invalid_utf8_bytes=0"));++tests; }
 { const char text[]="Ti\xe1\xba\xbfng Vi\xe1\xbb\x87t \xf0\x9f\x8e\xb5";auto x=receiver_log::escapeText(text,sizeof(text)-1);assert(!x.invalid_utf8_bytes&&!x.omitted_bytes&&std::string(x.text)==text);++tests; }
 { const char text[]={char(0xc0),char(0x80),char(0xed),char(0xa0),char(0x80),char(0xf4),char(0x90),char(0x80),char(0x80),char(0xe2),char(0x82)};auto x=receiver_log::escapeText(text,sizeof text);assert(x.invalid_utf8_bytes==sizeof text);++tests; }
 { char text[128];memset(text,'x',sizeof text);text[126]=char(0xc3);text[127]=char(0xa9);auto x=receiver_log::escapeText(text,sizeof text);assert(x.input_bytes==127&&x.omitted_bytes==1&&x.invalid_utf8_bytes==1);++tests; }
 { auto x=receiver_log::escapeText(nullptr,12);assert(!x.input_bytes&&x.omitted_bytes==12&&!x.text[0]);++tests; }
 { Fake c;receiver_log::volumeEvent(c,1,0);receiver_log::volumeEvent(c,2,128);assert(c.has("value=0 range=0..127 value_in_range=1")&&c.has("value=128 range=0..127 value_in_range=0"));++tests; }
 printf("PROFILE_LOG_CASES_PASS=%u\n",tests);
}
