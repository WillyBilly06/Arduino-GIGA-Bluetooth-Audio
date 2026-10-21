#pragma once
// Public developmental profile: only the project-owned backend is distributed.
#ifndef GIGA_BREDR_USE_BTSTACK
#define GIGA_BREDR_USE_BTSTACK 0
#endif
#if GIGA_BREDR_USE_BTSTACK != 0
#error Historical backend is not distributed in this source profile
#endif
