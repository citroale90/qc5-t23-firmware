/* SPDX-License-Identifier: GPL-2.0-or-later
 * Capture-only cyclic descriptor plan for the existing64KiB PCM reservation.
 * The layout/link encoding is checked against the pinned Ingenic SDK.
 * No hardware access, allocation, speaker enable or video buffer changes.
 */
#ifndef QC5_AUDIO_STREAM_DESCRIPTORS_H
#define QC5_AUDIO_STREAM_DESCRIPTORS_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "../audio-t23/audio-layout.h"
#define QAS_PERIODS 32u
#define QAS_PERIOD_BYTES 2000u
#define QAS_PERIOD_SAMPLES 1000u
#define QAS_PCM_BYTES (QAS_PERIODS*QAS_PERIOD_BYTES)
#define QAS_DESC_BYTES (QAS_PERIODS*32u)
struct qas_descriptor {uint32_t command,source,target,count,stride,request,reserved[2];};
_Static_assert(sizeof(struct qas_descriptor)==32,"Native8-word PDMA descriptor");
_Static_assert(QAS_PCM_BYTES==QA_BYTES&&QA_PCM+QAS_PCM_BYTES+512<=QA_END,"Existing PCM reservation/guard retained");
_Static_assert(QAS_DESC_BYTES<=4096&&!(QA_DESCRIPTOR&4095)&&QAS_PERIOD_BYTES%16==0,"All links stay in the same4KiB descriptor page");
static uint32_t qas_link(unsigned next)
{return ((QA_DESCRIPTOR+next*32u)>>4)<<24;}
static struct qas_descriptor qas_description(unsigned slot)
{
 /* The finite native command0x0040a300 already fixes source/dest16-bit,
  * destination increment,16-byte transactions and no interrupt. Enable
  * LINK alone. TIE stays clear: this userspace owner polls the controller
  * and the current kernel has no PDMA IRQ handler. */
 struct qas_descriptor d={0x0040a301u,0x10020034u,QA_PCM+slot*QAS_PERIOD_BYTES,
  qas_link((slot+1)%QAS_PERIODS)|(QAS_PERIOD_BYTES/16u),0,7,{0,0}};
 return d;
}
static int qas_descriptors(struct qas_descriptor *out,size_t count)
{
 if(!out||count!=QAS_PERIODS)return 0;
 for(unsigned n=0;n<QAS_PERIODS;n++)out[n]=qas_description(n);
 return 1;
}
static int qas_descriptors_valid(const struct qas_descriptor *table,size_t count)
{
 if(!table||count!=QAS_PERIODS)return 0;
 for(unsigned n=0;n<QAS_PERIODS;n++){
  struct qas_descriptor expected=qas_description(n);
  if(memcmp(&table[n],&expected,sizeof expected))return 0;
 }
 return 1;
}
static int qas_owns_address(uint32_t p)
{return (p>=QA_DESCRIPTOR&&p-QA_DESCRIPTOR<QAS_DESC_BYTES)||(p>=QA_PCM&&p-QA_PCM<QAS_PCM_BYTES);}
#endif
