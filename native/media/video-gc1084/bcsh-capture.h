/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QC5_BCSH_CAPTURE_H
#define QC5_BCSH_CAPTURE_H
#include "isp-bcsh.h"
#include "gc1084-bcsh.private.h"
#ifdef QC5_DYNAMIC_BCSH
#include "bcsh-live-plan.h"
#include "gc1084-bcsh-dynamic.private.h"
static struct qbh_state active_bcsh_dynamic;
#endif
static struct qbcsh_state active_bcsh;
static volatile uint32_t *bcsh_isp;
static uint32_t bcsh_read(void *opaque,uint32_t off){return color_read(opaque,off);}
static int bcsh_write(void *opaque,uint32_t off,uint32_t value){color_write(opaque,off,value);return 1;}
static void bcsh_log(const char *stage)
{
#ifndef QC5_FRAME_TEST
 int fd=open("/dev/kmsg",O_WRONLY|O_CLOEXEC);if(fd<0)return;
 char line[240];int n=snprintf(line,sizeof line,"<6>QC5_BCSH stage=%s profile=%08x bank=%u writes=%u frames=%" QC5_FILTER_COUNT_FMT " coefficients=%08x fault=%08x value=%08x\n",
  stage,active_bcsh.profile?active_bcsh.profile->checksum:0,active_bcsh.bank,active_bcsh.writes,active_bcsh.frames,
  active_bcsh.read_hash,active_bcsh.fault_offset,active_bcsh.fault_value);
 if(n>0&&n<(int)sizeof line)(void)write(fd,line,(unsigned)n);close(fd);
#else
 (void)stage;
#endif
}
static int bcsh_prepare(volatile uint32_t *isp,struct qc5_preview *record)
{
 memset(&active_bcsh,0,sizeof active_bcsh);bcsh_isp=0;
#ifdef QC5_DYNAMIC_BCSH
 memset(&active_bcsh_dynamic,0,sizeof active_bcsh_dynamic);
#endif
 if(!color_idle(isp,record)||!qbcsh_load(&active_bcsh,&gc1084_bcsh,0,bcsh_write,(void*)isp))return core_refuse(record,0x7b000,0);
#ifdef QC5_DYNAMIC_BCSH
 const struct qbh_io io={(void*)isp,bcsh_read,bcsh_write};
 if(!qbh_prepare(&active_bcsh_dynamic,&io,&gc1084_bcsh_dynamic_profiles[0]))return core_refuse(record,0x7b01c,0);
 active_bcsh.expected=active_bcsh_dynamic.events.registers;
#ifndef QC5_FRAME_TEST
 int fd=open("/dev/kmsg",O_WRONLY|O_CLOEXEC);
 if(fd>=0){char line[160];int n=snprintf(line,sizeof line,"<6>QC5_BCSH_DYNAMIC_PROFILE checksum=%08x initial_ct=6000 initial_ev=409600\n",QC5_BCSH_DYNAMIC_PROFILE_CHECKSUM);
  if(n>0&&(size_t)n<sizeof line)(void)write(fd,line,(unsigned)n);close(fd);}
#endif
#endif
 bcsh_isp=isp;
 bcsh_log("prepared");return 1;
}
static int bcsh_loaded_for_output(void)
{
#ifdef QC5_DYNAMIC_BCSH
 if(!active_bcsh_dynamic.ready||active_bcsh_dynamic.failed||active_bcsh_dynamic.events.failed||
    active_bcsh.expected!=active_bcsh_dynamic.events.registers||
    active_bcsh_dynamic.events.profile!=&gc1084_bcsh_dynamic_profiles[0])return 0;
#endif
 return bcsh_isp&&active_bcsh.profile==&gc1084_bcsh&&qbcsh_loaded(&active_bcsh);
}
#ifdef QC5_DYNAMIC_BCSH
static void bcsh_dynamic_log(uint32_t ct,uint32_t ev)
{
#ifndef QC5_FRAME_TEST
 int fd=open("/dev/kmsg",O_WRONLY|O_CLOEXEC);
 if(fd>=0){char line[280];const struct qbf_state *s=&active_bcsh_dynamic.events;
  int n=snprintf(line,sizeof line,"<6>QC5_BCSH_DYNAMIC sample=%u ct=%u ev=%u last_ct=%u last_ev=%u updates=%u skipped=%u writes=%u coefficients=%08x\n",
   active_bcsh_dynamic.samples,s->ct,s->ev_q10,s->last_ct,s->last_ev,s->updates,s->skipped,active_bcsh_dynamic.writes,active_bcsh_dynamic.read_hash);
  if(n>0&&(size_t)n<sizeof line)(void)write(fd,line,(unsigned)n);close(fd);}
#endif
 (void)ct;(void)ev;
}
static int bcsh_dynamic_tick(struct qc5_preview *record,uint32_t ct,uint32_t ev)
{
 if(!bcsh_loaded_for_output()||!qbh_tick(&active_bcsh_dynamic,ct,ev))
  return core_refuse(record,active_bcsh_dynamic.fault_offset?active_bcsh_dynamic.fault_offset:0x7b020,active_bcsh_dynamic.fault_value);
 bcsh_dynamic_log(ct,ev);return 1;
}
#endif
static int bcsh_frame_ready(struct qc5_preview *record,qc5_filter_count completed)
{
#ifndef QC5_FRAME_TEST
 if(completed==1&&bcsh_loaded_for_output()){
  char line[320];int at=snprintf(line,sizeof line,"<6>QC5_BCSH_VIEW values=");
  for(unsigned n=0;n<29;n++)at+=snprintf(line+at,sizeof line-(unsigned)at,"%08x",color_read(bcsh_isp,0x8000+4*n));
  int fd=open("/dev/kmsg",O_WRONLY|O_CLOEXEC);
  if(fd>=0){if(at>0&&at+1<(int)sizeof line){line[at++]='\n';(void)write(fd,line,(unsigned)at);}close(fd);}
 }
#endif
 if(!bcsh_loaded_for_output()||(color_read(bcsh_isp,0xc)&0x10000u)||!qbcsh_frame(&active_bcsh,bcsh_read,(void*)bcsh_isp,completed)){
  bcsh_log("refused");return core_refuse(record,active_bcsh.fault_offset?active_bcsh.fault_offset:0x7b004,active_bcsh.fault_value);
 }
 if(completed==1)bcsh_log("enabled");return 1;
}
static int bcsh_finish_frames(volatile uint32_t *isp,struct qc5_preview *record,unsigned stop,qc5_filter_count target)
{
 /* The diagnostic's frame target is selected by its capture image. Check
  * quiescence/counts before finalising the dynamic controller: a refused
  * premature stop must not silently disable subsequent colour updates. */
#ifndef QC5_IMAGE_CONTINUOUS
 if(target!=248)return core_refuse(record,0x7b008,0);
#endif
 if(stop!=31||!target||active_bcsh.frames!=target||!bcsh_loaded_for_output())
  return core_refuse(record,active_bcsh.fault_offset?active_bcsh.fault_offset:0x7b008,active_bcsh.fault_value);
#ifdef QC5_DYNAMIC_BCSH
 if(!qbh_finish(&active_bcsh_dynamic))return core_refuse(record,0x7b024,active_bcsh_dynamic.samples);
#endif
 if(!qbcsh_readback(&active_bcsh,bcsh_read,(void*)isp))
  return core_refuse(record,active_bcsh.fault_offset?active_bcsh.fault_offset:0x7b008,active_bcsh.fault_value);
 bcsh_log("finished");
#ifdef QC5_DYNAMIC_BCSH
 active_bcsh.loaded=0; /* A new stream must prepare fresh checked state. */
#endif
 return 1;
}
static int bcsh_finish(volatile uint32_t *isp,struct qc5_preview *record,unsigned stop)
{
#ifdef QC5_NV12_TARGET
 return bcsh_finish_frames(isp,record,stop,QC5_NV12_TARGET);
#else
 return bcsh_finish_frames(isp,record,stop,248);
#endif
}
#endif
