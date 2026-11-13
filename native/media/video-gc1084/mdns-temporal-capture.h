/* SPDX-License-Identifier: GPL-2.0-or-later
 * Native-calibrated MDNS with an independently validated reserved buffer.
 * Every enable is bounded by address/coefficient checks; stop is explicit.
 */
#ifndef QC5_MDNS_TEMPORAL_CAPTURE_H
#define QC5_MDNS_TEMPORAL_CAPTURE_H
#include "mdns-memory-map.h"
#include "mdns-reference.h"
#include "gc1084-mdns.private.h"
#ifdef QC5_NV12_AUDIT
#include "nv12-audit.h"
#endif
#ifdef QC5_MDNS_STROBE
#include "mdns-strobe.h"
static struct qc5_mdns_strobe active_mdns_strobe;
#endif
#ifdef QC5_MDNS_DEFERRED
#include "mdns-deferred.h"
static struct qc5_mdns_deferred active_mdns_deferred;
#endif
static struct qc5_mdns_reference active_mdns_reference;
static struct qc5_mdns_state active_mdns;
#ifdef QC5_MDNS_DRAIN
#include "mdns-drain.h"
static struct qc5_mdns_drain active_mdns_drain;
#endif
#ifdef QC5_MDNS_READBACK
#include "mdns-view-note.h"
static unsigned mdns_view_logged;
static int mdns_temporal_view(void)
{
 if(mdns_view_logged)return 1;
 if(!active_mdns_reference.prepared||!active_mdns_reference.armed||!active_mdns_reference.read)return 0;
#ifndef QC5_FRAME_TEST
 int fd=open("/dev/kmsg",O_WRONLY|O_CLOEXEC);if(fd<0)return 0;
 /* Six writes stay below the per-open /dev/kmsg default burst of ten. */
 for(unsigned first=0;first<QC5_MDNS_READ;first+=32){
  uint32_t values[32];unsigned count=QC5_MDNS_READ-first;if(count>32)count=32;
  for(unsigned n=0;n<count;n++)values[n]=active_mdns_reference.read(active_mdns_reference.opaque,qc5_mdns_read_offsets[first+n]);
  char text[QC5_MDNS_VIEW_LINE];unsigned length=qc5_mdns_view_line(text,first,values,count);
  if(!length||write(fd,text,length)!=(ssize_t)length){close(fd);return 0;}
 }
 close(fd);
#endif
 mdns_view_logged=1;return 1;
}
#endif
static uint32_t mdns_mapping_stage,mdns_finished,mdns_fault_offset,mdns_fault_value;
#ifndef QC5_MDNS_TEMPORAL_MAP
#define QC5_MDNS_TEMPORAL_MAP qc5_mdns_map_authorized
#endif
static uint32_t mdns_temporal_read(void *opaque,uint32_t off){return color_read(opaque,off);}
static int mdns_temporal_write(void *opaque,uint32_t off,uint32_t value)
{
 /* Current GC1084 coefficient and bypass oracles only issue refresh 0x21.
  * A status view must never be echoed back as a command. */
 if(off==0x7818&&value!=0x21)return 0;
 color_write(opaque,off,value);return 1;
}
static void mdns_temporal_log(const char *stage)
{
#ifndef QC5_FRAME_TEST
 int fd=open("/dev/kmsg",O_WRONLY|O_CLOEXEC);if(fd<0)return;
 char text[384];int n=snprintf(text,sizeof text,
  "<6>QC5_MDNS_TEMPORAL stage=%s map=%u profile=%08x gain=%u writes=%" QC5_MDNS_COUNT_FMT " updates=%" QC5_MDNS_COUNT_FMT " arms=%" QC5_MDNS_COUNT_FMT " config=%08x coefficients=%08x data=%08x changed=%u control=%08x fault=%08x value=%08x"
#ifdef QC5_MDNS_STROBE
  " active=%08x stopped=%u stable=%u"
#endif
  "\n",
  stage,mdns_mapping_stage,active_mdns.profile?active_mdns.profile->checksum:0,active_mdns.gain,
  active_mdns.writes,active_mdns.updates,active_mdns_reference.arms,active_mdns_reference.config_hash,
  active_mdns.read_hash,active_mdns_reference.data_hash,active_mdns_reference.changed_bytes,
  active_mdns_reference.read?active_mdns_reference.read(active_mdns_reference.opaque,0x7804):0,
  mdns_fault_offset,mdns_fault_value
#ifdef QC5_MDNS_STROBE
  ,active_mdns_strobe.active_hash,active_mdns_strobe.stopped,active_mdns_strobe.stable_data
#endif
  );
 if(n>0&&n<(int)sizeof text)(void)write(fd,text,(unsigned)n);close(fd);
#else
 (void)stage;
#endif
}
static int mdns_temporal_refuse(struct qc5_preview *record,uint32_t fallback,uint32_t value)
{
 mdns_fault_offset=active_mdns.fault_offset?active_mdns.fault_offset:
  active_mdns_reference.fault_offset?active_mdns_reference.fault_offset:fallback;
 mdns_fault_value=active_mdns.fault_offset?active_mdns.fault_value:
  active_mdns_reference.fault_offset?active_mdns_reference.fault_value:value;
 mdns_temporal_log("refused");return core_refuse(record,mdns_fault_offset,mdns_fault_value);
}
static int mdns_temporal_prepare(volatile uint32_t *isp,struct qc5_preview *record)
{
 if(active_mdns_reference.prepared||mdns_finished||!color_idle(isp,record))return 0;
 struct qc5_mdns_layout plan;
 if(!qc5_mdns_plan(QC5_MDNS_BASE,1280,720,0,1,&plan))return mdns_temporal_refuse(record,0x7c000,0);
 volatile uint8_t *memory=QC5_MDNS_TEMPORAL_MAP(&mdns_mapping_stage);
 if(memory==MAP_FAILED)return mdns_temporal_refuse(record,0x7c004,mdns_mapping_stage);
 if(!qc5_mdns_reference_prepare(&active_mdns_reference,&plan,memory,QC5_MDNS_REFERENCE_BYTES,
    mdns_temporal_read,mdns_temporal_write,(void*)isp))return mdns_temporal_refuse(record,0x7c008,0);
 if(!qc5_mdns_load(&active_mdns,&gc1084_mdns,0,mdns_temporal_write,(void*)isp)||
    !qc5_mdns_gain(&active_mdns,QC5_NV12_INITIAL_GAIN,mdns_temporal_write,(void*)isp)
#ifndef QC5_MDNS_STROBE
    ||!qc5_mdns_verify(&active_mdns,mdns_temporal_read,(void*)isp)
#endif
    )return mdns_temporal_refuse(record,0x7c00c,QC5_NV12_INITIAL_GAIN);
 if(!color_idle(isp,record))return 0;
 mdns_temporal_log("prepared");return 1;
}
static int mdns_temporal_start(struct qc5_preview *record)
{
 if(!active_mdns.ready||active_mdns.gain!=QC5_NV12_INITIAL_GAIN||active_mdns_reference.ever_armed||
#ifdef QC5_MDNS_DEFERRED
    !qc5_mdns_deferred_queue(&active_mdns_deferred,&active_mdns_strobe,&active_mdns_reference,&active_mdns)
#elif defined(QC5_MDNS_STROBE)
    !qc5_mdns_strobe_commit(&active_mdns_strobe,&active_mdns_reference,&active_mdns)
#else
    !qc5_mdns_reference_arm(&active_mdns_reference,&active_mdns)
#endif
    )return mdns_temporal_refuse(record,0x7c010,active_mdns.gain);
#ifdef QC5_MDNS_DEFERRED
 mdns_temporal_log("committed");
#else
 mdns_temporal_log("enabled");
#endif
 return 1;
}
static int mdns_temporal_gain(volatile uint32_t *isp,struct qc5_preview *record,unsigned gain)
{
 if(gain>=25||!active_mdns.ready||!active_mdns_reference.armed||mdns_finished)return mdns_temporal_refuse(record,0x7c014,gain);
 if(gain==active_mdns.gain){
  uint32_t control=color_read(isp,0x7804);
#ifdef QC5_MDNS_STROBE
  if((control&~1u)!=0x110)return mdns_temporal_refuse(record,0x7804,control);
#else
  if(control!=0x111)return mdns_temporal_refuse(record,0x7804,control);
#endif
  return 1;
 }
 if(!qc5_mdns_gain(&active_mdns,gain,mdns_temporal_write,(void*)isp)||
#ifdef QC5_MDNS_DEFERRED
    !qc5_mdns_deferred_queue(&active_mdns_deferred,&active_mdns_strobe,&active_mdns_reference,&active_mdns)
#elif defined(QC5_MDNS_STROBE)
    !qc5_mdns_strobe_commit(&active_mdns_strobe,&active_mdns_reference,&active_mdns)
#else
    !qc5_mdns_reference_arm(&active_mdns_reference,&active_mdns)
#endif
    )return mdns_temporal_refuse(record,0x7c018,gain);
 return 1;
}
#ifdef QC5_MDNS_GATE
static int mdns_temporal_output_ready(void)
{
 return active_mdns_reference.prepared&&active_mdns_reference.armed&&active_mdns_reference.ever_armed&&
  !active_mdns_reference.finished&&active_mdns.ready&&
#ifdef QC5_MDNS_DEFERRED
  (active_mdns.read_hash||active_mdns_deferred.pending)&&
#else
  active_mdns.read_hash&&
#endif
  qc5_mdns_reference_readback(&active_mdns_reference)&&
  (mdns_temporal_read(active_mdns_reference.opaque,0x7804)&~1u)==0x110;
}
#endif
#ifdef QC5_MDNS_DEFERRED
static int mdns_temporal_frame_ready(struct qc5_preview *record,qc5_mdns_count completed)
{
#ifdef QC5_MDNS_READBACK
 if(!mdns_temporal_view())return mdns_temporal_refuse(record,0x7c028,0);
#endif
#ifndef QC5_FRAME_TEST
 if(!active_mdns_deferred.first_confirmed){
  int fd=open("/dev/kmsg",O_WRONLY|O_CLOEXEC);
  if(fd>=0){char text[128];int n=snprintf(text,sizeof text,"<6>QC5_MDNS_COMMAND_STATUS raw=%08x controlled_mask=00000020 command=00000021\n",mdns_temporal_read(active_mdns_reference.opaque,0x7818));
   if(n>0&&n<(int)sizeof text)(void)write(fd,text,(unsigned)n);close(fd);}
 }
#endif
 if(!qc5_mdns_deferred_confirm(&active_mdns_deferred,&active_mdns_reference,&active_mdns,completed))
  return mdns_temporal_refuse(record,0x7c024,active_mdns.gain);
 if(!active_mdns_deferred.first_confirmed){active_mdns_deferred.first_confirmed=1;mdns_temporal_log("enabled");}
 return 1;
}
#endif
/* The reference remains mapped and owned during every stop failure. */
#ifdef QC5_MDNS_DRAIN
static int mdns_temporal_drain(void)
{
 if(!active_mdns_reference.prepared||!active_mdns_reference.ever_armed)return 1;
 if(active_mdns_deferred.pending||!active_mdns_deferred.first_confirmed||
    !qc5_mdns_drain_begin(&active_mdns_drain,&active_mdns_reference,&active_mdns)){
  mdns_fault_offset=active_mdns.fault_offset?active_mdns.fault_offset:0x7c02c;
  mdns_fault_value=active_mdns.fault_value;
  mdns_temporal_log("refused");return 0;
 }
 mdns_temporal_log("draining");
 for(unsigned n=0;n<200;n++){
  int state=qc5_mdns_drain_poll(&active_mdns_drain,&active_mdns_reference,&active_mdns);
  if(state>0){mdns_temporal_log("drained");return 1;}
  if(state<0)break;
  delay_us(1000);
 }
 mdns_fault_offset=active_mdns.fault_offset?active_mdns.fault_offset:0x7c030;
 mdns_fault_value=active_mdns.fault_value;
 mdns_temporal_log("refused");return 0;
}
#endif
static int mdns_temporal_stop(void)
{
 if(!active_mdns_reference.prepared)return 1;
 if(!active_mdns_reference.ever_armed)return !(mdns_temporal_read(active_mdns_reference.opaque,0x7804)&1u);
#ifdef QC5_MDNS_DRAIN
 return qc5_mdns_drain_stop(&active_mdns_drain,&active_mdns_strobe,&active_mdns_reference,&active_mdns);
#elif defined(QC5_MDNS_STROBE)
 return qc5_mdns_strobe_stop(&active_mdns_strobe,&active_mdns_reference,&active_mdns);
#else
 return qc5_mdns_reference_stop(&active_mdns_reference);
#endif
}
static int mdns_temporal_finish(volatile uint32_t *isp,struct qc5_preview *record,unsigned gain,unsigned stop)
{
 (void)isp;
 if(active_mdns.profile!=&gc1084_mdns||active_mdns.gain!=gain||mdns_finished||
#ifdef QC5_MDNS_DEFERRED
    active_mdns_deferred.pending||!active_mdns_deferred.first_confirmed||active_mdns_deferred.confirmed!=active_mdns_reference.arms||
#endif
    active_mdns.updates!=active_mdns_reference.arms||!qc5_mdns_write_count_matches(active_mdns_reference.arms,active_mdns.writes)||
#ifdef QC5_MDNS_STROBE
    !qc5_mdns_strobe_finish(&active_mdns_strobe,&active_mdns_reference,&active_mdns,stop)
#else
    !qc5_mdns_reference_finish(&active_mdns_reference,&active_mdns,stop)
#endif
    )return mdns_temporal_refuse(record,0x7c01c,gain);
 mdns_temporal_log("finished");
#ifdef QC5_NV12_AUDIT
 qnv_audit_image("mdns",0,active_mdns_reference.memory+(QC5_MDNS_BASE-QC5_MDNS_RESERVE_START),
  active_mdns_reference.memory+(QC5_MDNS_BASE-QC5_MDNS_RESERVE_START)+1280u*720u);
#endif
#ifndef QC5_FRAME_TEST
 if(munmap((void*)active_mdns_reference.memory,QC5_MDNS_REFERENCE_BYTES))return mdns_temporal_refuse(record,0x7c020,(uint32_t)errno);
#endif
 active_mdns_reference.memory=NULL;mdns_finished=1;return 1;
}
#endif
