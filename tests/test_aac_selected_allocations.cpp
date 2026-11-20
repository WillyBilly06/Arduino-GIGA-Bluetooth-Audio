// Portable AAC allocation and failure/retry regression; no device I/O.
#include "detail/Memory.h"
extern "C" {
#include "vendor/codecs/aac/aacdec.h"
#include "vendor/codecs/aac/sbr.h"
}
#include <cstdio>
#include <cstring>
using namespace giga_bredr::detail;
extern "C" size_t gbr_test_plane();
extern "C" void* gbr_test_work(AACDecInfo*);
static bool empty(){auto s=memoryStats();return s.current_bytes==0 && s.current_blocks==0;}
static bool full(const PSInfoSBR* p){return p && p->sbrHdr && p->sbrGrid && p->sbrFreq && p->sbrChan && p->delayIdxQMFA && p->delayQMFA && p->delayIdxQMFS && p->delayQMFS && p->XBufDelay;}
static int planes(){
 if(!empty())return 10;
 HAACDecoder m=AACInitDecoderRawChannels(1);if(!m)return 11;auto mono=memoryStats().current_bytes;AACFreeDecoder(m);if(!empty())return 12;
 HAACDecoder s=AACInitDecoderRawChannels(2);if(!s)return 13;auto stereo=memoryStats().current_bytes;AACFreeDecoder(s);if(!empty())return 14;
 size_t plane=gbr_test_plane();
 size_t core=2*AAC_MAX_NSAMPS*sizeof(int);size_t diff=stereo>=mono?stereo-mono:0;
 bool ok=stereo>=mono && diff>=core+plane;
 std::printf("BASE_PLANES mono=%zu stereo=%zu core_channel=%zu fixed_element_plane=%zu actual_delta=%zu required_delta=%zu result=%s\n",mono,stereo,core,plane,diff,core+plane,ok?"PASS":"RED");
 return ok?0:1;
}
static int partial(){
 if(!empty())return 20;HAACDecoder h=AACInitDecoderRawChannels(1);if(!h)return 21;
 auto* a=(AACDecInfo*)h;
 auto before=memoryStats();setAllocationFailureCountdown(2);int first=AACEnableSBR(h);setAllocationFailureCountdown(-1);
 auto after=memoryStats();auto* failed=(PSInfoSBR*)a->psInfoSBR;bool cleanFailure=first!=0&&!failed&&!gbr_test_work(a)&&after.current_bytes==before.current_bytes&&after.current_blocks==before.current_blocks;
 int retry=AACEnableSBR(h);auto* current=(PSInfoSBR*)a->psInfoSBR;bool ready=retry==0&&full(current);int flush=ready?AACFlushCodec(h):-9999;
 std::printf("PARTIAL first=%d published=%d child_complete_after_retry=%d retry=%d flush=%d before_bytes=%zu after_bytes=%zu result=%s\n",first,failed!=nullptr,ready,retry,flush,before.current_bytes,after.current_bytes,cleanFailure&&ready&&flush==0?"PASS":"RED");
 AACFreeDecoder(h);bool clean=empty();if(!clean)std::puts("CLEANUP_NONZERO");return cleanFailure&&ready&&flush==0&&clean?0:1;
}
static int workleak(){
 if(!empty())return 30;HAACDecoder h=AACInitDecoderRawChannels(1);if(!h)return 31;
 auto* a=(AACDecInfo*)h;auto before=memoryStats();
 setAllocationFailureCountdown(1);int first=AACEnableSBR(h);setAllocationFailureCountdown(-1);
 auto after=memoryStats();void* failedWork=gbr_test_work(a);bool cleanFailure=first!=0&&!a->psInfoSBR&&!failedWork&&after.current_bytes==before.current_bytes&&after.current_blocks==before.current_blocks;
 int retry=AACEnableSBR(h);bool ready=retry==0&&full((PSInfoSBR*)a->psInfoSBR);int flush=ready?AACFlushCodec(h):-9999;
 bool orphan=failedWork&&failedWork!=gbr_test_work(a);AACFreeDecoder(h);auto leaked=memoryStats();
 // Restore test allocator accounting after proving the orphan; do not hide RED.
 if(orphan)gbr_free(failedWork);bool clean=empty();
 std::printf("WORK_RETRY first=%d failure_work_published=%d retry=%d ready=%d flush=%d orphan=%d after_close_bytes=%zu result=%s\n",first,failedWork!=nullptr,retry,ready,flush,orphan,leaked.current_bytes,cleanFailure&&ready&&flush==0&&!orphan&&clean?"PASS":"RED");
 return cleanFailure&&ready&&flush==0&&!orphan&&clean?0:1;
}
int main(int argc,char**argv){setvbuf(stdout,nullptr,_IONBF,0);if(argc!=2)return 2;if(!std::strcmp(argv[1],"planes"))return planes();if(!std::strcmp(argv[1],"partial"))return partial();if(!std::strcmp(argv[1],"workleak"))return workleak();return 2;}
