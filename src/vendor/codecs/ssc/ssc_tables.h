/* SSC mode tables (from Samsung's SSC encoder). */
#ifndef SSC_TABLES_H
#define SSC_TABLES_H
extern const unsigned char SSC_LOG2_FRAC_TABLE[24];
extern const short ssc_eBands_44k[23];
extern const unsigned char ssc_allocVectorsA_44k[242];
extern const unsigned char ssc_allocVectorsB_44k[242];
extern const short ssc_logN_44k[22];
extern const short ssc_cache_index_44k[110];
extern const unsigned char ssc_cache_bits_44k[346];
extern const int ssc_cap_44k[22];
extern const short ssc_eBands_96k[23];
extern const unsigned char ssc_allocVectorsA_96k[242];
extern const unsigned char ssc_allocVectorsB_96k[242];
extern const short ssc_logN_96k[22];
extern const short ssc_cache_index_96k[110];
extern const unsigned char ssc_cache_bits_96k[334];
extern const int ssc_cap_96k[22];
#endif
