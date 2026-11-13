/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QC5_MSC_CLOCK_PLAN_H
#define QC5_MSC_CLOCK_PLAN_H
#include "sfc-clock.h"
static unsigned int qc5_msc_inherited_rate(unsigned int pll,unsigned int cdr,unsigned int gate,unsigned int gate1)
{
    unsigned int parent=qc5_sfc_pll_rate(pll),rate=parent/2/((cdr&255)+1);
    if(parent!=1200000000u||(cdr>>30)!=1||(cdr&0x18000000u)||(gate&(1u<<4))||(gate1&(1u<<16)))return 0;
    return rate==24000000u||rate==50000000u?rate:0;
}
#endif
