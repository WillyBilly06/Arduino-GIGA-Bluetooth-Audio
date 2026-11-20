#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
/* Inspect actual internal helpers without constructing a wire packet. */
#include "../src/vendor/codecs/aac/huffman.c"
static int reference(unsigned int v,unsigned shift,unsigned width){
 unsigned int divisor=1u<<shift, modulus=1u<<width;
 int field=(int)((v/divisor)%modulus);
 return field >= (int)(modulus/2u) ? field-(int)modulus : field;
}
int main(void){
 unsigned long checks=0;
 for(int signed_value=-32768;signed_value<=32767;++signed_value){
  unsigned int bits=(unsigned int)signed_value;
  const int actual[]={GET_QUAD_W(signed_value),GET_QUAD_X(signed_value),GET_QUAD_Y(signed_value),GET_QUAD_Z(signed_value),GET_PAIR_Y(signed_value),GET_PAIR_Z(signed_value),GET_ESC_Y(signed_value),GET_ESC_Z(signed_value)};
  const unsigned shifts[]={9,6,3,0,5,0,6,0},widths[]={3,3,3,3,5,5,6,6};
  for(unsigned i=0;i<8;++i){if(actual[i]!=reference(bits,shifts[i],widths[i])){fprintf(stderr,"field mismatch v=%d i=%u\n",signed_value,i);return 1;}++checks;}
 }
 if(GET_ESC_Y(8907)!=11 || GET_ESC_Z(8907)!=11)return 2;
 printf("PASS all signed 16-bit packed values: %lu independently computed fields; no wire input\n",checks);return 0;
}
