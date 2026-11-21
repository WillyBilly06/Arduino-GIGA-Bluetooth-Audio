#pragma once
#include <GigaBredr.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Receiver example diagnostics. Invoke only from loop()/poll() callbacks.
// No allocation, USB write, delay, worker callback or interrupt work occurs here.
namespace receiver_log {
constexpr size_t TextLimit = 127;
struct EscapedText {
    char text[TextLimit * 6 + 1] = {};
    size_t input_bytes = 0, omitted_bytes = 0, invalid_utf8_bytes = 0;
};
inline unsigned utf8Sequence(const unsigned char* p,size_t left) {
    const unsigned c=p[0];
    if(c<0x80)return 1;
    unsigned n=c>=0xc2&&c<=0xdf?2:c>=0xe0&&c<=0xef?3:c>=0xf0&&c<=0xf4?4:0;
    if(!n||left<n)return 0;
    for(unsigned i=1;i<n;++i)if((p[i]&0xc0)!=0x80)return 0;
    if((c==0xe0&&p[1]<0xa0)||(c==0xed&&p[1]>=0xa0)||
       (c==0xf0&&p[1]<0x90)||(c==0xf4&&p[1]>=0x90))return 0;
    return n;
}
inline EscapedText escapeText(const char* data,size_t bytes) {
    EscapedText out;
    if(!data){out.omitted_bytes=bytes;return out;}
    out.input_bytes=bytes<TextLimit?bytes:TextLimit;out.omitted_bytes=bytes-out.input_bytes;
    size_t used=0;
    const char hex[]="0123456789abcdef";
    for(size_t i=0;i<out.input_bytes;){
        const auto* p=reinterpret_cast<const unsigned char*>(data)+i;
        const unsigned c=p[0];const unsigned n=utf8Sequence(p,out.input_bytes-i);
        if(c=='"'||c=='\\'){out.text[used++]='\\';out.text[used++]=char(c);++i;}
        else if(c<0x20||c==0x7f||!n){
            out.text[used++]='\\';out.text[used++]='u';out.text[used++]='0';out.text[used++]='0';
            out.text[used++]=hex[c>>4];out.text[used++]=hex[c&15];
            if(!n)++out.invalid_utf8_bytes;
            ++i;
        }else{for(unsigned j=0;j<n;++j)out.text[used++]=data[i++];}
    }
    out.text[used]=0;return out;
}
inline const char* attributeName(uint8_t a) {
    switch(a){case 1:return "title";case 2:return "artist";case 3:return "album";case 7:return "playing_time";default:return "other";}
}
inline const char* playbackName(uint8_t s) {
    switch(s){case 0:return "stopped";case 1:return "playing";case 2:return "paused";
      case 3:return "forward_seek";case 4:return "reverse_seek";default:return "unknown";}
}
template<class Console> void metadataEvent(Console& c,uint32_t ms,uint8_t attribute,const char* text,size_t bytes) {
    const auto safe=escapeText(text,bytes);
    c.print("GBR_METADATA ms=%lu source=poll_callback attribute=%u name=%s bytes=%u text_present=%u logged_bytes=%u log_omitted=%u invalid_utf8_bytes=%u text=\"%s\"\n",
      (unsigned long)ms,unsigned(attribute),attributeName(attribute),unsigned(bytes),unsigned(text!=nullptr),
      unsigned(safe.input_bytes),unsigned(safe.omitted_bytes),unsigned(safe.invalid_utf8_bytes),safe.text);
}
template<class Console> void volumeEvent(Console& c,uint32_t ms,uint8_t value) {
    c.print("GBR_VOLUME ms=%lu source=poll_callback value=%u range=0..127 value_in_range=%u\n",
      (unsigned long)ms,unsigned(value),unsigned(value<=127));
}

// Each successful print is ring admission, not USB or peer delivery. Failed
// attempts remain pending; each 250 ms tick attempts at most one record and
// rotates classes so sustained pressure cannot starve the other classes.
class ProfileLog {
    bool sampled_once_=false,profile_seen_=false,meta_seen_=false,ble_seen_=false;
    uint32_t sampled_=0,profile_at_=0,ble_at_=0;
    uint8_t next_=0;
    giga_bredr::AvrcpStatus av_;
    uint32_t metadata_generation_=0;
    uint8_t metadata_valid_=0,metadata_truncated_=0,volume_=127;
    bool classic_=false,streaming_=false,playing_=false,output_=false;
    bool ble_connected_=false,ble_subscribed_=false;
    uint32_t ble_generation_=0;
    struct AdmittedText { bool seen=false; uint32_t generation=0; bool truncated=false; char value[128]={}; } text_[4];
    static size_t textLength(const char* s) { size_t n=0; while(n<127 && s[n]) ++n; return n; }
public:
    // Sampling changes may coalesce; this is not a wire-event capture.
    bool due(uint32_t now)const{return !sampled_once_||uint32_t(now-sampled_)>=250;}
    template<class Console> void poll(Console& c,uint32_t now,const giga_bredr::AvrcpStatus& a,
      const giga_bredr::AvrcpMetadata& m,const giga_bredr::ReceiverStats& s,const giga_bredr::BleStats& b) {
        if(!due(now))return;
        sampled_once_=true;sampled_=now;
        const uint8_t bits[]={1,2,4,64};
        const uint8_t ids[]={1,2,3,7};
        const char* values[]={m.title,m.artist,m.album,m.playing_time};
        for(unsigned i=0;i<4;++i)if(!(m.valid_attributes&bits[i]))text_[i].seen=false;
        const bool profile_changed=!profile_seen_||a.connected!=av_.connected||a.generation!=av_.generation||
          a.playback_status!=av_.playback_status||a.volume!=av_.volume||a.song_length_ms!=av_.song_length_ms||
          a.song_position_ms!=av_.song_position_ms||s.absolute_volume!=volume_||s.radio.connected!=classic_||
          s.radio.streaming!=streaming_||s.playing!=playing_||s.output.running!=output_;
        const bool metadata_changed=!meta_seen_||m.generation!=metadata_generation_||
          m.valid_attributes!=metadata_valid_||m.truncated_attributes!=metadata_truncated_;
        const bool ble_changed=!ble_seen_||b.connected!=ble_connected_||b.notify_enabled!=ble_subscribed_||b.generation!=ble_generation_;
        for(unsigned attempt=0;attempt<7;++attempt){
            const unsigned kind=(next_+attempt)%7;
            bool needed=false,admitted=false;
            if(kind==0 && (profile_changed||uint32_t(now-profile_at_)>=10000)){
              needed=true;
              admitted=c.print("GBR_PROFILE ms=%lu source=snapshot avrcp_connected=%u avrcp_gen=%lu playback=%u playback_name=%s playback_known=%u cached_volume=%u cached_volume_in_range=%u applied_volume=%u applied_volume_in_range=%u volume_range=0..127 length_known=%u length_ms=%lu position_known=%u position_ms=%lu classic_connected=%u a2dp_streaming=%u receiver_playing=%u output_running=%u console_drops=%u\n",
                (unsigned long)now,unsigned(a.connected),(unsigned long)a.generation,unsigned(a.playback_status),playbackName(a.playback_status),
                unsigned(a.playback_status<=4),unsigned(a.volume),unsigned(a.volume<=127),unsigned(s.absolute_volume),unsigned(s.absolute_volume<=127),
                unsigned(a.song_length_ms!=0xffffffffu),(unsigned long)a.song_length_ms,unsigned(a.song_position_ms!=0xffffffffu),
                (unsigned long)a.song_position_ms,unsigned(s.radio.connected),unsigned(s.radio.streaming),unsigned(s.playing),unsigned(s.output.running),c.dropped);
              if(admitted){profile_seen_=true;profile_at_=now;av_=a;volume_=s.absolute_volume;classic_=s.radio.connected;streaming_=s.radio.streaming;playing_=s.playing;output_=s.output.running;}
            }else if(kind==1 && metadata_changed){
              needed=true;
              admitted=c.print("GBR_METADATA_STATE ms=%lu source=snapshot generation=%lu valid=%02x truncated=%02x title_known=%u artist_known=%u album_known=%u playing_time_known=%u\n",
                (unsigned long)now,(unsigned long)m.generation,unsigned(m.valid_attributes),unsigned(m.truncated_attributes),
                unsigned((m.valid_attributes&1)!=0),unsigned((m.valid_attributes&2)!=0),unsigned((m.valid_attributes&4)!=0),unsigned((m.valid_attributes&64)!=0));
              if(admitted){meta_seen_=true;metadata_generation_=m.generation;metadata_valid_=m.valid_attributes;metadata_truncated_=m.truncated_attributes;}
            }else if(kind>=2 && kind<=5){
              const unsigned i=kind-2;const bool trunc=(m.truncated_attributes&bits[i])!=0;
              if((m.valid_attributes&bits[i]) && (!text_[i].seen || text_[i].generation!=m.generation || text_[i].truncated!=trunc || strncmp(text_[i].value,values[i],128)!=0)){
                needed=true;const size_t n=textLength(values[i]);const auto safe=escapeText(values[i],n);
                admitted=c.print("GBR_METADATA_SNAPSHOT ms=%lu source=snapshot generation=%lu attribute=%u name=%s valid=1 upstream_truncated=%u bytes_before_nul=%u invalid_utf8_bytes=%u text=\"%s\"\n",
                  (unsigned long)now,(unsigned long)m.generation,unsigned(ids[i]),attributeName(ids[i]),unsigned(trunc),unsigned(n),unsigned(safe.invalid_utf8_bytes),safe.text);
                if(admitted){text_[i].seen=true;text_[i].generation=m.generation;text_[i].truncated=trunc;memcpy(text_[i].value,values[i],128);}
              }
            }else if(kind==6 && (ble_changed||uint32_t(now-ble_at_)>=2000)){
              needed=true;
              admitted=c.print("GBR_LE_TRAFFIC ms=%lu generation=%lu connected=%u subscribed=%u mtu=%u received_writes=%lu notifications_queued=%lu capacity_retries=%lu command_drops=%lu disconnects=%lu last_reason=0x%02x counters=host_receipts_not_peer_delivery console_drops=%u\n",
                (unsigned long)now,(unsigned long)b.generation,unsigned(b.connected),unsigned(b.notify_enabled),unsigned(b.att_mtu),
                (unsigned long)b.writes,(unsigned long)b.notifications_queued,(unsigned long)b.capacity_retries,(unsigned long)b.write_command_drops,(unsigned long)b.disconnects,unsigned(b.last_disconnect_reason),c.dropped);
              if(admitted){ble_seen_=true;ble_at_=now;ble_connected_=b.connected;ble_subscribed_=b.notify_enabled;ble_generation_=b.generation;}
            }
            if(needed){next_=uint8_t((kind+1)%7);return;}
        }
    }
};
} // namespace receiver_log
