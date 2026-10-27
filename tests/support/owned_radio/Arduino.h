#pragma once
// Native-only hardware boundary for compiling the actual OwnedRadio.cpp.
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <atomic>
namespace owned_radio_test {
inline uint32_t now_ms=0,us_remainder=0;
inline unsigned critical_depth=0;
struct Registers {uint32_t ISR=1u<<6;};
inline Registers registers;
}
inline uint32_t millis(){return owned_radio_test::now_ms;}
inline uint32_t micros(){return owned_radio_test::now_ms*1000+owned_radio_test::us_remainder;}
inline void delay(uint32_t ms){owned_radio_test::now_ms+=ms;}
inline void __DMB(){std::atomic_thread_fence(std::memory_order_seq_cst);}
inline void core_util_critical_section_enter(){++owned_radio_test::critical_depth;}
inline void core_util_critical_section_exit(){assert(owned_radio_test::critical_depth);--owned_radio_test::critical_depth;}
inline owned_radio_test::Registers* UART7=&owned_radio_test::registers;
constexpr uint32_t USART_ISR_TC=1u<<6;
enum {PF_7,PA_8,PF_8,PF_9,PA_10,PH_7,PG_3};
struct gpio_t {int value=0;};
inline void gpio_init_out(gpio_t* p,int){p->value=0;}
inline void gpio_init_in(gpio_t* p,int){p->value=0;}
inline void gpio_write(gpio_t* p,int value){p->value=value;}
inline uint32_t HAL_GetUIDw0(){return 1;}
inline uint32_t HAL_GetUIDw1(){return 2;}
inline uint32_t HAL_GetUIDw2(){return 3;}
