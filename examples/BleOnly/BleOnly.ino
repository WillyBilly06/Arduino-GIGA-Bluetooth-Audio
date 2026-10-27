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
 config.name="GIGA BLE only";
 config.enable_a2dp=false; // Real Classic off: no inquiry/page/admission or audio worker.
 config.ble.enabled=true;
 // Audio options are unused in this mode; no DAC is required.
 const Status status=receiver.begin(config);
 started=status==Status::Ok;
 Serial.println(started?"BLE actor started; discover GIGA BLE only":"Receiver begin failed");
}
void loop() {
 if(started) { receiver.poll();echo.poll(receiver); }
 delay(1); // One bounded echo admission attempt per turn; never spin on Busy.
}
