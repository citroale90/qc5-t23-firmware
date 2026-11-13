/* SPDX-License-Identifier: GPL-2.0-or-later
 * Decode the existing talk-worklet320-byte G.711A-law frame toS16LE mono.
 * Pure conversion only: no controller, speaker, clock or GPIO operations.
 */
#ifndef QC5_DUPLEX_TALK_ALAW_H
#define QC5_DUPLEX_TALK_ALAW_H
#include <stddef.h>
#include <stdint.h>
static int16_t qad_decode_sample(uint8_t input)
{
 unsigned value=input^0x55u,segment=(value>>4)&7u;
 int magnitude=(int)(value&15u)<<4;
 if(segment==0)magnitude+=8;
 else{magnitude+=264;if(segment>1)magnitude<<=segment-1;}
 return (int16_t)((value&128u)?magnitude:-magnitude);
}
static int qad_decode_voice(const uint8_t *alaw,size_t count,uint8_t *pcm,size_t capacity)
{
 if(!alaw||!pcm||count!=320||capacity<640)return 0;
 for(unsigned n=0;n<320;n++){
  uint16_t word=(uint16_t)qad_decode_sample(alaw[n]);pcm[2*n]=(uint8_t)word;pcm[2*n+1]=(uint8_t)(word>>8);
 }
 return 640;
}
#endif
