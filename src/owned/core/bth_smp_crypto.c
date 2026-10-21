#include "bth_smp_crypto.h"
#include <string.h>
void bth_smp_secret_clear(void *data,size_t length) {
    volatile uint8_t *p=(volatile uint8_t *)data;
    if(p)while(length--)*p++=0;
}
bool bth_smp_secure_equal(const void *a,const void *b,size_t n) {
    if(n && (!a || !b))return false;
    const uint8_t *x=(const uint8_t *)a,*y=(const uint8_t *)b;
    volatile uint8_t difference=0;
    for(size_t i=0;i<n;++i)difference=(uint8_t)(difference|(x[i]^y[i]));
    return difference==0;
}
static bool mac(const bth_smp_crypto_t *c,const uint8_t *key,const uint8_t *input,size_t n,uint8_t *out) {
    const bool ok=c->cmac(c->context,key,input,n,out);
    if(!ok)bth_smp_secret_clear(out,16);
    return ok;
}
bool bth_smp_ah(const bth_smp_crypto_t *c,const uint8_t irk[16],const uint8_t random[3],uint8_t out[3]) {
    if(!c || !c->aes || !irk || !random || !out)return false;
    uint8_t block[16]={0},result[16];memcpy(block+13,random,3);
    const bool ok=c->aes(c->context,irk,block,result);
    if(ok)memcpy(out,result+13,3);else bth_smp_secret_clear(out,3);
    bth_smp_secret_clear(block,sizeof block);bth_smp_secret_clear(result,sizeof result);return ok;
}
bool bth_smp_c1(const bth_smp_crypto_t *c,const uint8_t key[16],const uint8_t random[16],const uint8_t preq[7],const uint8_t pres[7],uint8_t iat,uint8_t rat,const uint8_t ia[6],const uint8_t ra[6],uint8_t out[16]) {
    if(!c || !c->aes || !key || !random || !preq || !pres || !ia || !ra || !out || iat>1 || rat>1)return false;
    uint8_t p1[16],result[16];memcpy(p1,pres,7);memcpy(p1+7,preq,7);p1[14]=rat;p1[15]=iat;
    for(unsigned i=0;i<16;++i)p1[i]^=random[i];
    bool ok=c->aes(c->context,key,p1,result);
    if(ok) {
        memset(p1,0,4);memcpy(p1+4,ia,6);memcpy(p1+10,ra,6);
        for(unsigned i=0;i<16;++i)result[i]^=p1[i];
        ok=c->aes(c->context,key,result,out);
    }
    if(!ok)bth_smp_secret_clear(out,16);
    bth_smp_secret_clear(p1,sizeof p1);bth_smp_secret_clear(result,sizeof result);return ok;
}
bool bth_smp_s1(const bth_smp_crypto_t *c,const uint8_t key[16],const uint8_t r1[16],const uint8_t r2[16],uint8_t out[16]) {
    if(!c || !c->aes || !key || !r1 || !r2 || !out)return false;
    uint8_t block[16];memcpy(block,r1+8,8);memcpy(block+8,r2+8,8);
    const bool ok=c->aes(c->context,key,block,out);
    if(!ok)bth_smp_secret_clear(out,16);
    bth_smp_secret_clear(block,sizeof block);return ok;
}
bool bth_smp_f4(const bth_smp_crypto_t *c,const uint8_t u[32],const uint8_t v[32],const uint8_t x[16],uint8_t z,uint8_t out[16]) {
    if(!c || !c->cmac || !u || !v || !x || !out)return false;
    uint8_t message[65];memcpy(message,u,32);memcpy(message+32,v,32);message[64]=z;
    const bool ok=mac(c,x,message,sizeof message,out);bth_smp_secret_clear(message,sizeof message);return ok;
}
bool bth_smp_f5(const bth_smp_crypto_t *c,const uint8_t w[32],const uint8_t n1[16],const uint8_t n2[16],const uint8_t a1[7],const uint8_t a2[7],uint8_t mackey[16],uint8_t ltk[16]) {
    if(!c || !c->cmac || !w || !n1 || !n2 || !a1 || !a2 || !mackey || !ltk)return false;
    const uintptr_t a=(uintptr_t)mackey,b=(uintptr_t)ltk;
    if(a<=b?b-a<16:a-b<16)return false;
    static const uint8_t salt[16]={0x6c,0x88,0x83,0x91,0xaa,0xf5,0xa5,0x38,0x60,0x37,0x0b,0xdb,0x5a,0x60,0x83,0xbe};
    uint8_t t[16],message[53]={0,'b','t','l','e'};
    memcpy(message+5,n1,16);memcpy(message+21,n2,16);memcpy(message+37,a1,7);memcpy(message+44,a2,7);message[51]=1;
    bool ok=mac(c,salt,w,32,t);
    if(ok)ok=mac(c,t,message,sizeof message,mackey);
    message[0]=1;if(ok)ok=mac(c,t,message,sizeof message,ltk);
    if(!ok){bth_smp_secret_clear(mackey,16);bth_smp_secret_clear(ltk,16);}
    bth_smp_secret_clear(t,sizeof t);bth_smp_secret_clear(message,sizeof message);return ok;
}
bool bth_smp_f6(const bth_smp_crypto_t *c,const uint8_t w[16],const uint8_t n1[16],const uint8_t n2[16],const uint8_t r[16],const uint8_t iocap[3],const uint8_t a1[7],const uint8_t a2[7],uint8_t out[16]) {
    if(!c || !c->cmac || !w || !n1 || !n2 || !r || !iocap || !a1 || !a2 || !out)return false;
    uint8_t message[65];memcpy(message,n1,16);memcpy(message+16,n2,16);memcpy(message+32,r,16);
    memcpy(message+48,iocap,3);memcpy(message+51,a1,7);memcpy(message+58,a2,7);
    const bool ok=mac(c,w,message,sizeof message,out);bth_smp_secret_clear(message,sizeof message);return ok;
}
bool bth_smp_g2(const bth_smp_crypto_t *c,const uint8_t u[32],const uint8_t v[32],const uint8_t x[16],const uint8_t y[16],uint32_t *out) {
    if(!c || !c->cmac || !u || !v || !x || !y || !out)return false;
    uint8_t message[80],result[16];memcpy(message,u,32);memcpy(message+32,v,32);memcpy(message+64,y,16);
    const bool ok=mac(c,x,message,sizeof message,result);
    *out=ok?((uint32_t)result[12]<<24)|((uint32_t)result[13]<<16)|((uint32_t)result[14]<<8)|result[15]:0;
    bth_smp_secret_clear(message,sizeof message);bth_smp_secret_clear(result,sizeof result);return ok;
}
bool bth_smp_h6(const bth_smp_crypto_t *c,const uint8_t w[16],const uint8_t key_id[4],uint8_t out[16]) {
    if(!c || !c->cmac || !w || !key_id || !out)return false;
    return mac(c,w,key_id,4,out);
}
bool bth_smp_h7(const bth_smp_crypto_t *c,const uint8_t salt[16],const uint8_t w[16],uint8_t out[16]) {
    if(!c || !c->cmac || !salt || !w || !out)return false;
    return mac(c,salt,w,16,out);
}
