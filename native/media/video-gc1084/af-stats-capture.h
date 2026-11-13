/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QC5_AF_STATS_CAPTURE_H
#define QC5_AF_STATS_CAPTURE_H
#include "af-stats.h"
static struct qc5_af_stats active_af_stats;
static uint32_t af_read(void *opaque,uint32_t off){return color_read(opaque,off);}
static int af_stats_prepare(volatile uint32_t *isp,struct qc5_preview *record)
{
 const struct qc5_af_io io={(void*)isp,af_read};
 if(!qc5_af_prepare(&active_af_stats,&io))return core_refuse(record,
    active_af_stats.fault_offset?active_af_stats.fault_offset:0x7a000,active_af_stats.fault_value);
 return 1;
}
static int af_stats_finish(const volatile uint8_t *ram,struct qc5_preview *record,unsigned stop)
{
 if(!qc5_af_finish(&active_af_stats,ram,stop))return core_refuse(record,
    active_af_stats.fault_offset?active_af_stats.fault_offset:0x7a004,active_af_stats.fault_value);
#ifndef QC5_FRAME_TEST
 int fd=open("/dev/kmsg",O_WRONLY|O_CLOEXEC);
 if(fd>=0){char line[200];int n=snprintf(line,sizeof line,"<6>QC5_AF_STATS bank=%u changed=%u pages=%u config=%08x data=%08x\n",active_af_stats.bank,active_af_stats.changed_bytes,active_af_stats.pages_mask,active_af_stats.read_hash,active_af_stats.data_hash);
  if(n>0&&n<(int)sizeof line)(void)write(fd,line,(unsigned)n);close(fd);}
#endif
 return 1;
}
#endif
