#include <Arduino.h>
#include <GigaBredr.h>
#include <detail/NonblockingBleEcho.h>
using namespace giga_bredr;
GigaBredr receiver;
NonblockingBleEcho echo;
bool started=false;
void setup() {
 Serial.begin(115200);
 ReceiverConfig config;
 config.name="GIGA A2DP + BLE";
 config.enable_a2dp=true;
 config.ble.enabled=true;
 config.i2s.bclk=70;config.i2s.data=72;config.i2s.lrck=74;config.i2s.mclk=-1;
 const Status status=receiver.begin(config);
 started=status==Status::Ok;
 Serial.println(started?"A2DP + BLE actors started; pair audio and connect GATT":"Receiver begin failed");
}
void loop() {
 if(started) { receiver.poll();echo.poll(receiver); }
 delay(1);
}
