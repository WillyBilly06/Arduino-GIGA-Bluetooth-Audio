/* Exercise the actual private window descriptor and overlap implementation. */
#include "../src/vendor/codecs/lhdc/lhdc_dec.c"

int lhdc_test_window_descriptor(int n)
{
    lhdc_dec_window_prepare(n);
    const int h=n/2, expected_z=(h-n/5)/2;
    if (!s_win.ramp || s_win.n!=n || s_win.z!=expected_z || s_win.t!=n/5)
        return 1;
    float mdct[1920], tail_a[960], tail_b[960], pcm_a[960], pcm_b[960];
    for(int i=0;i<n;i++) mdct[i]=(float)((i*1237)%32749-16374)*0.03125f;
    for(int i=0;i<h;i++) tail_a[i]=tail_b[i]=(float)((i*711)%17389-8694)*0.015625f;
    lhdc_decoder_t dec={0};
    dec.slot[0].mdct_out=mdct;
    dec.overlap_buf[0]=tail_a;
    lhdc_dec_overlap_add(&dec,pcm_a,0,n);
    const float *saved=s_win.ramp;
    s_win.ramp=NULL;
    dec.overlap_buf[0]=tail_b;
    lhdc_dec_overlap_add(&dec,pcm_b,0,n);
    s_win.ramp=saved;
    int result=0;
    for(int i=0;i<h;i++) {
        /* The fast path zero-fills discarded tails. +/-0 are the same PCM
         * value; every other float must compare exactly, without tolerance. */
        if(pcm_a[i]!=pcm_b[i] || tail_a[i]!=tail_b[i]) {
            fprintf(stderr,"overlap n=%d i=%d pcm=%a/%a tail=%a/%a\n",
                    n,i,pcm_a[i],pcm_b[i],tail_a[i],tail_b[i]);
            result=1; break;
        }
    }
    lhdc_dec_free_window();
    return result ? 2 : 0;
}
