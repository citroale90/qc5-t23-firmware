/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QC5_DETAIL_CAPTURE_H
#define QC5_DETAIL_CAPTURE_H
#include "isp-detail-filters.h"
#include "gc1084-detail.private.h"
static struct qc5_detail_state active_detail;
static int detail_write(void *opaque,uint32_t off,uint32_t value)
{color_write(opaque,off,value);return 1;}
static uint32_t detail_read(void *opaque,uint32_t off){return color_read(opaque,off);}
static int detail_apply(volatile uint32_t *isp,struct qc5_preview *record)
{
 memset(&active_detail,0,sizeof active_detail);
 if(!color_idle(isp,record))return 0;
 /* The stopped/bypassed block need not expose its committed coefficients.
  * Validate the entire plan before writes; read active values after frames,
  * as for SDNS, WB and CCM. No DMA addresses or enable bits are written here. */
 if(!qc5_detail_load(&active_detail,&gc1084_detail,0,detail_write,(void*)isp)||
    !qc5_detail_gain(&active_detail,QC5_NV12_INITIAL_GAIN,detail_write,(void*)isp))return core_refuse(record,0x79000,QC5_NV12_INITIAL_GAIN);
 return color_idle(isp,record);
}
static int detail_refresh(volatile uint32_t *isp,struct qc5_preview *record,unsigned gain)
{
 if(!qc5_detail_gain(&active_detail,gain,detail_write,(void*)isp))return core_refuse(record,0x79004,gain);
 return 1;
}
static int detail_finish(volatile uint32_t *isp,struct qc5_preview *record,unsigned gain)
{
 if(active_detail.gain!=gain)return core_refuse(record,0x79008,gain);
 if(!qc5_detail_verify(&active_detail,detail_read,(void*)isp))return core_refuse(record,
    active_detail.fault_offset?active_detail.fault_offset:0x79008,active_detail.fault_value);
 return 1;
}
#endif
