#pragma once
#include "Arduino.h"
namespace owned_radio_test {
inline int write_allowance=-1;
inline bool write_failure=false;
inline unsigned input_bytes=0;
}
namespace mbed {
template<class Signature>class Callback;
template<>class Callback<void()> {
public:void(*function)()=nullptr;Callback()=default;Callback(void(*fn)()):function(fn){}
};
inline Callback<void()> callback(void(*fn)()){return Callback<void()>(fn);}
class SerialBase {
public:enum Parity{None};enum Flow{RTSCTS};enum IrqType{RxIrq,TxIrq};
};
class UnbufferedSerial:public SerialBase {
public:
 Callback<void()> rx_callback,tx_callback;
 UnbufferedSerial(int,int,int){}
 ~UnbufferedSerial(){assert(!rx_callback.function&&!tx_callback.function);}
 void baud(int){}void format(int,Parity,int){}void set_flow_control(Flow,int,int){}
 void attach(Callback<void()> c,IrqType type){if(type==RxIrq)rx_callback=c;else tx_callback=c;}
 bool readable()const{return owned_radio_test::input_bytes!=0;}
 bool writeable()const{return owned_radio_test::write_allowance!=0;}
 int read(void*p,size_t n){if(n!=1||!readable())return 0;--owned_radio_test::input_bytes;*static_cast<uint8_t*>(p)=0;return 1;}
 int write(const void*,size_t n){if(owned_radio_test::write_failure)return 0;if(owned_radio_test::write_allowance>0)--owned_radio_test::write_allowance;return int(n);}
};
}
