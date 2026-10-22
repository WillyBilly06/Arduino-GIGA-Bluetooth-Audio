#include "vendor/codecs/opus/silk/CNG.c"
#include "vendor/codecs/opus/silk/API.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

static uint32_t cng_random;
static uint32_t next_random(void){cng_random=cng_random*1664525u+1013904223u;return cng_random;}
static uint64_t hash_value;
static void hash_byte(unsigned value){hash_value=(hash_value^(uint8_t)value)*UINT64_C(1099511628211);}
static void hash16(const opus_int16* p,size_t n){for(size_t i=0;i<n;++i){uint16_t v=(uint16_t)p[i];hash_byte(v);hash_byte(v>>8);}}
static void hash32(const opus_int32* p,size_t n){for(size_t i=0;i<n;++i){uint32_t v=(uint32_t)p[i];for(unsigned s=0;s<32;s+=8)hash_byte(v>>s);}}
_Static_assert(sizeof(((silk_CNG_struct*)0)->CNG_exc_buf_Q14)/sizeof(opus_int32)==256,"CNG storage must retain only the reachable256 entries");
int cng_capacity(void){return sizeof(((silk_CNG_struct*)0)->CNG_exc_buf_Q14)/sizeof(opus_int32);}
int cng_state_size(void){return sizeof(silk_decoder_state);}
int cng_super_size(void){int n=0;if(silk_Get_Decoder_Size(&n))abort();return n;}
int run_opus_cng_storage(void){
    hash_value=UINT64_C(14695981039346656037);
    unsigned calls=0;
    for(unsigned trial=0;trial<128;++trial){
        silk_decoder_state* st=(silk_decoder_state*)malloc(sizeof(*st));
        if(!st)abort();
        cng_random=0x6f174229u+trial*193u;
        if(silk_init_decoder(st))abort();
        for(unsigned step=0;step<96;++step){
            if(step && step%19==0)if(silk_reset_decoder(st))abort();
            int fs=(int[]){8,12,16}[(step/4+trial)%3];
            int ms=(step+trial)%2?10:20;
            st->nb_subfr=ms/5;
            if(silk_decoder_set_fs(st,fs,(int[]){8000,12000,16000,24000,48000}[(step+trial)%5]))abort();
            for(int i=0;i<MAX_LPC_ORDER;++i)st->prevNLSF_Q15[i]=(opus_int16)((i+1)*32767/(st->LPC_order+1));
            for(int i=0;i<MAX_FRAME_LENGTH;++i)st->exc_Q14[i]=(opus_int32)((int)(next_random()%2049)-1024)*16384;
            if(step%17==0){
                for(int i=0;i<cng_capacity();++i)st->sCNG.CNG_exc_buf_Q14[i]=(opus_int32)((int)(next_random()%1025)-512)*16384;
                /* Keep following RNG choices independent of the omitted tail. */
                cng_random=0x2adf5731u+trial*193u+step*337u;
            }
            st->prevSignalType=step%3==0?TYPE_UNVOICED:TYPE_NO_VOICE_ACTIVITY;
            st->lossCnt=step%8>=5?(int)(step%8-4):0;
            st->sPLC.randScale_Q14=8192;
            st->sPLC.prevGain_Q16[1]=65536;
            silk_decoder_control control={0};
            for(int i=0;i<MAX_NB_SUBFR;++i)control.Gains_Q16[i]=65536;
            control.Gains_Q16[(step/2)%st->nb_subfr]=131072;
            opus_int16 frame[MAX_FRAME_LENGTH];
            for(int i=0;i<MAX_FRAME_LENGTH;++i)frame[i]=(opus_int16)((int)(next_random()%1001)-500);
            silk_CNG(st,&control,frame,st->frame_length);
            {opus_int32 value=fs;hash32(&value,1);value=ms;hash32(&value,1);}
            hash32(st->sCNG.CNG_exc_buf_Q14,256);
            hash16(st->sCNG.CNG_smth_NLSF_Q15,MAX_LPC_ORDER);
            hash32(st->sCNG.CNG_synth_state,MAX_LPC_ORDER);
            hash32(&st->sCNG.CNG_smth_Gain_Q16,1);
            hash32(&st->sCNG.rand_seed,1);{opus_int32 value=st->sCNG.fs_kHz;hash32(&value,1);}
            hash16(frame,st->frame_length);
            opus_int32 excitation[MAX_FRAME_LENGTH],seed=(opus_int32)next_random();
            silk_CNG_exc(excitation,st->sCNG.CNG_exc_buf_Q14,st->frame_length,&seed);
            hash32(excitation,st->frame_length);hash32(&seed,1);
            ++calls;
        }
        free(st);
    }
    if(calls!=12288 || hash_value!=UINT64_C(10986655626203342602)){fprintf(stderr,"CNG state/output digest mismatch calls=%u hash=%016llx\n",calls,(unsigned long long)hash_value);abort();}
    printf("PASS actual CNG256 transitions=%u digest=%016llx\n",calls,(unsigned long long)hash_value);
    return calls;
}
