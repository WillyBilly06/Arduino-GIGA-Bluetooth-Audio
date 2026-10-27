# Runtime I2S output pins

Set `ReceiverConfig.i2s` before `begin()`. Defaults are BCLK70, DATA72,
LRCK74 (legacy build macro69 remains supported), MCLK=-1. Only MCLK accepts
-1; it then reserves/configures no MCLK pad. Change pins after `end()` and a
new `begin()`. The output is 32-bit stereo Philips I2S with the existing DMA,
buffering and recovery behavior.

```cpp
giga_bredr::ReceiverConfig config;
config.i2s.bclk = 70;
config.i2s.data = 72;
config.i2s.lrck = 74;
config.i2s.mclk = -1; // Use68 for optional I2S2 MCLK.
receiver.begin(config);
```

The three/four signals must belong to the same hardware peripheral. Supported
Arduino pin choices are below; this is not arbitrary GPIO routing. If a set
matches more than one peripheral, the lowest matching instance is selected.

| Hardware | BCLK | DATA output | LRCK | MCLK |
|---|---|---|---|---|
| I2S1 |85,91,62|5,93,90|84,44|76|
| I2S2 |1,94,75,70|73,80,72|7,9,74,69|68|
| I2S3 |91|47,93,19|84|15|

Pin/AF mappings come from installed Arduino GIGA4.6 `variant.cpp` and
[ST DS12930 table9](https://www.st.com/resource/en/datasheet/stm32h747ig.pdf).
Internal USB/WiFi power nets92/96 are excluded. Invalid/mixed/duplicate pins
return InvalidArgument before receiver hardware starts. Existing output/AF pin
configuration, shared SPI123 clocks, PLL2 and DMA ownership are checked before
configuration; these register checks do not discover every analog/passive user.
Only selected GPIO fields are restored, preserving unrelated pin changes.

MCLK is256 times the sample rate. The existing45.1584/49.152 MHz kernel is
unchanged. Installed HAL1.10 and [ST HAL1.11](https://raw.githubusercontent.com/STMicroelectronics/stm32h7xx-hal-driver/v1.11.0/Src/stm32h7xx_hal_i2s.c)
permit DIV0/ODD1 bypass at192/176.4 kHz and DIV1/ODD0 at96/88.2 kHz. The backend
checks the programmed prescaler while I2S is stopped. No higher kernel clock
or PLL2 change is needed. MCLK frequency/duty cycle, physical alternate routes
and connected DAC behavior still need target measurement; compilation/native
boundary tests do not qualify electrical output or fix incoming audio gaps.
