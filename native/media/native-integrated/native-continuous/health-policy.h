/* SPDX-License-Identifier: GPL-3.0-only
 * Production can run continuously. A RAM trial uses an absolute boot-time
 * deadline which cannot be extended by restarting this helper. Fresh local
 * services matter; loss of Internet/Wi-Fi connectivity is NOT a reboot cause.
 */
#ifndef QC5_CONTINUOUS_HEALTH_H
#define QC5_CONTINUOUS_HEALTH_H
#include <stdint.h>
#include <string.h>
enum {QCH_STOP=-1,QCH_WAIT=0,QCH_FEED=1};
struct qch_observation {uint64_t now,stamp,frames,frontend_stamp,frontend_seq,network_stamp,network_seq;};
struct qch_state {uint64_t began,last_now,deadline,last_fresh,last_feed,frames,stamp,front,network;unsigned seen,pending,stopped;struct qch_observation proposed;};
static int qch_begin(struct qch_state *s,uint64_t now,uint64_t absolute_deadline)
{if(!s||!now||(absolute_deadline&&(absolute_deadline>120000000||now>=absolute_deadline)))return 0;memset(s,0,sizeof *s);s->began=s->last_now=now;s->deadline=absolute_deadline;return 1;}
static int qch_stop(struct qch_state *s)
{if(s){s->stopped=1;s->pending=0;}return QCH_STOP;}
static int qch_decide(struct qch_state *s,const struct qch_observation *o)
{
 if(!s||!o||!o->now||s->stopped||s->pending||o->now<s->last_now||(s->deadline&&o->now>=s->deadline))return qch_stop(s);
 s->last_now=o->now;
 if(o->stamp>o->now||o->frontend_stamp>o->now||o->network_stamp>o->now||
    (s->seen&&((o->frames&&o->frames<s->frames)||(o->frontend_seq&&o->frontend_seq<s->front)||(o->network_seq&&o->network_seq<s->network)||(o->stamp&&o->stamp<s->stamp))))return qch_stop(s);
 unsigned good=o->frames&&o->stamp&&o->now-o->stamp<=2000000&&o->frontend_seq&&o->frontend_stamp&&o->now-o->frontend_stamp<=3000000&&o->network_seq&&o->network_stamp&&o->now-o->network_stamp<=3000000;
 if(!good){if((s->seen&&o->now-s->last_fresh>=10000000)||(!s->seen&&o->now-s->began>=45000000))return qch_stop(s);return QCH_WAIT;}
 /* A stale file with fresh-looking contents cannot refresh the watchdog
  * repeatedly: all three independent owners must actually make progress. */
 if(o->frames<=s->frames||o->stamp<=s->stamp||o->frontend_seq<=s->front||o->network_seq<=s->network){
  if(s->seen&&o->now-s->last_fresh>=10000000)return qch_stop(s);return QCH_WAIT;
 }
 s->seen=1;s->last_fresh=o->now;
 if(s->last_feed&&o->now-s->last_feed<1000000)return QCH_WAIT;
 s->pending=1;s->proposed=*o;return QCH_FEED;
}
static int qch_confirm(struct qch_state *s,const struct qch_observation *o)
{
 if(!s||!o||!s->pending||s->stopped||memcmp(o,&s->proposed,sizeof *o))return qch_stop(s);
 s->last_feed=o->now;s->stamp=o->stamp;s->frames=o->frames;s->front=o->frontend_seq;s->network=o->network_seq;s->pending=0;return 1;
}
#endif
