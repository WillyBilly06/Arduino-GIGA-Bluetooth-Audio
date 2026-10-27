#include <stdint.h>
#if defined(__has_include)
# if __has_include("detail/OwnedUartBaudPolicy.h")
#  include "detail/OwnedUartBaudPolicy.h"
#  define HAVE_OWNED_UART_BAUD_POLICY 1
# endif
#endif
#ifndef HAVE_OWNED_UART_BAUD_POLICY
static_assert(false,"Missing owned UART supported-baud policy helper");
#else
using giga_bredr::detail::ownedUartBaudSupported;
static_assert(ownedUartBaudSupported(115200u),"115200 remains supported");
static_assert(ownedUartBaudSupported(460800u),"460800 remains supported");
static_assert(ownedUartBaudSupported(921600u),"921600 remains supported");
static_assert(ownedUartBaudSupported(2000000u),"default2M remains supported");
static_assert(ownedUartBaudSupported(3000000u),"documented3M transport trial is admitted");
static_assert(!ownedUartBaudSupported(0u)&&!ownedUartBaudSupported(1u),"zero/sentinel rates are rejected");
static_assert(!ownedUartBaudSupported(9600u)&&!ownedUartBaudSupported(57600u),"other low rates remain unsupported");
static_assert(!ownedUartBaudSupported(1500000u)&&!ownedUartBaudSupported(2500000u),"unlisted intermediate rates remain unsupported");
static_assert(!ownedUartBaudSupported(4000000u),"4M needs a separate clock procedure and stays unsupported");
static_assert(!ownedUartBaudSupported(UINT32_MAX),"UINT32_MAX cannot be admitted as a baud");
static_assert(!ownedUartBaudSupported(115199u)&&!ownedUartBaudSupported(115201u),"115200 neighboring values reject");
static_assert(!ownedUartBaudSupported(460799u)&&!ownedUartBaudSupported(460801u),"460800 neighboring values reject");
static_assert(!ownedUartBaudSupported(921599u)&&!ownedUartBaudSupported(921601u),"921600 neighboring values reject");
static_assert(!ownedUartBaudSupported(1999999u)&&!ownedUartBaudSupported(2000001u),"2M neighboring values reject");
static_assert(!ownedUartBaudSupported(2999999u)&&!ownedUartBaudSupported(3000001u),"3M neighboring values reject");
#endif
