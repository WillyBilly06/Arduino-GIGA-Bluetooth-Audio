#include <Arduino.h>
#include <mbed.h>

#include <GigaI2sOutput.h>
#include <detail/Memory.h>
#include <platform/I2sPins.h>
using namespace giga_bredr;
using namespace giga_bredr::detail;
static const uint32_t rates[]={48000,44100,96000,176400,192000,48000,44100};
static unsigned trial=0;
static uint32_t started=0,lastReport=0,phaseLeft=0,phaseRight=0,stepLeft=0,stepRight=0;
static const int32_t sine24[256]={0,6587648,13171456,19747328,26311168,32859136,39387648,45892096,52369152,58814464,65224448,71595008,77922560,84203264,90433024,96608512,102725632,108781056,114770944,120691456,126539520,132311296,138003200,143612160,149134592,154567168,159906560,165149952,170293504,175334656,180270080,185096960,189812480,194413568,198897408,203261696,207503360,211620096,215609344,219468544,223195904,226788608,230244608,233562112,236738816,239772928,242662656,245406208,248001792,250448128,252743680,254886912,256876544,258711552,260390656,261912832,263277312,264483328,265529856,266416640,267142656,267707904,268111872,268354560,268435200,268354560,268111872,267707904,267142656,266416640,265529856,264483328,263277312,261912832,260390656,258711552,256876544,254886912,252743680,250448128,248001792,245406208,242662656,239772928,236738816,233562112,230244608,226788608,223195904,219468544,215609344,211620096,207503360,203261696,198897408,194413568,189812480,185096960,180270080,175334656,170293504,165149952,159906560,154567168,149134592,143612160,138003200,132311296,126539520,120691456,114770944,108781056,102725632,96608512,90433024,84203264,77922560,71595008,65224448,58814464,52369152,45892096,39387648,32859136,26311168,19747328,13171456,6587648,0,-6587904,-13171712,-19747584,-26311424,-32859392,-39387904,-45892352,-52369408,-58814720,-65224704,-71595264,-77922816,-84203520,-90433280,-96608768,-102725888,-108781312,-114771200,-120691712,-126539776,-132311552,-138003456,-143612416,-149134848,-154567424,-159906816,-165150208,-170293760,-175334912,-180270336,-185097216,-189812736,-194413824,-198897664,-203261952,-207503616,-211620352,-215609600,-219468800,-223196160,-226788864,-230244864,-233562368,-236739072,-239773184,-242662912,-245406464,-248002048,-250448384,-252743936,-254887168,-256876800,-258711808,-260390912,-261913088,-263277568,-264483584,-265530112,-266416896,-267142912,-267708160,-268112128,-268354816,-268435456,-268354816,-268112128,-267708160,-267142912,-266416896,-265530112,-264483584,-263277568,-261913088,-260390912,-258711808,-256876800,-254887168,-252743936,-250448384,-248002048,-245406464,-242662912,-239773184,-236739072,-233562368,-230244864,-226788864,-223196160,-219468800,-215609600,-211620352,-207503616,-203261952,-198897664,-194413824,-189812736,-185097216,-180270336,-175334912,-170293760,-165150208,-159906816,-154567424,-149134848,-143612416,-138003456,-132311552,-126539776,-120691712,-114771200,-108781312,-102725888,-96608768,-90433280,-84203520,-77922816,-71595264,-65224704,-58814720,-52369408,-45892352,-39387904,-32859392,-26311424,-19747584,-13171712,-6587904};
static size_t pending=0,offset=0,baseline=0;
static int32_t samples[256*2];
static bool active=false,finished=false,primingReported=false;
static unsigned beginFailures=0,dmaFailures=0,cleanupFailures=0,startupFailures=0;
void report() {
 const OutputStats s=i2sStats();
 Serial.print("I2S_DMA trial="); Serial.print(trial); Serial.print(" requested_rate="); Serial.print(s.sample_rate);
 Serial.print(" initialized="); Serial.print(s.initialized); Serial.print(" priming="); Serial.print(s.priming);
 Serial.print(" running="); Serial.print(s.running); Serial.print(" completed_frames="); Serial.print((uint32_t)s.frames_transmitted);
 Serial.print(" queued="); Serial.print(s.queued_frames); Serial.print(" underrun_halves="); Serial.print(s.underruns);
 Serial.print(" dma_errors="); Serial.print(s.dma_errors); Serial.print(" DMA_M0AR=0x"); Serial.println(DMA1_Stream7->M0AR,HEX);
}
void beginTrial() {
 phaseLeft=phaseRight=0; pending=offset=0; primingReported=false; baseline=memoryStats().current_bytes;
 stepLeft=(uint64_t(440)<<32)/rates[trial]; stepRight=(uint64_t(880)<<32)/rates[trial];
 const Status result=i2sBegin(rates[trial]); active=result==Status::Ok;
 if(!active) ++beginFailures;
 Serial.print("I2S_BEGIN trial="); Serial.print(trial); Serial.print(" rate="); Serial.print(rates[trial]);
 Serial.print(" status="); Serial.print(statusName(result)); Serial.print(" tracked_bytes="); Serial.println(memoryStats().current_bytes);
 if(active) {
  PLL2_ClocksTypeDef clocks={}; HAL_RCCEx_GetPLL2ClockFreq(&clocks);
  const uint32_t cfgr=SPI2->I2SCFGR;
  const uint32_t divider=2*((cfgr&SPI_I2SCFGR_I2SDIV)>>SPI_I2SCFGR_I2SDIV_Pos)+((cfgr&SPI_I2SCFGR_ODD)?1:0);
  Serial.print("I2S_CLOCK pll2p_hz="); Serial.print(clocks.PLL2_P_Frequency);
  Serial.print(" divider="); Serial.print(divider); Serial.print(" register_derived_fs=");
  Serial.print(divider?double(clocks.PLL2_P_Frequency)/(64.0*divider):0.0,4);
  Serial.print(" I2SCFGR=0x"); Serial.print(cfgr,HEX); Serial.print(" DMA_M0AR=0x"); Serial.println(DMA1_Stream7->M0AR,HEX);
  Serial.print("I2S_GPIO ws_arduino="); Serial.print(GIGA_BREDR_I2S_WS_PIN);
  Serial.print(" GPIOB_MODER=0x"); Serial.print(GPIOB->MODER,HEX);
  Serial.print(" GPIOB_AFRH=0x"); Serial.print(GPIOB->AFR[1],HEX);
  Serial.print(" GPIOI_MODER=0x"); Serial.print(GPIOI->MODER,HEX);
  Serial.print(" GPIOI_AFRL=0x"); Serial.println(GPIOI->AFR[0],HEX);
 }
 started=lastReport=millis();
}
void setup() {
 Serial.begin(115200); const uint32_t wait=millis(); while(!Serial && millis()-wait<3000) {}
 delay(500);
 Serial.print("I2S2 PCM5102A D70=BCK D"); Serial.print(GIGA_BREDR_I2S_WS_PIN);
 Serial.println("=WS D72=DIN; wire/acoustic output UNVERIFIED");
 Serial.println("I2S_TEST 7 sequential 3-second rate trials; each begins with 500ms negotiation silence before PCM");
 beginTrial();
}
void loop() {
 if(finished) {
  if(millis()-lastReport>=5000) {
   lastReport=millis(); Serial.print("I2S_FINAL trials=7 begin_failures="); Serial.print(beginFailures);
   Serial.print(" startup_failures="); Serial.print(startupFailures);
   Serial.print(" dma_failures="); Serial.print(dmaFailures); Serial.print(" cleanup_failures="); Serial.print(cleanupFailures);
   Serial.print(" tracked_bytes="); Serial.print(memoryStats().current_bytes);
   Serial.println(" electrical=UNKNOWN acoustic=UNKNOWN");
  }
  delay(1); return;
 }
 if(active && millis()-started>=500) {
  // Left 440 Hz, right 880 Hz, -18 dBFS, valid signed 24 bits left-aligned.
  if(!pending) {
   for(unsigned i=0;i<256;++i) {
    samples[2*i]=sine24[phaseLeft>>24]; samples[2*i+1]=sine24[phaseRight>>24];
    phaseLeft+=stepLeft; phaseRight+=stepRight;
   }
   pending=256; offset=0;
  }
  const size_t n=i2sWrite(samples+offset*2,pending); pending-=n; offset+=n;
 }
 if(!primingReported && millis()-started>=400) { primingReported=true; report(); }
 if(millis()-lastReport>=1000) { lastReport=millis(); report(); }
 if(millis()-started>=3000) {
  const OutputStats s=i2sStats(); report(); dmaFailures+=s.dma_errors;
  if(active && (!s.running || s.priming || !s.frames_transmitted)) ++startupFailures;
  i2sEnd(); active=false;
  const size_t after=memoryStats().current_bytes; const OutputStats stopped=i2sStats();
  if(after!=baseline || stopped.initialized || stopped.priming || stopped.running) ++cleanupFailures;
  Serial.print("I2S_END trial="); Serial.print(trial); Serial.print(" tracked_bytes="); Serial.print(after);
  Serial.print(" baseline="); Serial.print(baseline); Serial.print(" pll2_on="); Serial.print(bool(RCC->CR&RCC_CR_PLL2ON));
  Serial.print(" dma_enabled="); Serial.println(bool(DMA1_Stream7->CR&DMA_SxCR_EN));
  if(++trial<sizeof(rates)/sizeof(rates[0])) beginTrial();
  else { finished=true; lastReport=millis()-5000; }
 }
}





