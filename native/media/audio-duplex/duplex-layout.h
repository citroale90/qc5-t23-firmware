/* SPDX-License-Identifier: GPL-2.0-or-later
 * Planned native intercom memory layout, NO hardware access or enable.
 * Add four20ms playback slots inside the existing descriptor page; keep
 * all64KiB microphone PCM and every video/ISP buffer at the same address.
 */
#ifndef QC5_DUPLEX_LAYOUT_H
#define QC5_DUPLEX_LAYOUT_H
#include "../audio-continuous/stream-descriptors.h"
#define QAD_PERIODS 4u
#define QAD_SAMPLES 320u
#define QAD_PERIOD_BYTES (QAD_SAMPLES*2u)
#define QAD_DESCRIPTOR (QA_DESCRIPTOR+QAS_DESC_BYTES)
#define QAD_DESCRIPTOR_BYTES (QAD_PERIODS*32u)
#define QAD_PCM (QAD_DESCRIPTOR+QAD_DESCRIPTOR_BYTES)
#define QAD_PCM_BYTES (QAD_PERIODS*QAD_PERIOD_BYTES)
#define QAD_END (QAD_PCM+QAD_PCM_BYTES)
_Static_assert(QAD_END+384u==QA_PCM,"Native duplex fits existing descriptor page with384guard bytes");
_Static_assert(!(QAD_PCM&15)&&!(QAD_DESCRIPTOR&31)&&!(QAD_PERIOD_BYTES%16),"Native16-byte DMA alignment");
static int qad_owns_address(uint32_t p)
{return (p>=QAD_DESCRIPTOR&&p-QAD_DESCRIPTOR<QAD_DESCRIPTOR_BYTES)||(p>=QAD_PCM&&p-QAD_PCM<QAD_PCM_BYTES);}
static struct qas_descriptor qad_description(unsigned slot)
{
 uint32_t next=QAD_DESCRIPTOR+((slot+1)%QAD_PERIODS)*32u;
 struct qas_descriptor out={0x0080a301u,QAD_PCM+slot*QAD_PERIOD_BYTES,0x10020034u,
  ((next>>4)<<24)|(QAD_PERIOD_BYTES/16u),0,6,{0,0}};
 return out;
}
static int qad_layout_valid(void)
{
 if(QAD_END>QA_PCM||QAD_DESCRIPTOR<QA_DESCRIPTOR||QAD_PCM_BYTES!=2560||QAD_DESCRIPTOR_BYTES!=128)return 0;
 for(uint32_t p=QA_BASE;p<QA_END;p++)if(qas_owns_address(p)&&qad_owns_address(p))return 0;
 for(unsigned slot=0;slot<QAD_PERIODS;slot++){
  struct qas_descriptor d=qad_description(slot);
  if(d.source<QAD_PCM||d.source+QAD_PERIOD_BYTES>QAD_END||d.target!=0x10020034||d.request!=6)return 0;
 }
 return 1;
}
#endif
