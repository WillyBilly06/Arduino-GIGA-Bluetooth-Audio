#include <Arduino.h>
#include <GigaBredr.h>
using namespace giga_bredr;
GigaBredr receiver;
bool started=false;
void setup() {
 Serial.begin(921600); // Never wait for a USB monitor before starting radio.
 ReceiverConfig config;
 config.name="GIGA A2DP only";
 config.enable_a2dp=true;
 config.ble.enabled=false;
 config.i2s.bclk=70;config.i2s.data=72;config.i2s.lrck=74;config.i2s.mclk=-1;
 const Status status=receiver.begin(config);
 started=status==Status::Ok;
 Serial.println(started?"A2DP actor started; pair an audio source":"Receiver begin failed");
}
void loop() {
 if(started) receiver.poll();
 delay(1); // Yield main; existing independent radio/audio workers keep running.
}
