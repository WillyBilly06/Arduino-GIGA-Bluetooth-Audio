// Align decoded PCM against a reference (source or reference decoder) by
// cross-correlation over a +/-8192 frame window, then report per-channel
// signal-to-error ratio, gain, max abs difference and the bit-exact fraction.
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <vector>
typedef std::vector<int32_t> Pcm;
static Pcm load(const char* p) { Pcm v; FILE* f = fopen(p, "rb"); if (!f) { perror(p); exit(2); } int32_t x; while (fread(&x, 4, 1, f) == 1) v.push_back(x); fclose(f); return v; }
int main(int argc, char** argv) {
    if (argc != 4 && argc != 5) return 1;
    Pcm a = load(argv[1]), b = load(argv[2]); const int skip = atoi(argv[3]); // skip leading frames (decoder start-up)
    long na = long(a.size() / 2), nb = long(b.size() / 2);
    // Coarse lag search on left channel with a 1-second probe from the middle.
    long probe = nb / 2, win = 48000; if (win > nb / 3) win = nb / 3;
    long best = 0; double bestv = -1e300;
    if (argc == 5) best = atol(argv[4]); else
    for (long lag = -8192; lag <= 8192; ++lag) { // b[i] ~ a[i - lag]
        double s = 0; for (long i = probe; i < probe + win; i += 3) { long j = i - lag; if (j < 0 || j >= na) { s = -1e300; break; } s += double(b[2 * i]) * a[2 * j]; }
        if (s > bestv) { bestv = s; best = lag; }
    }
    double sig[2] = {0, 0}, err[2] = {0, 0}, ab[2] = {0, 0}, aa[2] = {0, 0}; long n = 0, exact = 0; long long maxd = 0;
    for (long i = skip; i < nb; ++i) {
        long j = i - best; if (j < 0 || j >= na) continue; ++n;
        for (int c = 0; c < 2; ++c) {
            double x = a[2 * j + c], y = b[2 * i + c];
            sig[c] += x * x; err[c] += (y - x) * (y - x); ab[c] += x * y; aa[c] += x * x;
            long long d = llabs((long long)b[2 * i + c] - a[2 * j + c]); if (d > maxd) maxd = d; if (!d) ++exact;
        }
    }
    printf("lag=%ld compared_frames=%ld", best, n);
    for (int c = 0; c < 2; ++c) printf(" ch%d_ser_db=%.2f ch%d_gain_db=%.4f", c, err[c] > 0 ? 10 * log10(sig[c] / err[c]) : 999.0, c, 20 * log10(ab[c] / aa[c]));
    printf(" max_abs_diff_lsb24=%.1f bit_exact_pct=%.3f\n", maxd / 256.0, n ? 100.0 * exact / (2.0 * n) : 0);
    return 0;
}
