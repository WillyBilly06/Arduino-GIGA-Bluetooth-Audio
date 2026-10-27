#ifndef GIGA_BREDR_HFP_AT_H
#define GIGA_BREDR_HFP_AT_H
#include <cstddef>
#include <cstdint>
namespace giga_bredr { namespace owned {
// Serialized owner-only CRLF assembler; no borrowed line survives the next push.
class HfpAtLine {
public:
 enum { Limit=384 };enum class Result:uint8_t { More,Line,Malformed,Overflow };
 void reset(){used_=0;cr_=false;}
 Result push(uint8_t c,const char*& row){row=nullptr;
  if(c=='\r'){if(cr_&&used_)return Result::Malformed;cr_=true;return Result::More;}
  if(c=='\n'){if(!cr_)return Result::Malformed;cr_=false;if(!used_)return Result::More;data_[used_]=0;used_=0;row=data_;return Result::Line;}
  if(cr_||(c<32&&c!='\t')||c==127)return Result::Malformed;
  if(used_==Limit)return Result::Overflow;data_[used_++]=static_cast<char>(c);return Result::More;
 }
private: char data_[Limit+1]{};uint16_t used_=0;bool cr_=false;
};
namespace hfp_at {
inline void spaces(const char*& p){while(*p==' '||*p=='\t')++p;}
inline bool number(const char*& p,uint32_t& n){spaces(p);n=0;bool any=false;while(*p>='0'&&*p<='9'){uint32_t digit=static_cast<uint32_t>(*p-'0');if(n>(UINT32_MAX-digit)/10)return false;n=n*10+digit;++p;any=true;}return any;}
inline bool token(const char*& p,char c){spaces(p);if(*p!=c)return false;++p;return true;}
inline bool end(const char* p){spaces(p);return !*p;}
inline bool prefix(const char* p,const char* expected,const char*& tail){while(*expected){if(*p++!=*expected++)return false;}tail=p;return true;}
inline size_t decimal(uint32_t n,char* p){char r[10];size_t used=0;do{r[used++]=static_cast<char>('0'+n%10);n/=10;}while(n);for(size_t i=0;i<used;++i)p[i]=r[used-i-1];return used;}
}
}}
#endif
