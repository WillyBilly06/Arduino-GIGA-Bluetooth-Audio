#ifndef SPEAKER_BTH_SMP_CRYPTO_H
#define SPEAKER_BTH_SMP_CRYPTO_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#if defined(_WIN32) && defined(BTH_SMP_CRYPTO_EXPORT)
#define BTH_SMP_API __declspec(dllexport)
#else
#define BTH_SMP_API
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef bool (*bth_smp_aes_fn)(void *context,const uint8_t key[16],const uint8_t input[16],uint8_t output[16]);
typedef bool (*bth_smp_cmac_fn)(void *context,const uint8_t key[16],const uint8_t *input,size_t length,uint8_t output[16]);
typedef struct {void *context;bth_smp_aes_fn aes;bth_smp_cmac_fn cmac;} bth_smp_crypto_t;
// Core6.1 Vol3H2.2 composition only; existing platform code supplies AES/CMAC.
// ALL integers here are MSB FIRST, exactly as Core AppendixD. Convert SMP/HCI
// little-endian octet strings at the protocol boundary, including preq/pres.
// A1/A2 are address type followed by the six MSB-first address octets; IOcap is
// AuthReq,OOB,IO. Inputs/outputs must not overlap. f5 outputs must be distinct.
// Bad parameters leave outputs untouched. Backend failure clears valid outputs.
BTH_SMP_API void bth_smp_secret_clear(void *data,size_t length);
BTH_SMP_API bool bth_smp_secure_equal(const void *a,const void *b,size_t length);
BTH_SMP_API bool bth_smp_ah(const bth_smp_crypto_t *c,const uint8_t irk[16],const uint8_t random[3],uint8_t out[3]);
BTH_SMP_API bool bth_smp_c1(const bth_smp_crypto_t *c,const uint8_t key[16],const uint8_t random[16],const uint8_t preq[7],const uint8_t pres[7],uint8_t iat,uint8_t rat,const uint8_t ia[6],const uint8_t ra[6],uint8_t out[16]);
BTH_SMP_API bool bth_smp_s1(const bth_smp_crypto_t *c,const uint8_t key[16],const uint8_t r1[16],const uint8_t r2[16],uint8_t out[16]);
BTH_SMP_API bool bth_smp_f4(const bth_smp_crypto_t *c,const uint8_t u[32],const uint8_t v[32],const uint8_t x[16],uint8_t z,uint8_t out[16]);
BTH_SMP_API bool bth_smp_f5(const bth_smp_crypto_t *c,const uint8_t w[32],const uint8_t n1[16],const uint8_t n2[16],const uint8_t a1[7],const uint8_t a2[7],uint8_t mackey[16],uint8_t ltk[16]);
BTH_SMP_API bool bth_smp_f6(const bth_smp_crypto_t *c,const uint8_t w[16],const uint8_t n1[16],const uint8_t n2[16],const uint8_t r[16],const uint8_t iocap[3],const uint8_t a1[7],const uint8_t a2[7],uint8_t out[16]);
// Returns the full32-bit g2 value. Numeric Comparison displays value%1000000.
BTH_SMP_API bool bth_smp_g2(const bth_smp_crypto_t *c,const uint8_t u[32],const uint8_t v[32],const uint8_t x[16],const uint8_t y[16],uint32_t *out);
BTH_SMP_API bool bth_smp_h6(const bth_smp_crypto_t *c,const uint8_t w[16],const uint8_t key_id[4],uint8_t out[16]);
BTH_SMP_API bool bth_smp_h7(const bth_smp_crypto_t *c,const uint8_t salt[16],const uint8_t w[16],uint8_t out[16]);
#ifdef __cplusplus
}
#endif
#endif
