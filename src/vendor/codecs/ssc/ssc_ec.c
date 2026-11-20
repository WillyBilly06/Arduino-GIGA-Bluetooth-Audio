/* GigaBredr modification notice, 2026-11-20: build guard only (GBR_ENABLE_SSC).
 * Original file body below is unchanged; see LICENSE and COPYING.opus in this folder.
 * Upstream/export SHA-256: manifest.json in this folder. */
#include "../../../CodecBuildConfig.h"
#if GBR_ENABLE_SSC
/* Part of the SSC decoder. Derived from libopus 1.1 (BSD license, see COPYING.opus) and from
 * analysis of Samsung's SSC encoder; see docs/notes for the format notes. */
#include "ssc_ec.h"

/* ---- range coder constants (mfrngcod.h / entcode.h) ---- */
#define EC_SYM_BITS    (8)
#define EC_CODE_BITS   (24)                     /* SSC: 24 (opus: 32) */
#define EC_SYM_MAX     ((1U<<EC_SYM_BITS)-1)    /* 0xFF */
#define EC_CODE_SHIFT  (EC_CODE_BITS-EC_SYM_BITS-1)          /* 15 */
#define EC_CODE_TOP    (((opus_uint32)1U)<<(EC_CODE_BITS-1)) /* 0x800000 */
#define EC_CODE_BOT    (EC_CODE_TOP>>EC_SYM_BITS)            /* 0x8000 */
#define EC_CODE_EXTRA  ((EC_CODE_BITS-2)%EC_SYM_BITS+1)      /* 7 */
#define EC_WINDOW_SIZE ((int)sizeof(ec_window)*8)            /* 32 */
#define EC_UINT_BITS   (8)
#define BITRES         (3)

#define EC_ILOG(x) ssc_ilog32(x)
#define EC_MINI(a,b) ((a)<(b)?(a):(b))
/* ec_tell and ec_tell_frac are inline in ssc_ec.h. */

/* ---- entdec.c (opus 1.1, unchanged except for the EC_CODE_BITS value above) ---- */
static int ec_read_byte(ec_dec *_this) {
    return _this->offs < _this->storage ? _this->buf[_this->offs++] : 0;
}
static int ec_read_byte_from_end(ec_dec *_this) {
    return _this->end_offs < _this->storage ? _this->buf[_this->storage - ++(_this->end_offs)] : 0;
}
static void ec_dec_normalize(ec_dec *_this) {
    while (_this->rng <= EC_CODE_BOT) {
        int sym;
        _this->nbits_total += EC_SYM_BITS;
        _this->rng <<= EC_SYM_BITS;
        sym = _this->rem;
        _this->rem = ec_read_byte(_this);
        sym = (sym << EC_SYM_BITS | _this->rem) >> (EC_SYM_BITS - EC_CODE_EXTRA);
        _this->val = ((_this->val << EC_SYM_BITS) + (EC_SYM_MAX & ~sym)) & (EC_CODE_TOP - 1);
    }
}
/* _buf = frame + 4 (after the 4-byte SSC header), _storage = nbBytes (the full payload size; raw bits
   are read from its end). */
void ec_dec_init(ec_dec *_this, unsigned char *_buf, opus_uint32 _storage) {
    _this->buf = _buf;
    _this->storage = _storage;
    _this->end_offs = 0;
    _this->end_window = 0;
    _this->nend_bits = 0;
    _this->nbits_total = EC_CODE_BITS + 1 - ((EC_CODE_BITS - EC_CODE_EXTRA) / EC_SYM_BITS) * EC_SYM_BITS; /* 9 */
    _this->offs = 0;
    _this->rng = 1U << EC_CODE_EXTRA;
    _this->rem = ec_read_byte(_this);
    _this->val = _this->rng - 1 - (_this->rem >> (EC_SYM_BITS - EC_CODE_EXTRA));
    _this->error = 0;
    ec_dec_normalize(_this);   /* -> rng = 2^23, nbits_total = 25, ec_tell() = 1 */
}
unsigned ec_decode(ec_dec *_this, unsigned _ft) {
    unsigned s;
    _this->ext = _this->rng / _ft;
    s = (unsigned)(_this->val / _this->ext);
    return _ft - EC_MINI(s + 1, _ft);
}
unsigned ec_decode_bin(ec_dec *_this, unsigned _bits) {
    unsigned s;
    _this->ext = _this->rng >> _bits;
    s = (unsigned)(_this->val / _this->ext);
    return (1U << _bits) - EC_MINI(s + 1U, 1U << _bits);
}
void ec_dec_update(ec_dec *_this, unsigned _fl, unsigned _fh, unsigned _ft) {
    opus_uint32 s;
    s = _this->ext * (_ft - _fh);
    _this->val -= s;
    _this->rng = _fl > 0 ? _this->ext * (_fh - _fl) : _this->rng - s;
    ec_dec_normalize(_this);
}
int ec_dec_bit_logp(ec_dec *_this, unsigned _logp) {
    opus_uint32 r, d, s;
    int ret;
    r = _this->rng;
    d = _this->val;
    s = r >> _logp;
    ret = d < s;
    if (!ret) _this->val = d - s;
    _this->rng = ret ? s : r - s;
    ec_dec_normalize(_this);
    return ret;
}
int ec_dec_icdf(ec_dec *_this, const unsigned char *_icdf, unsigned _ftb) {
    opus_uint32 r, d, s, t;
    int ret;
    s = _this->rng;
    d = _this->val;
    r = s >> _ftb;
    ret = -1;
    do {
        t = s;
        s = r * _icdf[++ret];
    } while (d < s);
    _this->val = d - s;
    _this->rng = t - s;
    ec_dec_normalize(_this);
    return ret;
}
opus_uint32 ec_dec_bits(ec_dec *_this, unsigned _bits) {
    ec_window window;
    int available;
    opus_uint32 ret;
    window = _this->end_window;
    available = _this->nend_bits;
    if ((unsigned)available < _bits) {
        do {
            window |= (ec_window)ec_read_byte_from_end(_this) << available;
            available += EC_SYM_BITS;
        } while (available <= EC_WINDOW_SIZE - EC_SYM_BITS);
    }
    ret = (opus_uint32)window & (((opus_uint32)1 << _bits) - 1U);
    window >>= _bits;
    available -= _bits;
    _this->end_window = window;
    _this->nend_bits = available;
    _this->nbits_total += _bits;
    return ret;
}
opus_uint32 ec_dec_uint(ec_dec *_this, opus_uint32 _ft) {
    unsigned ft, s;
    int ftb;
    _ft--;
    ftb = EC_ILOG(_ft);
    if (ftb > EC_UINT_BITS) {
        opus_uint32 t;
        ftb -= EC_UINT_BITS;
        ft = (unsigned)(_ft >> ftb) + 1;
        s = ec_decode(_this, ft);
        ec_dec_update(_this, s, s + 1, ft);
        t = (opus_uint32)s << ftb | ec_dec_bits(_this, ftb);
        if (t <= _ft) return t;
        _this->error = 1;
        return _ft;
    } else {
        _ft++;
        s = ec_decode(_this, (unsigned)_ft);
        ec_dec_update(_this, s, s + 1, (unsigned)_ft);
        return s;
    }
}

/* ---- laplace.c (opus 1.1, unchanged: LAPLACE_MINP=1, LAPLACE_NMIN=16) ---- */
#define LAPLACE_LOG_MINP (0)
#define LAPLACE_MINP (1<<LAPLACE_LOG_MINP)
#define LAPLACE_NMIN (16)

static unsigned ec_laplace_get_freq1(unsigned fs0, int decay) {
    unsigned ft = 32768 - LAPLACE_MINP * (2 * LAPLACE_NMIN) - fs0;   /* 0x7FE0 - fs0 */
    return ft * (opus_int32)(16384 - decay) >> 15;
}

int ec_laplace_decode(ec_dec *dec, unsigned fs, int decay) {
    int val = 0;
    unsigned fl, fm;
    fm = ec_decode_bin(dec, 15);
    fl = 0;
    if (fm >= fs) {
        val++;
        fl = fs;
        fs = ec_laplace_get_freq1(fs, decay) + LAPLACE_MINP;
        while (fs > LAPLACE_MINP && fm >= fl + 2 * fs) {
            fs *= 2;
            fl += fs;
            fs = ((fs - 2 * LAPLACE_MINP) * (opus_int32)decay) >> 15;
            fs += LAPLACE_MINP;
            val++;
        }
        if (fs <= LAPLACE_MINP) {
            int di = (fm - fl) >> (LAPLACE_LOG_MINP + 1);
            val += di;
            fl += 2 * di * LAPLACE_MINP;
        }
        if (fm < fl + fs)
            val = -val;
        else
            fl += fs;
    }
    ec_dec_update(dec, fl, EC_MINI(fl + fs, 32768), 32768);
    return val;
}
#endif
