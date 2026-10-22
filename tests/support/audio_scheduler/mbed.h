#pragma once
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <cstdint>
constexpr int osPriorityAboveNormal=1,osPriorityNormal=0,osOK=0;
constexpr uint32_t osFlagsError=0x80000000u,osFlagsErrorTimeout=0xfffffffeu;
namespace probe {extern void(*beforeLock)(void*);extern void(*sleepHook)(unsigned);}
namespace mbed {template<class T>auto callback(T*p,void(T::*fn)()){return [p,fn]{(p->*fn)();};}}
namespace rtos {
class Mutex {std::mutex m;public:void lock(){if(probe::beforeLock)probe::beforeLock(this);m.lock();}void unlock(){m.unlock();}bool trylock(){return m.try_lock();}};
class Semaphore {std::atomic<unsigned> n{0};public:Semaphore(unsigned,unsigned){}void release(){n.store(1);}bool try_acquire_for(unsigned){return n.exchange(0)!=0;}};
class Thread {public:Thread(int,unsigned,void*,const char*){}template<class F>int start(F){return osOK;}int join(){return osOK;}};
class EventFlags {std::mutex m;std::condition_variable cv;unsigned flags=0;public:uint32_t set(unsigned f){std::lock_guard<std::mutex> l(m);flags|=f;cv.notify_all();return flags;}uint32_t wait_any(unsigned f,unsigned ms,bool clear){std::unique_lock<std::mutex> l(m);cv.wait_for(l,std::chrono::milliseconds(ms),[&]{return (flags&f)!=0;});unsigned v=flags&f;if(clear)flags&=~f;return v?v:osFlagsErrorTimeout;}};
namespace ThisThread {inline void sleep_for(unsigned ms){if(probe::sleepHook)probe::sleepHook(ms);}}
}
