/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef QC5_CCM_NOTE_H
#define QC5_CCM_NOTE_H
#include <stdint.h>
#include "wb-note.h"
#include "ccm-algorithm.h"
#include "gc1084-ccm.private.h"
#include "exposure-ev.h"
/* WG37 v3 reuses the formerly duplicated requested gains for CCM evidence.
 * Four actual WB comparisons and five actual CCM comparisons remain explicit.
 */
struct qc5_ccm_wb_note {uint32_t magic,version,awb_profile_hash,ccm_profile_hash,ccm_read_hash,r,b,masks;};
_Static_assert(sizeof(struct qc5_ccm_wb_note)==32,"Existing checkpoint size is preserved");
static int qc5_ccm_wb_note_valid(const struct qc5_ccm_wb_note *p,uint32_t awb_hash,uint32_t ccm_hash)
{
    if(!p||p->magic!=QC5_WB_NOTE_MAGIC||(p->version!=3&&p->version!=4)||!awb_hash||!ccm_hash||
       p->awb_profile_hash!=awb_hash||p->ccm_profile_hash!=ccm_hash||!p->ccm_read_hash||
       (p->r&0xffffc000u)!=0x04000000u||(p->b&0xffffc000u)!=0x04000000u||!(p->r&0x3fff)||!(p->b&0x3fff)||
       ((p->masks&0xffff)&~(p->version==4?512u:0u))!=511)return 0;
    unsigned samples=p->masks>>24,updates=p->masks>>16&255;
    return samples>=16&&samples<=(p->version==4?248u:32u)&&updates<=samples&&(!(p->masks&512u)||samples==248);
}
#endif
