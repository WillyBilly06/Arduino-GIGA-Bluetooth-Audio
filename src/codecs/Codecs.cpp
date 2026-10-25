#include "Codecs.h"
#include "../CodecBuildConfig.h"
#include "../detail/Memory.h"
#include "../detail/SbcConfig.h"
#include <string.h>
#include <limits.h>
extern "C" {
#if GBR_ENABLE_SBC
#include "../vendor/codecs/sbc/include/oi_codec_sbc.h"
#endif
#if GBR_ENABLE_AAC
#include "../vendor/codecs/aac/aacdec.h"
#include "../vendor/codecs/aac/latm_parser.h"
#endif
#if GBR_ENABLE_APTX
#include "../vendor/codecs/aptx/freeaptx.h"
#endif
#if GBR_ENABLE_LDAC
#include "../vendor/codecs/ldac/inc/ldacBT.h"
#endif
#if GBR_ENABLE_LHDC
#include "../vendor/codecs/lhdc/lhdc_dec.h"
#include "../vendor/codecs/lhdc/lhdc_imdct.h"
#include "../vendor/codecs/lhdc/lhdc_entropy_dec.h"
int gbr_lhdc_entropy_ready(void);
#endif
#if GBR_ENABLE_OPUS
#include "../vendor/codecs/opus/include/opus.h"
#endif
}
// Single serialized decoder worker owns this guard and all LHDC module caches.
static uintptr_t ldacFirst=0,ldacLast=0;
static bool ldacFailed=true;
extern "C" uint32_t gbr_ldac_read24(const uint8_t* p,int bits,int offset) {
    uintptr_t at=(uintptr_t)p;
    if(ldacFailed || bits<1 || bits>16 || offset<0 || offset>7 || at<ldacFirst || at>=ldacLast ||
       (size_t)((bits+offset+7)/8)>ldacLast-at) { ldacFailed=true;return 0; }
    size_t n=ldacLast-at;
    return (uint32_t(p[0])<<16)|(n>1?uint32_t(p[1])<<8:0)|(n>2?p[2]:0);
}
// Pipeline measures decode time on the target. No host clocks or I/O in codec loops.
extern "C" int64_t gbr_codec_now_us(void) { return 0; }
namespace giga_bredr { namespace detail {
static DecodeResult result(Status s,size_t used=0,size_t frames=0) { return {s,used,frames}; }
static bool common(const CodecConfig& c) { return c.channels>=1 && c.channels<=2; }
static bool rate(uint32_t r) { return r==44100 || r==48000; }
static bool io(const uint8_t* in,size_t n,int32_t* pcm,size_t cap) { return in && n && pcm && cap; }
static void expand16(int32_t* out,const int16_t* in,size_t n) {
    for(size_t i=0;i<n;++i) out[i]=int32_t(in[i])*65536;
}
#if GBR_ENABLE_SBC
struct SbcState { OI_CODEC_SBC_DECODER_CONTEXT decoder; CodecConfig config; int16_t* pcm; OI_UINT32* work; };
static void sbcClose(void* p) { auto s=(SbcState*)p;if(!s)return;gbr_free(s->pcm);gbr_free(s->work);gbr_free(s); }
static Status sbcOpen(void** p,const CodecConfig& c) {
    *p=nullptr;SbcGeometry geometry;if(!sbcGeometry(c,geometry))return Status::Unsupported;
    auto s=(SbcState*)gbr_calloc(1,sizeof(SbcState));if(!s)return Status::OutOfMemory;s->config=c;
    // Four-band OI synthesis also uses an 80-halfword filter window; retain
    // the fast eight-wide ring while sizing subband samples to this stream.
    size_t workBytes=sizeof(int32_t)*geometry.frames()*c.channels+sizeof(SBC_BUFFER_T)*c.channels*8*SBC_CODEC_FAST_FILTER_BUFFERS;
    s->work=(OI_UINT32*)gbr_calloc(1,workBytes);s->pcm=(int16_t*)gbr_malloc(geometry.frames()*c.channels*sizeof(int16_t));
    if(!s->work || !s->pcm) { sbcClose(s);return Status::OutOfMemory; }
    if(OI_CODEC_SBC_DecoderResetGeometry(&s->decoder,s->work,workBytes,c.channels,c.channels,0,0,geometry.subbands,geometry.blocks)!=OI_OK) { sbcClose(s);return Status::Unsupported; }
    *p=s;return Status::Ok;
}
static DecodeResult sbcDecode(void* p,const uint8_t* in,size_t n,int32_t* pcm,size_t cap) {
    if(!io(in,n,pcm,cap))return result(cap?Status::InvalidArgument:Status::OutputFull);
    auto s=(SbcState*)p;if(n<4)return result(Status::NeedMoreData);
    if(in[0]!=0x9c)return result(Status::CorruptFrame);
    unsigned bands=(in[1]&1)?8:4,blocks=4+4*((in[1]>>4)&3),mode=(in[1]>>2)&3,channels=mode?2:1;
    static const uint32_t rates[]={16000,32000,44100,48000};
    if(channels!=s->config.channels || rates[in[1]>>6]!=s->config.sample_rate)return result(Status::Unsupported);
    if(s->config.codec_info_size) {
        const uint8_t* selected=s->config.codec_info;
        if(!(selected[0]&(8>>mode)) || !(selected[1]&(0x80>>((blocks/4)-1))) ||
           !(selected[1]&(bands==8?4:8)) || !(selected[1]&((in[1]&2)?2:1)) ||
           in[2]<selected[2] || in[2]>selected[3])return result(Status::Unsupported);
    }
    size_t frames=bands*blocks;if(cap<frames)return result(Status::OutputFull);
    size_t audioBits=blocks*in[2]*((mode<2)?channels:1)+(mode==3?bands:0);
    size_t length=4+(4*bands*channels)/8+(audioBits+7)/8;
    if(n<length)return result(Status::NeedMoreData);
    const OI_BYTE* data=in;OI_UINT32 bytes=length,pcmBytes=frames*channels*2;
    auto status=OI_CODEC_SBC_DecodeFrame(&s->decoder,&data,&bytes,s->pcm,&pcmBytes);
    if(status!=OI_OK)return result(Status::CorruptFrame);
    frames=pcmBytes/(2*channels);expand16(pcm,s->pcm,frames*channels);
    return result(Status::Ok,length-bytes,frames);
}
static const DecoderOps sbcOps={sbcOpen,sbcDecode,sbcClose};
#endif
#if GBR_ENABLE_AAC
struct AacState { HAACDecoder decoder; CodecConfig config; latm_parser_t latm; uint8_t* frame; int16_t* pcm; uint8_t framing; bool he; size_t frames; uint32_t coreRate; };
static void aacClose(void* p) { auto s=(AacState*)p;if(!s)return;AACFreeDecoder(s->decoder);gbr_free(s->frame);gbr_free(s->pcm);gbr_free(s); }
static Status aacOpen(void** p,const CodecConfig& c) {
    *p=nullptr;if(!common(c)||(c.codec==Codec::AacHe && c.sample_rate<16000)||!(rate(c.sample_rate)||c.sample_rate==32000||c.sample_rate==24000||c.sample_rate==16000||c.sample_rate==8000))return Status::Unsupported;
    auto s=(AacState*)gbr_calloc(1,sizeof(AacState));if(!s)return Status::OutOfMemory;s->config=c;s->he=c.codec==Codec::AacHe;s->frames=s->he?2048:1024;s->coreRate=s->he?c.sample_rate/2:c.sample_rate;
    // A2DP uses LATM. Raw and ADTS are explicit diagnostic modes.
    s->framing=static_cast<uint8_t>(c.aac_transport);if(s->framing>2){aacClose(s);return Status::Unsupported;}
    s->decoder=AACInitDecoderRawChannels(c.channels);
    if(s->he && s->decoder && AACEnableSBR(s->decoder)){aacClose(s);return Status::OutOfMemory;}
    s->frame=(uint8_t*)gbr_malloc(768*c.channels+16);
    s->pcm=(int16_t*)gbr_malloc(s->frames*c.channels*2);
    if(!s->decoder||!s->frame||!s->pcm){aacClose(s);return Status::OutOfMemory;}
    latm_parser_init(&s->latm);*p=s;return Status::Ok;
}
static DecodeResult aacDecode(void* p,const uint8_t* in,size_t n,int32_t* pcm,size_t cap) {
    if(!io(in,n,pcm,cap))return result(cap?Status::InvalidArgument:Status::OutputFull);
    auto s=(AacState*)p;if(cap<s->frames)return result(Status::OutputFull);
    if(n>65535)return result(Status::CorruptFrame);
    size_t frameBytes=0,consumed=n;int raw=1;
    if(s->framing==0) {
        auto candidate=s->latm;
        auto error=latm_parse_frame(&candidate,in,n,s->frame,768*s->config.channels,&frameBytes,nullptr);
        if(error!=LATM_OK)return result(error==LATM_ERR_NOT_ENOUGH_DATA?Status::NeedMoreData:error==LATM_ERR_UNSUPPORTED_CONFIG?Status::Unsupported:Status::CorruptFrame);
        const auto* asc=latm_get_audio_config(&candidate);
        if(!asc||(!s->he && asc->object_type!=2)||(s->he && asc->object_type!=2 && asc->object_type!=5)||(asc->object_type==5 && asc->extension_sample_rate!=s->config.sample_rate)||asc->sample_rate!=s->coreRate||asc->channel_config!=s->config.channels||asc->frame_length_flag)return result(Status::Unsupported);
        s->latm=candidate;
    } else if(s->framing==1) {
        if(n>768*s->config.channels)return result(Status::CorruptFrame);frameBytes=n;memcpy(s->frame,in,n);
    } else {
        if(n<7)return result(Status::NeedMoreData);
        if(in[0]!=0xff || (in[1]&0xf6)!=0xf0)return result(Status::CorruptFrame);
        static const uint32_t rates[]={96000,88200,64000,48000,44100,32000,24000,22050,16000,12000,11025,8000,7350};
        unsigned profile=in[2]>>6,idx=(in[2]>>2)&15,ch=((in[2]&1)<<2)|(in[3]>>6);
        if(profile!=1||idx>=13||rates[idx]!=s->coreRate||ch!=s->config.channels||(in[6]&3))return result(Status::Unsupported);
        size_t length=((in[3]&3)<<11)|(in[4]<<3)|(in[5]>>5),header=(in[1]&1)?7:9;
        // The payload limit excludes the actual 7- or 9-byte ADTS header.
        // Keep the full 16-byte core-reader padding inside its allocation.
        if(length<header||length-header>768*s->config.channels)return result(Status::CorruptFrame);
        if(n<length)return result(Status::NeedMoreData);
        frameBytes=length-header;memcpy(s->frame,in+header,frameBytes);consumed=length;
    }
    // Always supply explicit LC/rate/channels; ADTS is validated and stripped above.
    AACFrameInfo info={};info.nChans=s->config.channels;info.sampRateCore=s->coreRate;info.profile=AAC_PROFILE_LC;
    if(AACSetRawBlockParams(s->decoder,0,&info)!=0)return result(Status::Unsupported);
    memset(s->frame+frameBytes,0,16);unsigned char* input=s->frame;int left=(int)frameBytes;
    int error=AACDecode(s->decoder,&input,&left,s->pcm);
    if(error) { AACFlushCodec(s->decoder);return result(error==ERR_AAC_INDATA_UNDERFLOW?Status::NeedMoreData:Status::CorruptFrame); }
    AACGetLastFrameInfo(s->decoder,&info);
    if(info.nChans!=s->config.channels||info.outputSamps!=s->frames*s->config.channels||info.sampRateOut!=(int)s->config.sample_rate)return result(Status::CorruptFrame);
    expand16(pcm,s->pcm,s->frames*s->config.channels);return result(Status::Ok,consumed,s->frames);
}
static const DecoderOps aacOps={aacOpen,aacDecode,aacClose};
#endif
#if GBR_ENABLE_APTX
struct AptxState { aptx_context* decoder;bool hd; };
static void aptxClose(void* p) {auto s=(AptxState*)p;if(!s)return;aptx_finish(s->decoder);gbr_free(s);}
static Status aptxOpen(void** p,const CodecConfig& c) {
    *p=nullptr;if(c.channels!=2||!rate(c.sample_rate))return Status::Unsupported;
    auto s=(AptxState*)gbr_calloc(1,sizeof(AptxState));if(!s)return Status::OutOfMemory;s->hd=c.codec==Codec::AptxHd;s->decoder=aptx_init(s->hd);
    if(!s->decoder){aptxClose(s);return Status::OutOfMemory;}*p=s;return Status::Ok;
}
static DecodeResult aptxDecode(void* p,const uint8_t* in,size_t n,int32_t* pcm,size_t cap) {
    if(!io(in,n,pcm,cap))return result(cap?Status::InvalidArgument:Status::OutputFull);
    auto s=(AptxState*)p;size_t block=s->hd?6:4;
    if(n<block)return result(Status::NeedMoreData);if(n%block)return result(Status::CorruptFrame);
    // One block at a time bounds scratch and preserves the core's 90-sample priming.
    size_t used=0,frames=0;uint8_t packed[24];
    while(used<n) {
        if(cap-frames<4)return result(frames?Status::Ok:Status::OutputFull,used,frames);
        size_t wrote=0,got=aptx_decode(s->decoder,in+used,block,packed,sizeof(packed),&wrote);
        if(got!=block)return result(Status::CorruptFrame,used,frames);
        used+=got;
        for(size_t i=0;i<wrote/3;i++) {
            uint32_t v=uint32_t(packed[3*i])|(uint32_t(packed[3*i+1])<<8)|(uint32_t(packed[3*i+2])<<16);
            pcm[2*frames+i]=(int32_t)(v<<8);
        }
        frames+=wrote/6;
    }
    return result(Status::Ok,used,frames);
}
static const DecoderOps aptxOps={aptxOpen,aptxDecode,aptxClose};
#endif
#if GBR_ENABLE_LDAC
struct LdacState { HANDLE_LDAC_BT decoder;CodecConfig config; };
static void ldacClose(void* p) {auto s=(LdacState*)p;if(!s)return;ldacBT_free_handle(s->decoder);gbr_free(s);ldacFirst=ldacLast=0;ldacFailed=true;}
static Status ldacOpen(void** p,const CodecConfig& c) {
    *p=nullptr;if(!common(c)||!(rate(c.sample_rate)||c.sample_rate==88200||c.sample_rate==96000))return Status::Unsupported;
    auto s=(LdacState*)gbr_calloc(1,sizeof(LdacState));if(!s)return Status::OutOfMemory;s->config=c;s->decoder=ldacBT_get_handle();
    if(!s->decoder){ldacClose(s);return Status::OutOfMemory;}
    if(ldacBT_init_handle_decode(s->decoder,c.channels==1?LDACBT_CHANNEL_MODE_MONO:LDACBT_CHANNEL_MODE_STEREO,c.sample_rate,0,0,0)) {ldacClose(s);return Status::OutOfMemory;}
    *p=s;return Status::Ok;
}
static DecodeResult ldacDecode(void* p,const uint8_t* in,size_t n,int32_t* pcm,size_t cap) {
    if(!io(in,n,pcm,cap))return result(cap?Status::InvalidArgument:Status::OutputFull);
    auto s=(LdacState*)p;if(n<5)return result(Status::NeedMoreData);
    if(in[0]!=0xaa)return result(Status::CorruptFrame);
    unsigned sf=in[1]>>5,cci=(in[1]>>3)&3;static const uint32_t rates[]={44100,48000,88200,96000};
    if(sf>=4||rates[sf]!=s->config.sample_rate||cci>2||(cci==0?1:2)!=s->config.channels)return result(Status::Unsupported);
    size_t length=(((in[1]&7)<<6)|(in[2]>>2))+1+3;
    if(n<length)return result(Status::NeedMoreData);
    size_t frames=s->config.sample_rate>48000?256:128;if(cap<frames)return result(Status::OutputFull);
    ldacFirst=(uintptr_t)in;ldacLast=ldacFirst+length;ldacFailed=false;int used=0,wrote=0;
    int error=ldacBT_decode(s->decoder,const_cast<uint8_t*>(in),(uint8_t*)pcm,LDACBT_SMPL_FMT_S32,length,&used,&wrote);
    bool checked=!ldacFailed;ldacFirst=ldacLast=0;ldacFailed=true;
    if(error||!checked||used<0||used>(int)length||wrote<0||size_t(wrote)>frames*s->config.channels*4)return result(Status::CorruptFrame);
    return result(Status::Ok,used,wrote/(4*s->config.channels));
}
static const DecoderOps ldacOps={ldacOpen,ldacDecode,ldacClose};
#endif
#if GBR_ENABLE_LHDC
struct LhdcState { lhdc_decoder_t* decoder;CodecConfig config; };
static void lhdcCachesClose() {lhdc_dec_free_window();lhdc_dec_free_slot_scratch();lhdc_entropy_free();lhdc_imdct_free_480();lhdc_imdct_free_960();lhdc_imdct_free_1920();lhdc_imdct_free_cos();}
static void lhdcClose(void* p) {auto s=(LhdcState*)p;if(!s)return;lhdc_dec_deinit(s->decoder);gbr_free(s->decoder);gbr_free(s);lhdcCachesClose();}
static Status lhdcOpen(void** p,const CodecConfig& c) {
    *p=nullptr;if(c.channels!=2||!(rate(c.sample_rate)||c.sample_rate==96000||c.sample_rate==192000)||!(c.bits_per_sample==16||c.bits_per_sample==24)||(c.frame_duration_us&&c.frame_duration_us!=5000))return Status::Unsupported;
    auto s=(LhdcState*)gbr_calloc(1,sizeof(LhdcState));if(!s)return Status::OutOfMemory;s->config=c;
    s->decoder=(lhdc_decoder_t*)gbr_calloc(1,lhdc_dec_get_workspace_size(c.sample_rate,5));if(!s->decoder){lhdcClose(s);return Status::OutOfMemory;}
    lhdc_dec_config_t config={};config.sample_rate=(lhdc_dec_sample_rate_t)c.sample_rate;config.channels=c.channels;config.bit_depth=(lhdc_dec_bitdepth_t)c.bits_per_sample;config.frame_duration=LHDC_DEC_FRAME_5MS;config.max_frame_bytes=1536;
    if(!lhdc_dec_init(s->decoder,&config)||!gbr_lhdc_entropy_ready()){lhdcClose(s);return Status::OutOfMemory;}
    *p=s;return Status::Ok;
}
static DecodeResult lhdcDecode(void* p,const uint8_t* in,size_t n,int32_t* pcm,size_t cap) {
    if(!io(in,n,pcm,cap))return result(cap?Status::InvalidArgument:Status::OutputFull);
    auto s=(LhdcState*)p;if(n<2)return result(Status::NeedMoreData);
    size_t length=((in[0]|(in[1]<<8))&1023)*2+2;if(length<20||length>1538)return result(Status::CorruptFrame);if(n<length)return result(Status::NeedMoreData);
    size_t frames=s->config.sample_rate>48000?s->config.sample_rate/200:240;if(cap<frames)return result(Status::OutputFull);
    size_t used=0;uint32_t generated=0;lhdc_dec_frame_info_t info={};
    auto error=lhdc_dec_decode_frame(s->decoder,in,length,pcm,cap,&used,&generated,&info);
    if(error!=LHDC_DEC_OK)return result(error==LHDC_DEC_NEED_MORE_DATA?Status::NeedMoreData:Status::CorruptFrame);
    if(generated>cap||info.channels!=s->config.channels)return result(Status::CorruptFrame);
    if(s->config.bits_per_sample==16) {
        // Expand backwards in the same caller-owned 32-bit buffer.
        auto samples=(int16_t*)pcm;for(size_t i=generated*s->config.channels;i>0;--i)pcm[i-1]=int32_t(samples[i-1])*65536;
    }
    DecodeResult decoded = result(Status::Ok,used,generated);
    if(info.concealment_flags & LHDC_CONCEAL_RIGHT_CHANNEL) decoded.concealment_flags |= ConcealRightChannel;
    if(info.concealment_flags & LHDC_CONCEAL_FULL_FRAME) decoded.concealment_flags |= ConcealFullFrame;
    return decoded;
}
static const DecoderOps lhdcOps={lhdcOpen,lhdcDecode,lhdcClose};
#endif
#if GBR_ENABLE_OPUS
struct OpusState { OpusDecoder* decoder;int16_t* pcm;uint32_t sampleRate;uint8_t channels;size_t maxFrames; };
static void opusClose(void* p) {auto s=(OpusState*)p;if(!s)return;opus_decoder_destroy(s->decoder);gbr_free(s->pcm);gbr_free(s);}
static Status opusOpen(void** p,const CodecConfig& c) {
    *p=nullptr;if(!common(c)||!(c.sample_rate==8000||c.sample_rate==12000||c.sample_rate==16000||c.sample_rate==24000||c.sample_rate==48000))return Status::Unsupported;
    uint32_t duration=c.frame_duration_us?c.frame_duration_us:20000;
    if(!(duration==2500||duration==5000||duration==10000||duration==20000||duration==40000||duration==60000))return Status::Unsupported;
    auto s=(OpusState*)gbr_calloc(1,sizeof(OpusState));if(!s)return Status::OutOfMemory;s->sampleRate=c.sample_rate;s->channels=c.channels;s->maxFrames=(uint64_t)c.sample_rate*duration/1000000;
    int error=0;s->decoder=opus_decoder_create(c.sample_rate,c.channels,&error);s->pcm=(int16_t*)gbr_malloc(s->maxFrames*c.channels*2);
    if(!s->decoder||!s->pcm){opusClose(s);return Status::OutOfMemory;}*p=s;return Status::Ok;
}
static DecodeResult opusDecode(void* p,const uint8_t* in,size_t n,int32_t* pcm,size_t cap) {
    if(!io(in,n,pcm,cap))return result(cap?Status::InvalidArgument:Status::OutputFull);
    auto s=(OpusState*)p;if(n>1275*48)return result(Status::CorruptFrame);
    int frames=opus_packet_get_nb_samples(in,n,s->sampleRate);if(frames<0)return result(Status::CorruptFrame);
    if(size_t(frames)>s->maxFrames)return result(Status::Unsupported);if(size_t(frames)>cap)return result(Status::OutputFull);
    int got=opus_decode(s->decoder,in,n,s->pcm,s->maxFrames,0);if(got<0)return result(Status::CorruptFrame);
    expand16(pcm,s->pcm,got*s->channels);return result(Status::Ok,n,got);
}
static const DecoderOps opusOps={opusOpen,opusDecode,opusClose};
#endif
const DecoderOps* codecOperations(Codec c) {
    switch(c) {
#if GBR_ENABLE_SBC
    case Codec::Sbc:return &sbcOps;
#endif
#if GBR_ENABLE_AAC
    case Codec::Aac:return &aacOps;
#if GBR_ENABLE_AAC_HE
    case Codec::AacHe:return &aacOps;
#endif
#endif
#if GBR_ENABLE_APTX
    case Codec::Aptx:case Codec::AptxHd:return &aptxOps;
#endif
#if GBR_ENABLE_LDAC
    case Codec::Ldac:return &ldacOps;
#endif
#if GBR_ENABLE_LHDC
    case Codec::LhdcV5:return &lhdcOps;
#endif
#if GBR_ENABLE_OPUS
    case Codec::Opus:return &opusOps;
#endif
    default:return nullptr;
    }
}
uint32_t compiledCodecs() { uint32_t mask=0;for(unsigned i=0;i<(unsigned)Codec::Count;++i)if(codecOperations((Codec)i))mask|=codecBit((Codec)i);return mask; }
}}

