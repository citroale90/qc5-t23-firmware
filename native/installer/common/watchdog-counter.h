/* Installer-only known profiles. U-Boot: TDR=51200/TCSR=0x1a.
 * Original tfUpgrade WatchDogInit @0x403244 enables the Linux watchdog and
 * requests 90 seconds @0x4032e8. Cold hardware log confirms TDR=16479,
 * TCSR=0x22, TCER=1. The separate U-Boot writer keeps its original profile.
 * Neither accepted profile permits changing the timer configuration. */
/* SPDX-License-Identifier: GPL-3.0-only
 * The RAM shim already configured/started this timer. This layer may only
 * reset its counter; it cannot start, disable or reconfigure the watchdog.
 */
#ifndef QC5_WATCHDOG_COUNTER_ONLY_H
#define QC5_WATCHDOG_COUNTER_ONLY_H
#include <stdint.h>
struct qhw_io {void *opaque;int (*read)(void *,unsigned,uint32_t *);int (*write)(void *,unsigned,uint32_t);};
static int qhw_read_allowed(unsigned off){return off==0||off==4||off==8||off==12;}
static int qhw_write_allowed(unsigned off,uint32_t value){return off==8&&!value;}
static int qhw_geometry(const struct qhw_io *io,uint32_t *counter)
{
 uint32_t data,enable,control;
 return io&&io->read&&io->write&&counter&&io->read(io->opaque,0,&data)&&
  io->read(io->opaque,4,&enable)&&io->read(io->opaque,8,counter)&&io->read(io->opaque,12,&control)&&
  enable==1u&&((data==51200u&&control==0x1au)||(data==16479u&&control==0x22u))&&*counter<data;
}
static int qhw_refresh(const struct qhw_io *io)
{
 uint32_t before,after;
 /* Also detect an interfering refresher or a timer that is not advancing. */
 if(!qhw_geometry(io,&before)||before<64u||!io->write(io->opaque,8,0))return 0;
 return qhw_geometry(io,&after)&&after<=32u;
}
#endif
