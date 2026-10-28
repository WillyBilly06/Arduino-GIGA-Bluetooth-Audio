#include "detail/VoiceInputLifetime.h"
#include <cstdint>
#include <cstdio>
using giga_bredr::detail::reserveVoiceInputLifetime;
int main(){
 uint64_t counter=0,a=0,b=0;
 if(!reserveVoiceInputLifetime(counter,a)||a!=1||!reserveVoiceInputLifetime(counter,b)||b!=2||a==b)return 2;
 counter=UINT64_MAX-1;uint64_t last=0;
 if(!reserveVoiceInputLifetime(counter,last)||last!=UINT64_MAX||counter!=UINT64_MAX)return 3;
 uint64_t refused=7;if(reserveVoiceInputLifetime(counter,refused)||refused!=0||counter!=UINT64_MAX)return 4;
 std::puts("VOICE_INPUT_LIFETIME_EXHAUSTION_PASS");return 0;
}
