#pragma once
// Single-thread native schedule boundary for the actual Arduino owner loop.
// This does not model RTOS execution time or prove hardware interrupt latency.
#include "mbed.h"

constexpr int osPriorityAboveNormal=1,osPriorityNormal=0,osOK=0;
constexpr uint32_t osFlagsError=0x80000000u;
constexpr uint32_t osFlagsErrorTimeout=0xfffffffeu;
constexpr uint32_t osFlagsErrorResource=0xfffffffdu;

namespace owner_wake_test {
inline void (*wait_hook)(uint32_t,uint32_t*)=nullptr;
inline void (*error_hook)()=nullptr;
inline void (*start_hook)()=nullptr;
inline void (*join_hook)(int)=nullptr;
inline void (*destroy_hook)()=nullptr;
inline uint32_t wait_error=0;
inline unsigned sets=0,sleeps=0,starts=0,joins=0,fail_start=0;
inline void timeUs(uint32_t now){owned_radio_test::now_ms=now/1000;owned_radio_test::us_remainder=now%1000;}
inline void advance(uint32_t us){timeUs(micros()+us);}
}
namespace mbed {
template<class T>struct BoundCallback{T*object;void(T::*method)();};
template<class T>BoundCallback<T> callback(T*p,void(T::*method)()){return {p,method};}
}
namespace rtos {
class Mutex {public:void lock(){}void unlock(){}};
class Semaphore {public:Semaphore(int,int){}void release(){}bool try_acquire_for(unsigned){return false;}};
class Thread {
    int priority_;
public:
    Thread(int p,unsigned,void*,const char*):priority_(p){}
    template<class F>int start(F){
        ++owner_wake_test::starts;
        if(owner_wake_test::start_hook)owner_wake_test::start_hook();
        return owner_wake_test::starts==owner_wake_test::fail_start?-1:osOK;
    }
    int join(){++owner_wake_test::joins;if(owner_wake_test::join_hook)owner_wake_test::join_hook(priority_);return osOK;}
};
class EventFlags {
    uint32_t bits_=0;
public:
    ~EventFlags(){if(owner_wake_test::destroy_hook)owner_wake_test::destroy_hook();}
    uint32_t set(uint32_t bits){++owner_wake_test::sets;return bits_|=bits;}
    uint32_t get()const{return bits_;}
    uint32_t wait_any(uint32_t wanted,uint32_t ms,bool clear=true){
        if(owner_wake_test::wait_error){
            if(owner_wake_test::wait_error==osFlagsErrorTimeout)owner_wake_test::advance(ms*1000);
            if(owner_wake_test::error_hook)owner_wake_test::error_hook();
            return owner_wake_test::wait_error;
        }
        if(!(bits_&wanted)){
            if(owner_wake_test::wait_hook)owner_wake_test::wait_hook(ms,&bits_);
            else owner_wake_test::advance(ms*1000);
        }
        uint32_t ready=bits_&wanted;
        if(clear)bits_&=~wanted;
        return ready?ready:osFlagsErrorTimeout;
    }
};
namespace ThisThread {
inline void sleep_for(uint32_t ms){
    ++owner_wake_test::sleeps;
    if(owner_wake_test::wait_hook)owner_wake_test::wait_hook(ms,nullptr);
    else owner_wake_test::advance(ms*1000);
}
}
}
