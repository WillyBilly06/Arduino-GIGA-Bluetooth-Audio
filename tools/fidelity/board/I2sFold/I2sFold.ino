// Equivalent-time I2S probe: constant frames (left 0xA53C96, right 0x5A3C69,
// each <<8) so the pad waveform repeats every frame; long GPIOI+GPIOB captures
// with interrupts masked (the DMA halves hold identical data, so stale halves
// are still correct). The host folds samples by frame phase using LRCK edges.
// Protocol after 'g': 'K' + u32 len + {u32 rate, u32 samples, u32 cycles} + bytes
// (bit0 BCLK, bit1 DIN, bit2 LRCK); 'S' text; 'Z' done. 'B' enters DFU.
#include <GigaBredr.h>
#include "platform/GigaI2s.h"
using namespace giga_bredr;
using namespace giga_bredr::detail;
static uint8_t cap[12000]; // < one ~1 ms DMA half at 13.3 MHz sampling
static void block(char tag, const void* d, uint32_t n) {
  Serial.write(uint8_t(tag)); Serial.write(reinterpret_cast<const uint8_t*>(&n), 4);
  if (n) Serial.write(reinterpret_cast<const uint8_t*>(d), n);
}
static void fill() {
  int32_t buf[64 * 2];
  for (int i = 0; i < 64; ++i) { buf[2 * i] = int32_t(0xA53C96u << 8); buf[2 * i + 1] = int32_t(0x5A3C69u << 8); }
  while (i2sWrite(buf, 64) == 64) {}
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
static void runRate(uint32_t rate) {
  char line[96];
  Status st = i2sBegin(rate, 20, I2sPins());
  snprintf(line, sizeof line, "rate=%lu begin=%d", (unsigned long)rate, int(st)); block('S', line, strlen(line));
  if (st != Status::Ok) return;
  for (int k = 0; k < 8; ++k) {
    uint32_t w = millis(); while (millis() - w < 60) { fill(); delay(1); } fill();
    __disable_irq(); const uint32_t cycles = sample(sizeof cap); __enable_irq();
    const uint32_t hdr[3] = {rate, uint32_t(sizeof cap), cycles};
    Serial.write(uint8_t('K')); uint32_t len = sizeof hdr + sizeof cap; Serial.write(reinterpret_cast<const uint8_t*>(&len), 4);
    Serial.write(reinterpret_cast<const uint8_t*>(hdr), sizeof hdr); Serial.write(cap, sizeof cap);
  }
  i2sEnd();
}
void setup() {
  Serial.begin(115200);
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; DWT->LAR = 0xC5ACCE55; DWT->CYCCNT = 0; DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}
void loop() {
  const int command = Serial.available() ? Serial.read() : -1;
  if (command == 'B') { delay(100); _ontouch1200bps_(); }
  if (command == 'g') { for (uint32_t r : {48000u, 96000u, 192000u}) runRate(r); block('Z', nullptr, 0); }
  delay(1);
}
