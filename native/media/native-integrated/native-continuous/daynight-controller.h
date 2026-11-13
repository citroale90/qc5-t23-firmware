/* SPDX-License-Identifier: GPL-3.0-only
 * Full native sequencer. No guessed ISP writes, frame buffers, shell calls,
 * sleeps or hardware access exist here. The adapters must acknowledge each
 * image/filter/light operation. Automatic night->day samples ambient light
 * with IR temporarily OFF, so an IR reflection is not mistaken for daylight.
 */
#ifndef QC5_NATIVE_DAYNIGHT_CONTROLLER_H
#define QC5_NATIVE_DAYNIGHT_CONTROLLER_H
#include <stdint.h>
#include <string.h>
#include "image-mode.h"
enum {QND_DAY=0,QND_NIGHT=1,QND_AUTO=2};
enum {QND_IDLE=0,QND_IMAGE=1,QND_FILTER_START=2,QND_FILTER=3,QND_PROBE=4,QND_FAILED=5};
struct qnd_io {
 void *opaque;
 int (*meter)(void *,struct qdi_status *);
 int (*image)(void *,unsigned night);
 int (*hold)(void *,unsigned seconds);
 int (*filter_start)(void *,unsigned night); /*1 accepted,0 busy,-1 failed*/
 int (*filter_done)(void *); /*1 acknowledged,0 busy,-1 failed*/
 int (*lights)(void *,unsigned mode); /*0 off,1 white,2 IR; checked readback*/
};
struct qnd_state {
 struct qnd_io io;struct qdi_status meter;
 uint64_t now,phase_since,deadline,switched,last_probe,candidate,bright_since;
 unsigned mode,white,known,night,applied_white,target,target_white,phase,error;
};
static int qnd_init(struct qnd_state *s,struct qnd_io io)
{
 if(!s||!io.meter||!io.image||!io.hold||!io.filter_start||!io.filter_done||!io.lights)return 0;
 memset(s,0,sizeof *s);s->io=io;s->mode=QND_AUTO;return 1;
}
static int qnd_request(struct qnd_state *s,unsigned mode,unsigned white)
{
 if(!s||mode>QND_AUTO||white>1||(white&&mode!=QND_DAY))return 0;
 s->mode=mode;s->white=white;s->candidate=s->bright_since=0;return 1;
}
static int qnd_fail(struct qnd_state *s,unsigned error,uint64_t now)
{
 (void)s->io.lights(s->io.opaque,0);(void)s->io.hold(s->io.opaque,3);
 s->known=0;s->error=error;s->phase=QND_FAILED;s->deadline=now+10000000;return 0;
}
static int qnd_fresh(const struct qdi_status *m,uint64_t now)
{return m->ok&&m->magic==QDI_MAGIC&&m->stamp&&m->stamp<=now&&now-m->stamp<=2000000&&m->frames>120&&m->mean<=255&&m->exposure>=4&&m->exposure<=896&&m->gain<=24&&m->night<=1&&m->pending<=1&&m->settled<=1;}
static unsigned qnd_darkness(const struct qdi_status *m)
{
 /* These are sensor exposure/gain-index thresholds, not invented lux.
  * Daylight requires low exposure as well as a populated luminance sample. */
 if(m->exposure>=800&&m->gain>=8)return 100;
 if(m->exposure<=224&&m->gain<=2&&m->mean>=48)return 0;
 return 50;
}
static int qnd_begin(struct qnd_state *s,unsigned night,unsigned white,uint64_t now)
{
 if(!s->io.hold(s->io.opaque,5)||!s->io.lights(s->io.opaque,0)||!s->io.image(s->io.opaque,night))return qnd_fail(s,1,now);
 s->target=night;s->target_white=white;s->phase=QND_IMAGE;s->phase_since=now;s->deadline=now+5000000;s->candidate=0;return 1;
}
static int qnd_tick(struct qnd_state *s,uint64_t now)
{
 if(!s||!now||now<s->now||now>UINT64_MAX-60000000)return 0;s->now=now;
 if(s->phase==QND_FAILED){if(now<s->deadline)return 1;s->phase=QND_IDLE;}
 struct qdi_status meter={0};int fresh=s->io.meter(s->io.opaque,&meter)&&qnd_fresh(&meter,now);
 if(fresh)s->meter=meter;
 if(s->phase==QND_IMAGE){
  if(now>=s->deadline)return qnd_fail(s,2,now);
  if(fresh&&!meter.pending&&meter.night==s->target&&meter.stamp>=s->phase_since){s->phase=QND_FILTER_START;s->phase_since=now;}
  return 1;
 }
 if(s->phase==QND_FILTER_START){
  int started=s->io.filter_start(s->io.opaque,s->target);
  if(started<0)return qnd_fail(s,3,now);
  if(started){s->phase=QND_FILTER;s->deadline=now+5000000;}
  else if(now-s->phase_since>=30000000)return qnd_fail(s,4,now);
  return 1;
 }
 if(s->phase==QND_FILTER){
  int done=s->io.filter_done(s->io.opaque);
  if(done<0||now>=s->deadline)return qnd_fail(s,5,now);
  if(done){
   unsigned mode=s->target?2:s->target_white?1:0;
   if(!s->io.lights(s->io.opaque,mode)||!s->io.hold(s->io.opaque,3))return qnd_fail(s,6,now);
   s->night=s->target;s->applied_white=s->target_white;s->known=1;s->phase=QND_IDLE;s->switched=s->last_probe=now;s->error=0;
  }return 1;
 }
 if(s->phase==QND_PROBE){
  /* A manual command cancels a probe by entering its proper full sequence. */
  if(s->mode!=QND_AUTO||s->white)return qnd_begin(s,s->mode==QND_NIGHT,s->white,now);
  if(now>=s->phase_since+3000000&&fresh&&meter.stamp>=s->phase_since+3000000&&meter.settled){
   if(qnd_darkness(&meter)==0){
    if(!s->bright_since)s->bright_since=now;
    else if(now-s->bright_since>=1000000)return qnd_begin(s,0,0,now);
   }else s->bright_since=0;
  }
  if(now>=s->deadline){if(!s->io.lights(s->io.opaque,2)||!s->io.hold(s->io.opaque,3))return qnd_fail(s,7,now);s->phase=QND_IDLE;s->bright_since=0;}
  return 1;
 }
 unsigned wanted=s->mode==QND_AUTO?(s->known?s->night:0):s->mode;
 if(!s->known||wanted!=s->night||s->white!=s->applied_white){if(fresh)return qnd_begin(s,wanted,s->white,now);return 1;}
 if(s->mode!=QND_AUTO||s->white||!fresh||!meter.settled||meter.transition){s->candidate=0;return 1;}
 if(now-s->switched<30000000)return 1;
 if(!s->night){
  if(qnd_darkness(&meter)!=100){s->candidate=0;return 1;}
  if(!s->candidate)s->candidate=now;
  else if(now-s->candidate>=5000000)return qnd_begin(s,1,0,now);
 }else if(now-s->last_probe>=60000000){
  if(!s->io.hold(s->io.opaque,5)||!s->io.lights(s->io.opaque,0))return qnd_fail(s,8,now);
  s->phase=QND_PROBE;s->phase_since=s->last_probe=now;s->deadline=now+5000000;s->bright_since=0;
 }
 return 1;
}
static unsigned qnd_busy(const struct qnd_state *s)
{return s&&s->phase!=QND_IDLE&&s->phase!=QND_FAILED;}
#endif
