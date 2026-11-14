/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QC5_STARTUP_CALIBRATION_H
#define QC5_STARTUP_CALIBRATION_H
#include <stdint.h>
enum {QSC_WAIT=0,QSC_STARTED=1,QSC_UNAVAILABLE=2,QSC_FAILED=3};
struct qsc_state {unsigned phase;uint64_t deadline,last;};
static unsigned qsc_tick(struct qsc_state *s,uint64_t now,int (*start)(void *,uint64_t),void *opaque)
{
 if(!s||!start)return QSC_FAILED;
 if(s->phase!=QSC_WAIT)return s->phase;
 if(!now||now<s->last||now>UINT64_MAX-10000000)return s->phase=QSC_FAILED;
 if(!s->deadline)s->deadline=now+10000000;s->last=now;
 if(now>=s->deadline)return s->phase=QSC_UNAVAILABLE;
 int result=start(opaque,now);
 if(result>0)s->phase=QSC_STARTED;else if(result<0)s->phase=QSC_FAILED;
 return s->phase;
}
#endif
