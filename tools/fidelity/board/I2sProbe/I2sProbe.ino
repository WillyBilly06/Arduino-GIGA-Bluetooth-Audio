// On-chip I2S pin probe. Runs the library's I2S output on the default pins
// (D70/PI1 BCLK, D72/PI3 DIN, D74/PB12 LRCK) with a known, frame-numbered
// pattern, and samples the GPIO input data registers (which follow the pad
// level in alternate-function mode) in a tight loop with interrupts masked.
// Capture windows stay below one DMA half (~1 ms), so the DMA always has
// fresh data. The host decodes the raw samples.
// Protocol after the host sends 'g': 'K' + u32 len + {u32 rate, u32 samples,
// u32 cycles, u32 first_frame_written, u32 last_frame_written} + samples
// (one byte each: bit0 BCLK, bit1 DIN, bit2 LRCK); 'S' + u32 len + text; 'Z'.
#include <GigaBredr.h>
#include "platform/GigaI2s.h"
using namespace giga_bredr;
using namespace giga_bredr::detail;
static uint8_t cap[60000];
static uint32_t frameNo = 0;
static inline int32_t left(uint32_t n) { return int32_t(((n ^ 0xA53C96u) & 0xFFFFFFu) << 8); }
static inline int32_t right(uint32_t n) { return int32_t((((n * 0x1F3u) + 0x3C5Au) & 0xFFFFFFu) << 8); }
static void block(char tag, const void* d, uint32_t n) {
  Serial.write(uint8_t(tag)); Serial.write(reinterpret_cast<const uint8_t*>(&n), 4);
  if (n) Serial.write(reinterpret_cast<const uint8_t*>(d), n);
}
static void text(const char* s) { block('S', s, strlen(s)); }
static void fill() {
  int32_t buf[64 * 2];
  for (;;) {
    for (int i = 0; i < 64; ++i) { buf[2 * i] = left(frameNo + i); buf[2 * i + 1] = right(frameNo + i); }
    size_t n = i2sWrite(buf, 64); frameNo += n;
    if (n < 64) return;
  }
}
__attribute__((noinline)) static uint32_t sample(uint32_t n) {
  volatile uint32_t* pi = &GPIOI->IDR; volatile uint32_t* pb = &GPIOB->IDR;
  const uint32_t t0 = DWT->CYCCNT;
  for (uint32_t i = 0; i < n; ++i) {
    const uint32_t a = *pi, b = *pb;
    cap[i] = uint8_t(((a >> 1) & 1u) | ((a >> 2) & 2u) | ((b >> 10) & 4u));
  }
  return DWT->CYCCNT - t0;
}
// GPIOI only (BCLK bit0, DIN bit1): about twice the sample rate, no LRCK.
__attribute__((noinline)) static uint32_t sampleI(uint32_t n) {
  volatile uint32_t* pi = &GPIOI->IDR;
  const uint32_t t0 = DWT->CYCCNT;
  for (uint32_t i = 0; i < n; ++i) { const uint32_t a = *pi; cap[i] = uint8_t(((a >> 1) & 1u) | ((a >> 2) & 2u)); }
  return DWT->CYCCNT - t0;
}
// MDMA block copy of GPIOI->IDR into AXI SRAM, software triggered, no CPU in the loop.
alignas(32) static uint32_t mcap[16384];
static uint32_t sampleMdma(uint32_t words) {
  RCC->AHB3ENR |= RCC_AHB3ENR_MDMAEN; (void)RCC->AHB3ENR;
  MDMA_Channel_TypeDef* ch = MDMA_Channel0;
  ch->CCR = 0; ch->CIFCR = 0x1F;
  ch->CTCR = MDMA_CTCR_SWRM | (1u << MDMA_CTCR_TRGM_Pos) | (2u << MDMA_CTCR_DINC_Pos) |
             (2u << MDMA_CTCR_SSIZE_Pos) | (2u << MDMA_CTCR_DSIZE_Pos) | (2u << MDMA_CTCR_DINCOS_Pos) |
             (3u << MDMA_CTCR_TLEN_Pos) | MDMA_CTCR_BWM;
  ch->CBNDTR = words * 4; ch->CSAR = uint32_t(&GPIOI->IDR); ch->CDAR = uint32_t(mcap);
  ch->CBRUR = 0; ch->CLAR = 0; ch->CTBR = 0; ch->CMAR = 0; ch->CMDR = 0;
  ch->CCR = MDMA_CCR_EN;
  const uint32_t t0 = DWT->CYCCNT; ch->CCR |= MDMA_CCR_SWRQ;
  while (!(ch->CISR & (MDMA_CISR_CTCIF | MDMA_CISR_TEIF))) {}
  const uint32_t cycles = DWT->CYCCNT - t0; const uint32_t err = ch->CISR & MDMA_CISR_TEIF;
  ch->CCR = 0; ch->CIFCR = 0x1F;
  SCB_InvalidateDCache_by_Addr(mcap, sizeof mcap);
  for (uint32_t i = 0; i < words; ++i) cap[i] = uint8_t(((mcap[i] >> 1) & 1u) | ((mcap[i] >> 2) & 2u));
  return err ? 0 : cycles;
}
static void runRate(uint32_t rate) {
  I2sPins pins; // library defaults: bclk 70, data 72, lrck 74
  char line[160];
  Status st = i2sBegin(rate, 20, pins);
  snprintf(line, sizeof line, "rate=%lu begin=%d", (unsigned long)rate, int(st)); text(line);
  if (st != Status::Ok) return;
  uint32_t t = millis();
  while (millis() - t < 200) { fill(); delay(1); }
  OutputStats o = i2sStats();
  snprintf(line, sizeof line, "rate=%lu running=%d underruns=%lu", (unsigned long)rate, int(o.running), (unsigned long)o.underruns); text(line);
  // Size the window to ~0.9 ms of capture from a calibration pass.
  __disable_irq(); uint32_t c = sample(2000); __enable_irq();
  uint32_t n = uint32_t(uint64_t(2000) * (SystemCoreClock / 1111) / (c ? c : 1));
  if (n > sizeof cap) n = sizeof cap;
  for (int k = 0; k < 8; ++k) {
    { uint32_t w = millis(); while (millis() - w < 60) { fill(); delay(1); } } fill();
    const uint32_t first = frameNo;
    __disable_irq(); const uint32_t cycles = sample(n); __enable_irq();
    const uint32_t hdr[5] = {rate, n, cycles, first, frameNo};
    Serial.write(uint8_t('K')); uint32_t len = sizeof hdr + n; Serial.write(reinterpret_cast<const uint8_t*>(&len), 4);
    Serial.write(reinterpret_cast<const uint8_t*>(hdr), sizeof hdr); Serial.write(cap, n);
  }
  __disable_irq(); c = sampleI(2000); __enable_irq();
  n = uint32_t(uint64_t(2000) * (SystemCoreClock / 1111) / (c ? c : 1)); if (n > sizeof cap) n = sizeof cap;
  for (int k = 0; k < 8; ++k) {
    { uint32_t w = millis(); while (millis() - w < 60) { fill(); delay(1); } } fill();
    const uint32_t first = frameNo;
    __disable_irq(); const uint32_t cycles = sampleI(n); __enable_irq();
    const uint32_t hdr[5] = {rate | 0x80000000u, n, cycles, first, frameNo};
    Serial.write(uint8_t('K')); uint32_t len = sizeof hdr + n; Serial.write(reinterpret_cast<const uint8_t*>(&len), 4);
    Serial.write(reinterpret_cast<const uint8_t*>(hdr), sizeof hdr); Serial.write(cap, n);
  }
  for (int k = 0; k < 8; ++k) {
    { uint32_t w = millis(); while (millis() - w < 60) { fill(); delay(1); } } fill();
    const uint32_t first = frameNo; const uint32_t m = 16384;
    const uint32_t cycles = sampleMdma(m);
    const uint32_t hdr[5] = {rate | 0x40000000u, m, cycles, first, frameNo};
    Serial.write(uint8_t('K')); uint32_t len = sizeof hdr + m; Serial.write(reinterpret_cast<const uint8_t*>(&len), 4);
    Serial.write(reinterpret_cast<const uint8_t*>(hdr), sizeof hdr); Serial.write(cap, m);
  }
  o = i2sStats();
  snprintf(line, sizeof line, "rate=%lu end running=%d underruns=%lu core_hz=%lu", (unsigned long)rate, int(o.running), (unsigned long)o.underruns, (unsigned long)SystemCoreClock); text(line);
  i2sEnd();
}
void setup() {
  Serial.begin(115200);
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; DWT->LAR = 0xC5ACCE55; DWT->CYCCNT = 0; DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}
void loop() {
  const int command = Serial.available() ? Serial.read() : -1;
  if (command == 'B') { delay(100); _ontouch1200bps_(); }  // DFU entry for tools/flash_giga.py
  if (command == 'g') {
    for (uint32_t r : {44100u, 48000u, 96000u, 192000u}) runRate(r);
    block('Z', nullptr, 0);
  }
  delay(1);
}
