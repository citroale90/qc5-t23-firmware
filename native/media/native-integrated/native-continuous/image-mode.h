/* SPDX-License-Identifier: GPL-3.0-only
 * Typed capture-owner IPC. A mode is acknowledged only on an owned frame;
 * grayscale changes the existing UV plane, never Y/geometry/QP or ownership.
 */
#ifndef QC5_NATIVE_IMAGE_MODE_H
#define QC5_NATIVE_IMAGE_MODE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define QDI_MAGIC 0x37494451u
enum {QDI_STATUS=1,QDI_SET_NIGHT=2,QDI_TRANSITION=3};
struct qdi_request {uint32_t magic,serial,operation,value;};
struct qdi_status {
 uint32_t magic,serial,ok,night,pending,mean,exposure,gain,settled,transition;
 uint64_t stamp,frames;
};
_Static_assert(sizeof(struct qdi_request)==16,"Image request ABI");
_Static_assert(sizeof(struct qdi_status)==56,"Image status ABI");
struct qdi_owner {uint64_t stamp,frames,stable_since,hold_until;unsigned desired,night,mean,exposure,gain;};
static int qdi_apply(struct qdi_owner *s,const struct qdi_request *p,struct qdi_status *r,uint64_t now)
{
 if(!s||!p||!r||!now||now>UINT64_MAX-10000000)return 0;
 unsigned ok=p->magic==QDI_MAGIC&&p->serial;
 if(ok){
  if(p->operation==QDI_STATUS)ok=p->value==0;
  else if(p->operation==QDI_SET_NIGHT){ok=p->value<=1;if(ok){s->desired=p->value;s->hold_until=now+3000000;}}
  else if(p->operation==QDI_TRANSITION){ok=p->value>=1&&p->value<=10;if(ok)s->hold_until=now+(uint64_t)p->value*1000000;}
  else ok=0;
 }
 *r=(struct qdi_status){.magic=QDI_MAGIC,.serial=p->serial,.ok=ok,.night=s->night,.pending=s->desired!=s->night,
  .mean=s->mean,.exposure=s->exposure,.gain=s->gain,.settled=s->stable_since&&now>=s->stable_since&&now-s->stable_since>=500000,
  .transition=now<s->hold_until,.stamp=s->stamp,.frames=s->frames};return 1;
}
static int qdi_sample(struct qdi_owner *s,const uint8_t *y,unsigned width,unsigned height,unsigned stride,unsigned exposure,unsigned gain,uint64_t now)
{
 if(!s||!y||width!=1280||height!=720||stride!=1280||exposure<4||exposure>896||gain>24||!now||now<s->stamp)return 0;
 unsigned sum=0;
 for(unsigned row=0;row<12;row++)for(unsigned col=0;col<16;col++)sum+=y[(row*60+30)*stride+col*80+40];
 if(!s->stable_since||s->gain!=gain||s->exposure!=exposure)s->stable_since=now;
 s->mean=sum/192;s->gain=gain;s->exposure=exposure;return 1;
}
static int qdi_frame(struct qdi_owner *s,volatile uint8_t *uv,size_t bytes,uint64_t serial,uint64_t now)
{
 if(!s||!uv||bytes!=1280u*720u/2||serial<=120||!now||now<s->stamp||serial<=s->frames)return 0;
 if(s->desired){
  /* DMA has relinquished this exact plane. Native capture overwrites it
   * again before reuse; returning to day therefore needs no saved copy. */
  volatile uint32_t *words=(volatile uint32_t*)uv;
  if((uintptr_t)uv&3)return 0;
  for(size_t n=0;n<bytes/4;n++)words[n]=0x80808080u;
 }
 s->night=s->desired;s->stamp=now;s->frames=serial;return 1;
}
#endif
