// Alternate-route I2S pin probe (equivalent-time, constant frame, 48 kHz).
// For each route: i2sBegin with those pins, then sample the GPIO input data
// registers of the route's ports with interrupts masked (< one DMA half).
// Byte per sample: bit0 BCLK, bit1 DIN, bit2 LRCK, bit3 MCLK.
// Protocol after 'g': 'S' text; 'K' + u32 len + {u32 route, u32 rate, u32 n, u32 cycles} + bytes; 'Z'. 'B' enters DFU.
#include <GigaBredr.h>
#include "platform/GigaI2s.h"
#include "platform/I2sPins.h"
#include "Tap.h"
using namespace giga_bredr;
using namespace giga_bredr::detail;
struct Route { int16_t bclk, data, lrck, mclk; };
static const Route routes[] = {
  {75, 73, 74, -1},  // SPI2: D75/PD3 BCLK, D73/PC1 DIN, D74/PB12 LRCK
  {75, 73, 71, -1},  // D71/PI2 is I2S2_SDI only: must be rejected
  {70, 72, 69, 68},  // SPI2 legacy D69/PI0 LRCK + D68/PC6 MCLK (256 fs)
  {85, 5, 84, -1},   // SPI1: D85/PA5, D5/PA7, D84/PA4
  {91, 47, 84, -1},  // SPI3: D91/PB3 (JTAG reset AF0), D47/PB2, D84/PA4
  {75, 73, 7, -1},   // SPI2 with D7/PB4 (JTAG reset AF0) as LRCK (AF7)
};
static uint8_t cap[12000];
static GPIO_TypeDef* port(uint8_t p) { switch (p) { case 0: return GPIOA; case 1: return GPIOB; case 2: return GPIOC; case 3: return GPIOD; case 6: return GPIOG; case 8: return GPIOI; default: return nullptr; } }
static void block(char tag, const void* d, uint32_t n) {
  Serial.write(uint8_t(tag)); Serial.write(reinterpret_cast<const uint8_t*>(&n), 4);
  if (n) Serial.write(reinterpret_cast<const uint8_t*>(d), n);
}
static void fill() {
  int32_t buf[64 * 2];
  for (int i = 0; i < 64; ++i) { buf[2 * i] = int32_t(0xA53C96u << 8); buf[2 * i + 1] = int32_t(0x5A3C69u << 8); }
  while (i2sWrite(buf, 64) == 64) {}
}
__attribute__((noinline)) static uint32_t sample(const Tap* taps, uint8_t ntaps, uint32_t n) {
  const uint32_t t0 = DWT->CYCCNT;
  for (uint32_t i = 0; i < n; ++i) {
    uint8_t v = 0;
    for (uint8_t t = 0; t < ntaps; ++t) { const uint32_t r = *taps[t].idr; for (uint8_t s = 0; s < taps[t].count; ++s) v |= uint8_t(((r >> taps[t].shift[s]) & 1u) << taps[t].bit[s]); }
    cap[i] = v;
  }
  return DWT->CYCCNT - t0;
}
static void runRoute(uint32_t r) {
  const Route& q = routes[r]; I2sPins pins; pins.bclk = q.bclk; pins.data = q.data; pins.lrck = q.lrck; pins.mclk = q.mclk;
  char line[160]; I2sRoutePlan plan; const bool planned = i2sRoutePlan(pins, plan);
  Status st = i2sBegin(48000, 20, pins);
  snprintf(line, sizeof line, "route=%lu pins=%d/%d/%d/%d spi=%d begin=%d", (unsigned long)r, q.bclk, q.data, q.lrck, q.mclk, planned ? plan.peripheral : 0, int(st));
  block('S', line, strlen(line));
  if (st != Status::Ok) return;
  Tap taps[4] = {}; uint8_t ntaps = 0;
  for (uint8_t s = 0; s < plan.count; ++s) {
    volatile uint32_t* idr = &port(plan.pins[s].port)->IDR; uint8_t t = 0;
    while (t < ntaps && taps[t].idr != idr) ++t;
    if (t == ntaps) { taps[ntaps].idr = idr; ++ntaps; }
    taps[t].shift[taps[t].count] = plan.pins[s].pin; taps[t].bit[taps[t].count] = s; ++taps[t].count;
  }
  __disable_irq(); const uint32_t cal = sample(taps, ntaps, 500); __enable_irq();
  uint32_t n = uint32_t(uint64_t(500) * (SystemCoreClock / 1111) / (cal ? cal : 1)); if (n > sizeof cap) n = sizeof cap;
  for (int k = 0; k < 6; ++k) {
    uint32_t w = millis(); while (millis() - w < 60) { fill(); delay(1); } fill();
    __disable_irq(); const uint32_t cycles = sample(taps, ntaps, n); __enable_irq();
    const uint32_t hdr[4] = {r, 48000u, n, cycles};
    Serial.write(uint8_t('K')); uint32_t len = sizeof hdr + n; Serial.write(reinterpret_cast<const uint8_t*>(&len), 4);
    Serial.write(reinterpret_cast<const uint8_t*>(hdr), sizeof hdr); Serial.write(cap, n);
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
  if (command == 'g') {
    { char l[160]; snprintf(l, sizeof l, "reset_state GPIOB MODER=%08lx AFRL=%08lx GPIOA MODER=%08lx AFRH=%08lx", (unsigned long)GPIOB->MODER, (unsigned long)GPIOB->AFR[0], (unsigned long)GPIOA->MODER, (unsigned long)GPIOA->AFR[1]); block('S', l, strlen(l)); } for (uint32_t r = 0; r < sizeof routes / sizeof routes[0]; ++r) runRoute(r); block('Z', nullptr, 0); }
  delay(1);
}
