/* SPDX-License-Identifier: GPL-3.0-only
 * Diagnostic only: one continuous8-second AP interval, then the profile
 * already read from SD. No new credentials and no durable writes required.
 */
#ifndef QC5_NATIVE_AP_RAM_PROBE_H
#define QC5_NATIVE_AP_RAM_PROBE_H
#include "wifi-transaction.h"
struct qna_probe {struct qc5_wifi_profile saved;uint64_t since;unsigned active,observing;};
static int qna_init(struct qna_probe *p,const struct qc5_wifi_profile *saved)
{memset(p,0,sizeof *p);if(!qc5_wifi_valid(saved))return 0;p->saved=*saved;p->active=1;return 1;}
static int qna_tick(struct qna_probe *p,struct qnt_transaction *t,uint64_t now,unsigned dhcp_running)
{
 if(!p->active)return 0;
 if(t->mode->active!=QNW_AP||!dhcp_running){p->observing=0;return 0;}
 if(!p->observing){p->since=now;p->observing=1;return 0;}
 if(now<p->since)return -1;if(now-p->since<8000)return 0;
 if(!qnt_stage(t,&p->saved))return -1;
 memset(&p->saved,0,sizeof p->saved);p->active=0;return 1;
}
#endif
