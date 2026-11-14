/* SPDX-License-Identifier: GPL-2.0-only
 * Native calibration/absolute move owner. Calibration uses the OEM full-span
 * hard-stop convention, NOT a measured encoder reference. Presets contain
 * steps only; actual position is deliberately unknown after process/reboot.
 */
#ifndef QC5_PTZ_CONTROLLER_H
#define QC5_PTZ_CONTROLLER_H
#include <stdint.h>
#include <string.h>
#include "../../../../drivers/ptz/peripheral-abi.h"
#include "../../../../drivers/ptz/move-progress.h"
#include "ptz-position.h"
#include "homing-plan.h"
#define QPT_PRESETS 32
struct qpt_preset {char name[21];uint16_t x,y;};
struct qpt_io {void *opaque;int (*read)(void *,struct qcm_status *,struct qcm_progress *);int (*submit)(void *,const struct qcm_request *);};
enum {QPT_IDLE=0,QPT_HOME=1,QPT_GOTO=2,QPT_MANUAL=3,QPT_STOP=4};
struct qpt_controller {
 struct qpt_io io;struct qps_position position;struct qpt_preset preset[QPT_PRESETS];
 struct qcm_status hw;struct qcm_progress progress;
 unsigned action,axis,waiting,initializing,generation,homed,error,manual_axis,manual_direction,manual_steps,home_phase;
 unsigned target[2];uint64_t deadline;
};
static int qpt_init(struct qpt_controller *s,struct qpt_io io)
{
 if(!s||!io.read||!io.submit)return 0;memset(s,0,sizeof *s);s->io=io;return qps_init(&s->position,8000,2500);
}
static void qpt_unknown(struct qpt_controller *s)
{s->homed=0;s->position.axis[0].known=s->position.axis[1].known=0;}
static int qpt_fail(struct qpt_controller *s,unsigned code,uint64_t now)
{
 s->error=code;s->action=QPT_IDLE;s->waiting=s->initializing=0;qpt_unknown(s);
 struct qcm_request stop={.command=QCM_STOP};
 if(s->io.submit(s->io.opaque,&stop)){
  /* A queued Stop still owns the actuator. Do not advertise idle until the
   * kernel has acknowledged it, even when the preceding move failed. */
  s->action=QPT_STOP;s->waiting=1;s->hw.busy=1;s->deadline=now+23000000;
 }return 0;
}
static int qpt_send(struct qpt_controller *s,struct qcm_request p,uint64_t now)
{
 if(!s->io.submit(s->io.opaque,&p))return qpt_fail(s,1,now);
 s->waiting=1;s->generation=s->progress.generation+1;s->deadline=now+23000000;return 1;
}
static int qpt_tick(struct qpt_controller *s,uint64_t now)
{
 if(!s||!now||now>UINT64_MAX-23000000)return 0;
 if(!s->io.read(s->io.opaque,&s->hw,&s->progress))return qpt_fail(s,2,now);
 if(s->action==QPT_IDLE)return 1;
 if(now>s->deadline)return qpt_fail(s,3,now);
 if(s->hw.busy)return 1;
 if(s->hw.last_error)return qpt_fail(s,4,now);
 if(s->initializing){
  if(!s->hw.initialized)return qpt_fail(s,5,now);
  s->initializing=s->waiting=0;s->progress.generation=0;
 }
 else if(s->waiting){
  if(s->action==QPT_STOP){s->waiting=0;s->action=QPT_IDLE;return 1;}
  struct qhc_step home={0};unsigned endpoint=0;
  if(s->action==QPT_HOME){
   if(!qhc_plan(s->home_phase,s->position.axis[0].maximum,s->position.axis[1].maximum,&home)||
      home.axis!=s->axis||home.steps!=s->progress.requested)return qpt_fail(s,12,now);
   endpoint=home.endpoint;
  }
  if(s->progress.version!=QCM_PROGRESS_VERSION||s->progress.generation!=s->generation||
     s->progress.axis!=s->axis||s->progress.active||!s->progress.valid||s->progress.steps>32764||(s->progress.steps&3)||
     s->progress.reason!=(endpoint?QMP_HOME_COMPLETE:QMP_COMPLETE)||
     (endpoint?s->progress.steps<s->progress.requested:s->progress.steps!=s->progress.requested))return qpt_fail(s,6,now);
  struct qps_axis *a=&s->position.axis[s->axis];
  if(s->action==QPT_HOME)a->position=home.position;
  else if(s->action==QPT_GOTO){a->position=s->target[s->axis];a->known=1;}
  else if(s->action==QPT_MANUAL&&a->known){
   unsigned n=s->progress.steps;if(s->manual_direction){if(n>a->position)return qpt_fail(s,7,now);a->position-=n;}
   else{if(n>a->maximum-a->position)return qpt_fail(s,7,now);a->position+=n;}
  }
  s->waiting=0;
  if(s->action==QPT_MANUAL){s->action=QPT_IDLE;return 1;}
  if(s->action==QPT_HOME)s->home_phase++;else s->axis++;
 }
 if(s->action==QPT_HOME){
  if(s->home_phase==QHC_PHASES){
   s->position.axis[0].known=s->position.axis[1].known=1;
   s->homed=1;s->action=QPT_IDLE;return 1;
  }
  struct qhc_step h;
  if(!qhc_plan(s->home_phase,s->position.axis[0].maximum,s->position.axis[1].maximum,&h))return qpt_fail(s,12,now);
  s->axis=h.axis;
  struct qcm_request p={.command=h.endpoint?QCM_HOME:QCM_HOME_CENTER,.axis=h.axis,.direction=h.direction,.value=h.steps};
  return qpt_send(s,p,now);
 }
 while(s->axis<2){
  struct qps_axis *a=&s->position.axis[s->axis];struct qcm_request p={.axis=s->axis};
  if(s->action==QPT_MANUAL){p.command=QCM_JOG;p.axis=s->manual_axis;p.direction=s->manual_direction;p.value=s->manual_steps;s->axis=p.axis;}
  else if(s->action==QPT_GOTO){
   unsigned target=s->target[s->axis];if(target==a->position){s->axis++;continue;}
   p.command=QCM_MOVE;p.direction=target<a->position;p.value=p.direction?a->position-target:target-a->position;
  }else return qpt_fail(s,8,now);
  return qpt_send(s,p,now);
 }
 s->action=QPT_IDLE;return 1;
}
static int qpt_home(struct qpt_controller *s,uint64_t now)
{
 if(!s||s->action!=QPT_IDLE||!now||now>UINT64_MAX-23000000)return 0;
 if(!s->io.read(s->io.opaque,&s->hw,&s->progress)||s->hw.busy)return 0;
 qpt_unknown(s);s->error=0;s->action=QPT_HOME;s->axis=0;s->home_phase=0;s->initializing=s->waiting=1;s->deadline=now+23000000;
 struct qcm_request init={.command=QCM_INITIALIZE};return s->io.submit(s->io.opaque,&init)?1:qpt_fail(s,9,now);
}
static int qpt_goto(struct qpt_controller *s,unsigned x,unsigned y,uint64_t now)
{
 if(!s||!s->homed||s->action!=QPT_IDLE||x>8000||y>2500||(x&3)||(y&3)||!now||now>UINT64_MAX-23000000)return 0;
 s->target[0]=x;s->target[1]=y;s->axis=0;s->action=QPT_GOTO;s->error=0;s->deadline=now+23000000;return qpt_tick(s,now);
}
static int qpt_stop(struct qpt_controller *s,uint64_t now)
{
 if(!s||!now||now>UINT64_MAX-23000000)return 0;
 struct qcm_request stop={.command=QCM_STOP};qpt_unknown(s);s->action=QPT_STOP;s->waiting=1;s->initializing=0;s->deadline=now+23000000;
 return s->io.submit(s->io.opaque,&stop)?1:qpt_fail(s,10,now);
}
static int qpt_jog(struct qpt_controller *s,unsigned axis,unsigned direction,uint64_t now)
{
 if(!s||axis>1||direction>1||s->action!=QPT_IDLE||!now||now>UINT64_MAX-23000000)return 0;
 if(!s->io.read(s->io.opaque,&s->hw,&s->progress)||s->hw.busy)return 0;
 unsigned steps=256;
 if(s->homed){struct qps_axis *a=&s->position.axis[axis];unsigned left=direction?a->position:a->maximum-a->position;if(steps>left)steps=left;if(!steps)return 1;}
 s->action=QPT_MANUAL;s->axis=s->manual_axis=axis;s->manual_direction=direction;s->manual_steps=steps;s->error=0;s->deadline=now+23000000;
 if(!s->hw.initialized||s->hw.last_error||(s->progress.generation&&(!s->progress.valid||s->progress.reason!=QMP_COMPLETE||s->progress.active))){qpt_unknown(s);s->initializing=s->waiting=1;struct qcm_request init={.command=QCM_INITIALIZE};return s->io.submit(s->io.opaque,&init)?1:qpt_fail(s,11,now);}
 return qpt_tick(s,now);
}
static int qpt_name(const char *name)
{
 if(!name||!*name||strlen(name)>20)return 0;
 for(const unsigned char *p=(const unsigned char*)name;*p;p++)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='_'||*p=='-'))return 0;return 1;
}
static int qpt_slot(const struct qpt_controller *s,const char *name)
{if(!s||!qpt_name(name))return -1;for(unsigned n=0;n<QPT_PRESETS;n++)if(!strcmp(s->preset[n].name,name))return (int)n;return -1;}
static int qpt_save(struct qpt_controller *s,const char *name)
{
 if(!s||!s->homed||s->action!=QPT_IDLE||!qpt_name(name))return 0;
 int slot=qpt_slot(s,name);if(slot<0)for(unsigned n=0;n<QPT_PRESETS;n++)if(!s->preset[n].name[0]){slot=(int)n;break;}if(slot<0)return 0;
 struct qpt_preset p={.x=s->position.axis[0].position,.y=s->position.axis[1].position};strcpy(p.name,name);s->preset[slot]=p;return 1;
}
static int qpt_recall(struct qpt_controller *s,const char *name,uint64_t now)
{int slot=qpt_slot(s,name);return slot>=0&&qpt_goto(s,s->preset[slot].x,s->preset[slot].y,now);}
static int qpt_delete(struct qpt_controller *s,const char *name)
{int slot=qpt_slot(s,name);if(slot<0||s->action!=QPT_IDLE)return 0;memset(&s->preset[slot],0,sizeof s->preset[slot]);return 1;}
#endif
