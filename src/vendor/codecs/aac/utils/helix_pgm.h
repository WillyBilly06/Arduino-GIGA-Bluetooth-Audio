/* GigaBredr modification notice, 2026-10-21: portable decoder port;
 * build guards, relative includes, tracked allocation and bounds/ARM fixes.
 * Original copyright and license notices remain below unchanged.
 * Per-file original/port SHA-256: docs/source-provenance.json. */
/* helix_pgm.h - ESP32 IDF version (no PROGMEM needed) */

#ifndef HELIX_PGM_H
#define HELIX_PGM_H

/* ESP32 has no PROGMEM - all memory is directly accessible */
#define PROGMEM
#define pgm_read_byte(addr) (*(const unsigned char *)(addr))
#define pgm_read_word(addr) (*(const unsigned short *)(addr))

#endif /* HELIX_PGM_H */
