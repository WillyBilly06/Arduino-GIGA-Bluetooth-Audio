#pragma once
#include <stdint.h>
struct TestSerial {
    void begin(unsigned){}
    bool connected()const{return false;}
    explicit operator bool()const{return false;}
    int send_nb(uint8_t*,uint32_t,uint32_t* sent,bool){*sent=0;return 0;}
};
extern TestSerial _SerialUSB;
#define Serial _SerialUSB
