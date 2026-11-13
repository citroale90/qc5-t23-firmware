/* SPDX-License-Identifier: GPL-2.0-or-later
 * Ingenic CH_DCS.CDOA is descriptor offset information, not an error.
 * Definitions: vendor PDMA submission, 2024-03-21, CH_DCS0x10/CDOA[15:8]:
 * https://lkml.iu.edu/2403.2/04681.html
 * T23 H57 actual snapshot: DCS40000201, DDA01cee020, target01cef020,
 * count123. No AR/TT/HLT; offset2 means next descriptor at base+32.
 * Permit ONLY that defined field, bounded to our owned32-descriptor table.
 * Every other control, error and reserved bit remains checked exactly.
 */
#ifndef QC5_AUDIO_STREAM_STATUS_H
#define QC5_AUDIO_STREAM_STATUS_H
#include "stream-descriptors.h"
#define QAS_DCS_CDOA_SHIFT 8u
#define QAS_DCS_CDOA_MASK UINT32_C(0x0000ff00)
static int qas_dma_status_valid(uint32_t dcs)
{
 if((dcs&~QAS_DCS_CDOA_MASK)!=0x40000001u)return 0;
 unsigned offset=(dcs&QAS_DCS_CDOA_MASK)>>QAS_DCS_CDOA_SHIFT;
 return !(offset&1u)&&offset<QAS_DESC_BYTES/16u;
}
static uint32_t qas_dma_descriptor_address(uint32_t dcs)
{return QA_DESCRIPTOR+((dcs&QAS_DCS_CDOA_MASK)>>QAS_DCS_CDOA_SHIFT)*16u;}
static int qas_dma_address_owned(uint32_t dda)
{return dda>=QA_DESCRIPTOR&&dda-QA_DESCRIPTOR<QAS_DESC_BYTES&&!((dda-QA_DESCRIPTOR)%32u);}
#endif
