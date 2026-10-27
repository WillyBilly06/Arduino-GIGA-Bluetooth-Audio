#include "GigaBredr.h"
#include "../examples/OperationModeCommon/NonblockingBleEcho.h"
using namespace giga_bredr;
struct EchoPort {
 BleWrite incoming{};Status result=Status::Busy;bool available=true;
 unsigned reads=0,attempts=0;uint32_t last_generation=0;uint8_t last_byte=0;
 constexpr EchoPort(){incoming.generation=7;incoming.length=1;incoming.data[0]=42;}
 constexpr bool bleReadWrite(BleWrite& out){++reads;if(!available)return false;out=incoming;available=false;return true;}
 constexpr Status bleNotify(const BleWrite& origin){++attempts;last_generation=origin.generation;last_byte=origin.data[0];return result;}
};
constexpr bool boundedRetryRetainsOrigin(){
 EchoPort port;NonblockingBleEcho echo;
 echo.poll(port);if(port.reads!=1||port.attempts!=1||port.last_generation!=7||port.last_byte!=42)return false;
 port.incoming.generation=8;port.incoming.data[0]=99;port.available=true;port.result=Status::OutputFull;
 echo.poll(port);if(port.reads!=1||port.attempts!=2||port.last_generation!=7||port.last_byte!=42)return false;
 port.result=Status::Ok;echo.poll(port);if(port.reads!=1||port.attempts!=3||port.last_generation!=7)return false;
 port.result=Status::InvalidArgument;echo.poll(port);if(port.reads!=2||port.attempts!=4||port.last_generation!=8||port.last_byte!=99)return false;
 echo.poll(port);return port.reads==3&&port.attempts==4;
}
constexpr bool emptyDoesNotNotify(){EchoPort port;port.available=false;NonblockingBleEcho echo;echo.poll(port);return port.reads==1&&port.attempts==0;}
static_assert(boundedRetryRetainsOrigin(),"Busy/OutputFull must retain one origin without another dequeue; terminal/stale completion releases it");
static_assert(emptyDoesNotNotify(),"Empty RX must not create a notification");
