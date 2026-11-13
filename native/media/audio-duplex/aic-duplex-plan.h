/* SPDX-License-Identifier: GPL-2.0-only
 * Plan TX-only AIC changes while the proven16-bit mono RX stays running.
 * Constants come from the pinned Ingenic audio_aic.h, not status guesses.
 * No MMIO occurs here. DMA ownership/start/stop remain a separate operation.
 */
#ifndef QC5_AIC_DUPLEX_PLAN_H
#define QC5_AIC_DUPLEX_PLAN_H
#include <stdint.h>
#define QAD_AIC_LAST_SAMPLE (1u<<6)
#define QAD_AIC_TX_FIELDS ((7u<<19)|(1u<<11)|(1u<<14)|(1u<<8)|(1u<<1))
struct qad_aic_plan {uint32_t frame_zero,tx_prepared,tx_flush,tx_enabled,tx_stopped;};
static int qad_aic_plan(uint32_t frame,uint32_t control,struct qad_aic_plan *out)
{
 if(!out||(frame&~QAD_AIC_LAST_SAMPLE)!=0x08100031u||
    (control&~QAD_AIC_TX_FIELDS)!=0x00019001u||(control&0x4102u))return 0;
 uint32_t prepared=(control&~QAD_AIC_TX_FIELDS)|0x00080800u;
 *out=(struct qad_aic_plan){frame&~QAD_AIC_LAST_SAMPLE,prepared,prepared|0x100u,prepared|0x4002u,prepared};
 return 1;
}
#endif
