/* SPDX-License-Identifier: GPL-2.0-or-later
 * Literal T23 PLL counters; never programs a PLL. 25 MHz single SPI only.
 */
#ifndef QC5_SFC_CLOCK_H
#define QC5_SFC_CLOCK_H
#ifdef __KERNEL__
#include <linux/math64.h>
#define QC5_DIV64(a,b) div_u64((a),(b))
#else
#define QC5_DIV64(a,b) ((a)/(b))
#endif
static unsigned int qc5_sfc_pll_rate(unsigned int v)
{
    unsigned int m=v>>20,n=v>>14&63,od1=v>>11&7,od0=v>>8&7;
    if((v&9)!=9||!m||!n||!od1||!od0)return 0;
    unsigned long long hz=QC5_DIV64(24000000ull*m,n*od1*od0);
    return hz<=2400000000u?(unsigned int)hz:0;
}
static int qc5_sfc_clock_plan(unsigned int mpll,unsigned int current,unsigned int *out)
{
    unsigned int rate,target=25000000u;
    if(!out)return 0;
    rate=qc5_sfc_pll_rate(mpll);
    if(!rate||rate%target||rate/target>256u)return 0;
    *out=(current&~0xdc0000ffu)|0x40000000u|(rate/target-1u);
    return 1;
}
#undef QC5_DIV64
#endif
