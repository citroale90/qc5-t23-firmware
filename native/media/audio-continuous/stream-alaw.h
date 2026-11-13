/* SPDX-License-Identifier: GPL-2.0-or-later
 * Existing WebUI listen.js wire contract: codec12,16kHz G.711A-law. Input
 * is native S16LE ADC data; no resampling, noise suppression or echo effect.
 */
#ifndef QC5_AUDIO_STREAM_ALAW_H
#define QC5_AUDIO_STREAM_ALAW_H
#include <stddef.h>
#include <stdint.h>
#include "stream-descriptors.h"
static uint8_t qas_alaw(int16_t sample)
{
 unsigned magnitude=sample<0?(unsigned)(-(int)sample-1)/8u:(unsigned)sample/8u;
 unsigned segment=0,top=31;
 while(segment<8&&magnitude>top){segment++;top=top*2+1;}
 unsigned value;
 if(segment>=8)value=0x7f;
 else value=(segment<<4)|((magnitude>>(segment<2?1:segment))&15u);
 return (uint8_t)(value^(sample<0?0x55u:0xd5u));
}
/* Caller supplies a stable private copy, already verified by a second DMA
 * observation. The output can then be sent with an absolute deadline without
 * retaining or reading a live DMA slot. No byte is written on bad sizes. */
static int qas_alaw_packet(const uint8_t *pcm,size_t bytes,uint8_t *out,size_t capacity)
{
 if(!pcm||!out||bytes!=QAS_PERIOD_BYTES||capacity<QAS_PERIOD_SAMPLES+1)return 0;
 out[0]=12;
 for(unsigned i=0;i<QAS_PERIOD_SAMPLES;i++){
  unsigned word=(unsigned)pcm[2*i]|(unsigned)pcm[2*i+1]<<8;
  int value=word<32768?(int)word:(int)word-65536;
  out[1+i]=qas_alaw((int16_t)value);
 }
 return QAS_PERIOD_SAMPLES+1;
}
#endif
