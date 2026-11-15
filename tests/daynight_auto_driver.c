/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include "daynight-controller.h"
struct fake {uint64_t now;struct qdi_status m;unsigned wanted,lights,filter,fail,freeze,ops;};
static int meter(void *v,struct qdi_status *m){struct fake*f=v;*m=f->m;m->night=f->wanted;if(!f->freeze)m->stamp=f->now;return !f->fail;}
static int image(void*v,unsigned n){struct fake*f=v;f->wanted=n;f->ops++;return 1;}
static int hold(void*v,unsigned n){(void)v;return n>=1&&n<=10;}
static int filter(void*v,unsigned n){struct fake*f=v;f->filter=n;f->ops++;return 1;}
static int done(void*v){(void)v;return 1;}
static int lights(void*v,unsigned n){struct fake*f=v;f->lights=n;f->ops++;return 1;}
static void tick(struct qnd_state*s,struct fake*f){f->now+=250000;assert(qnd_tick(s,f->now));}
static void ticks(struct qnd_state*s,struct fake*f,unsigned n){while(n--)tick(s,f);}
static void settled(struct qnd_state*s,struct fake*f,unsigned night){for(unsigned n=0;n<60;n++){tick(s,f);if(s->known&&s->night==night&&s->phase==QND_IDLE)return;}assert(0);}
static void init(struct qnd_state*s,struct fake*f,unsigned manual_night){
 *f=(struct fake){.now=1000000,.m={.magic=QDI_MAGIC,.ok=1,.frames=200,.mean=125,.exposure=450,.gain=6,.settled=0}};
 assert(qnd_init(s,(struct qnd_io){f,meter,image,hold,filter,done,lights}));
 assert(qnd_request(s,manual_night?QND_NIGHT:QND_DAY,0));settled(s,f,manual_night);
}
int main(void){
 struct qnd_state s;struct fake f;
 /* Exact R48 failure trace: 450 lines, gain6, mean125, manual Night->Auto.
  * Ambient probe must start immediately, despite fresh manual dwell. */
 init(&s,&f,1);assert(qnd_request(&s,QND_AUTO,0));tick(&s,&f);assert(s.phase==QND_PROBE&&f.lights==0&&s.night);
 uint64_t since=f.now;for(unsigned n=0;n<25&&s.night;n++){f.m.gain=6+(n&1);tick(&s,&f);}settled(&s,&f,0);assert(f.now-since<8000000&&!f.lights&&!f.filter);
 /* Explicit Auto in true darkness never forces a day/image/filter trial. */
 init(&s,&f,1);f.m.gain=24;f.m.mean=20;unsigned ops=f.ops;assert(qnd_request(&s,QND_AUTO,0));ticks(&s,&f,35);assert(s.night&&s.phase==QND_IDLE&&f.lights==2&&f.filter==1&&f.ops==ops+2);
 /* Reachable dark detection at the real450-line exposure ceiling. */
 init(&s,&f,0);f.m.gain=16;f.m.mean=90;assert(qnd_request(&s,QND_AUTO,0));ticks(&s,&f,20);assert(!s.night);settled(&s,&f,1);assert(f.lights==2&&f.filter==1);
 /* Marginal light retains day: a gain-code oscillation is not a reason to flip. */
 init(&s,&f,0);assert(qnd_request(&s,QND_AUTO,0));for(unsigned n=0;n<240;n++){f.m.gain=14+(n&1);tick(&s,&f);}assert(!s.night&&!f.lights);
 /* Repeated Auto requests cannot postpone the running probe's decision. */
 init(&s,&f,1);assert(qnd_request(&s,QND_AUTO,0));for(unsigned n=0;n<25&&s.night;n++){assert(qnd_request(&s,QND_AUTO,0));tick(&s,&f);}settled(&s,&f,0);
 /* Manual Night cancels a bright probe through acknowledged sequencing. */
 init(&s,&f,1);assert(qnd_request(&s,QND_AUTO,0));tick(&s,&f);assert(s.phase==QND_PROBE);assert(qnd_request(&s,QND_NIGHT,0));settled(&s,&f,1);assert(f.lights==2&&f.filter==1);
 /* A stale/frozen frame cannot accumulate a bright decision. */
 init(&s,&f,1);assert(qnd_request(&s,QND_AUTO,0));tick(&s,&f);f.freeze=1;f.m.stamp=f.now;ticks(&s,&f,35);assert(s.night&&f.lights==2);
 /* Periodic probes still distinguish real daylight from IR reflections. */
 init(&s,&f,1);assert(qnd_request(&s,QND_AUTO,0));f.m.gain=24;f.m.mean=20;ticks(&s,&f,35);assert(s.night);
 for(unsigned n=0;n<250&&s.phase!=QND_PROBE;n++)tick(&s,&f);assert(s.phase==QND_PROBE&&!f.lights);f.m.gain=6;f.m.mean=125;settled(&s,&f,0);
 /* Explicit Auto gives the same result from either manual mode in the band. */
 init(&s,&f,1);f.m.gain=12;assert(qnd_request(&s,QND_AUTO,0));settled(&s,&f,0);
 /* Periodic automatic probes retain hysteresis in that same band. */
 init(&s,&f,1);assert(qnd_request(&s,QND_AUTO,0));f.m.gain=24;f.m.mean=20;ticks(&s,&f,35);for(unsigned n=0;n<250&&s.phase!=QND_PROBE;n++)tick(&s,&f);assert(s.phase==QND_PROBE);f.m.gain=12;f.m.mean=125;ticks(&s,&f,35);assert(s.night&&f.lights==2);
 puts("DAYNIGHT_AUTO_PASS cases=10 live_450_line_trace=1 dark_retains_night=1 two_stop_hysteresis=1 AE_jitter=1 no_blind_day=1 stale_rejected=1");
}
