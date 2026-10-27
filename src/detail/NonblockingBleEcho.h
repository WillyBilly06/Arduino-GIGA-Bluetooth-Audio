#pragma once
#include <GigaBredr.h>
// One copied connection-bound reply. Call once per loop, never from callbacks.
class NonblockingBleEcho {
public:
 template<class Receiver> constexpr void poll(Receiver& receiver) {
  if(!pending_) {
   if(!receiver.bleReadWrite(origin_)) return;
   pending_=true;
  }
  const auto status=receiver.bleNotify(origin_);
  if(status!=giga_bredr::Status::Busy&&status!=giga_bredr::Status::OutputFull) pending_=false;
 }
private:
 giga_bredr::BleWrite origin_{};
 bool pending_=false;
};
