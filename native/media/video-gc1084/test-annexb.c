/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "annexb-native.h"
static size_t decode(const uint8_t *in,size_t n,uint8_t *out)
{
 unsigned zeros=0;size_t used=0;
 assert(n>=5&&!memcmp(in,"\0\0\0\1",4)&&in[4]==0x65);
 for(size_t i=5;i<n;i++){
  if(zeros==2&&in[i]==3){assert(i+1<n&&in[i+1]<=3);zeros=0;continue;}
  if(zeros==2)assert(in[i]>3);
  out[used++]=in[i];zeros=in[i]?0:zeros+1;
 }
 return used;
}
int main(void)
{
 uint8_t input[64],whole[128],split[128],back[64];struct qna_writer a,b;
 /* Exhaust all 4-byte sequences over the 0..4 emulation-prevention alphabet,
  * at every chunk seam. Decode independently and require lossless roundtrip. */
 for(unsigned code=0;code<625;code++){
  unsigned number=code;for(unsigned i=0;i<4;i++){input[i]=number%5;number/=5;}
  assert(qna_begin(&a,whole,sizeof whole,5,3)&&qna_append(&a,input,4));
  assert(decode(whole,a.used,back)==4&&!memcmp(back,input,4));
  for(unsigned seam=0;seam<=4;seam++){
   assert(qna_begin(&b,split,sizeof split,5,3));
   assert(qna_append(&b,input,seam)&&qna_append(&b,input+seam,4-seam));
   assert(a.used==b.used&&!memcmp(whole,split,a.used));
  }
 }
 memset(input,0,sizeof input);assert(qna_begin(&a,whole,sizeof whole,5,3)&&qna_append(&a,input,sizeof input));
 assert(a.used<=qna_bound(sizeof input));assert(decode(whole,a.used,back)==sizeof input);
 for(size_t cap=0;cap<a.used;cap++){
  memset(split,0xa5,sizeof split);int began=qna_begin(&b,split,cap,5,3);
  if(began)assert(!qna_append(&b,input,sizeof input));
  for(size_t n=cap;n<sizeof split;n++)assert(split[n]==0xa5);
  assert(!qna_append(&b,input,1));
 }
 assert(qna_bound(SIZE_MAX)==SIZE_MAX);
 assert(!qna_begin(&b,split,sizeof split,0,3));assert(!qna_begin(&b,split,sizeof split,1,4));
 assert(qna_begin(&b,split,sizeof split,5,3));assert(!qna_append(&b,split,1));
 puts("Annex-B: 625 vectors, 3125 seams, roundtrip, bounds and aliases verified");return 0;
}
