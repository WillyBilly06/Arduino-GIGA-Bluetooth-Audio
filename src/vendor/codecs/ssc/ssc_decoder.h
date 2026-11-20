/* SSC (Samsung Scalable Codec) decoder: public API.
 *
 * One SSC frame = 4-byte header + range-coded payload, decoding to 864 stereo samples.
 * The output lags the encoder input by 108 samples (MDCT overlap).
 *
 * Memory: all tables are const (they stay in flash on MCUs). The working state (~23 KB, see
 * ssc_decoder_size) is only allocated by ssc_decoder_create*, as 3 blocks of at most ~8 KB, and freed by
 * ssc_decoder_destroy. The library has no static RAM buffers.
 */
#ifndef SSC_DECODER_H
#define SSC_DECODER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SSC_FRAME_SAMPLES 864
/* Size of the buffer returned by ssc_output_buffer: one whole frame of int16 stereo, half a frame of int32. */
#define SSC_OUTPUT_BYTES 3456

typedef struct ssc_decoder ssc_decoder;

typedef struct {
    int bitrate;   /* bits per second, from the header's rate code */
    int hq;        /* "high quality" rate class (E-family, 256k, 328k) */
    int seq;       /* frame sequence counter (4 or 8 bits) */
    int end_band;  /* number of coded bands (18..22) */
} ssc_frame_info;

/* Memory hooks. */
typedef void *(*ssc_alloc_fn)(void *ctx, unsigned size);
typedef void (*ssc_free_fn)(void *ctx, void *ptr);

/* sample_rate: 44100 or 48000 (same codec mode) or 96000. Returns NULL on bad rate / no memory. */
ssc_decoder *ssc_decoder_create(int sample_rate);
ssc_decoder *ssc_decoder_create_with_allocator(int sample_rate, ssc_alloc_fn alloc, ssc_free_fn release,
                                               void *ctx);
void ssc_decoder_destroy(ssc_decoder *dec);
/* Total bytes the decoder allocates. */
int ssc_decoder_size(void);
/* Clear all history (e.g. after a stream gap). */
void ssc_decoder_reset(ssc_decoder *dec);

/* Parse only the 4-byte header. Returns 0 if valid. */
int ssc_parse_frame_info(const uint8_t *frame, int len, ssc_frame_info *info);

/* Decode one frame into 864 interleaved stereo int16 samples (pcm must hold 1728 values).
 * Returns 864 on success, negative on error (pcm is then filled with silence). */
int ssc_decode(ssc_decoder *dec, const uint8_t *frame, int len, int16_t *pcm);

/* Same, float output in [-1, 1). */
int ssc_decode_float(ssc_decoder *dec, const uint8_t *frame, int len, float *pcm);

/* Same, int32 output with 16-bit full scale at 1<<31 (keeps the extra precision of 24-bit streams). */
int ssc_decode_s32(ssc_decoder *dec, const uint8_t *frame, int len, int32_t *pcm);

/* Decode without converting: the frame stays inside the decoder, and the caller converts it in pieces
 * of any size with ssc_read_*, so no frame-sized PCM buffer is needed. Returns 864 on success, negative
 * on error (the frame then reads as silence). ssc_decode* = ssc_decode_frame + ssc_read_* of the frame. */
int ssc_decode_frame(ssc_decoder *dec, const uint8_t *frame, int len);

/* Convert samples [offset, offset + count) of the last decoded frame to interleaved stereo (pcm holds
 * 2*count values), in the same format as the matching ssc_decode*. Valid until the next decode or reset;
 * any order and any number of reads. Returns count, or -1 if the range is not within 0..864. */
int ssc_read_s16(ssc_decoder *dec, int offset, int count, int16_t *pcm);
int ssc_read_s32(ssc_decoder *dec, int offset, int count, int32_t *pcm);
int ssc_read_float(ssc_decoder *dec, int offset, int count, float *pcm);

/* SSC_OUTPUT_BYTES (8-byte aligned) of decoder memory that is only used while a frame is being decoded,
 * lent to the caller as a destination for ssc_read_*. Its content is destroyed by the next ssc_decode*
 * call (not by ssc_read_* or ssc_decoder_reset). Lets a caller emit the PCM without a buffer of its own. */
void *ssc_output_buffer(ssc_decoder *dec);

/* Total size in bytes (header included) of the frame starting at `frame`, derived from its header
 * (the wire format has no length field). sample_rate selects the codec mode: 44100/48000 or 96000.
 * Returns 0 if the header is invalid. Needed when one packet carries several frames (96 kHz). */
int ssc_frame_bytes(const uint8_t *frame, int avail, int sample_rate);

#ifdef __cplusplus
}
#endif
#endif
