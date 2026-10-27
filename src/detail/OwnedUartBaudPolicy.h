#pragma once
#include <stdint.h>
namespace giga_bredr { namespace detail {
// Owned UART admission only; actual controller/host clock feasibility remains
// separately checked. Default stays2M;4M requires a separate clock procedure.
constexpr bool ownedUartBaudSupported(uint32_t baud) {
 return baud==115200u||baud==460800u||baud==921600u||
        baud==2000000u||baud==3000000u;
}
}}
