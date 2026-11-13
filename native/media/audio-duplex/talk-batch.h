/* SPDX-License-Identifier: GPL-2.0-or-later
 * Finite, non-cyclic TX batches in the existing descriptor page.
 * Hardware remains disabled: the caller must confirm TX stopped before
 * preparing a new batch. A stalled producer cannot make DMA repeat speech.
 */
#ifndef QC5_DUPLEX_TALK_BATCH_H
#define QC5_DUPLEX_TALK_BATCH_H
#include <string.h>
#include "duplex-layout.h"
#include "talk-alaw.h"
struct qad_batch { unsigned frames,bytes,samples;uint32_t descriptor,pcm; };
static struct qas_descriptor qad_finite_description(unsigned frames)
{
 /* PCM packets are contiguous: one descriptor covers the whole finite
  * batch. Neither LINK nor an unused next-descriptor pointer is needed. */
 struct qas_descriptor d={0x0080a300u,QAD_PCM,0x10020034u,frames*(QAD_PERIOD_BYTES/16u),0,6,{0,0}};
 return d;
}
static int qad_batch_prepare(volatile uint8_t *page,size_t capacity,
                            const uint8_t *alaw,size_t count,unsigned tx_stopped,
                            struct qad_batch *out)
{
 if(!page||!alaw||!out||capacity<4096||tx_stopped!=1||!count||count%QAD_SAMPLES||
    count>QAD_PERIODS*QAD_SAMPLES)return 0;
 uintptr_t begin=(uintptr_t)page,src=(uintptr_t)alaw,report=(uintptr_t)out;
 if(begin>UINTPTR_MAX-4096||src>UINTPTR_MAX-count||report>UINTPTR_MAX-sizeof(*out)||
    (src<begin+4096&&begin<src+count)||
    (report<begin+4096&&begin<report+sizeof(*out)))return 0;
 unsigned frames=(unsigned)(count/QAD_SAMPLES);
 /* No stores before the entire request, its aliases and ownership pass. */
 for(unsigned slot=0;slot<QAD_PERIODS;slot++){
  struct qas_descriptor d={0};if(!slot)d=qad_finite_description(frames);
  const uint8_t *bytes=(const uint8_t*)&d;
  for(unsigned n=0;n<sizeof d;n++)page[QAS_DESC_BYTES+slot*32+n]=bytes[n];
 }
 for(unsigned n=0;n<QAD_PCM_BYTES;n++)page[QAD_PCM-QA_DESCRIPTOR+n]=0;
 for(unsigned n=0;n<count;n++){
  uint16_t word=(uint16_t)qad_decode_sample(alaw[n]);
  page[QAD_PCM-QA_DESCRIPTOR+2*n]=(uint8_t)word;
  page[QAD_PCM-QA_DESCRIPTOR+2*n+1]=(uint8_t)(word>>8);
 }
 *out=(struct qad_batch){frames,(unsigned)(count*2),(unsigned)count,QAD_DESCRIPTOR,QAD_PCM};
 return 1;
}
#endif
