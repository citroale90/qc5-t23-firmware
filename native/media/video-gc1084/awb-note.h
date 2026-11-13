/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef QC5_AWB_NOTE_H
#define QC5_AWB_NOTE_H
#include <stdint.h>
#define QC5_AWB_NOTE_MAGIC UINT32_C(0x37335741) /* AW37 */
#define QC5_AWB_EMPTY_STATS_HASH UINT32_C(0x4207f105) /* FNV3600 zero bytes */
#define QC5_AWB_EMPTY_STATS_FLAG (1u<<25)
#define QC5_AWB_NO_ZONES_FLAG (1u<<26)
struct qc5_awb_note {uint32_t magic,progress,profile_hash,ct,stats_hash,readback;};
_Static_assert(sizeof(struct qc5_awb_note)==24,"AWB note shares existing proof space");
static int qc5_awb_note_version_valid(const struct qc5_awb_note *p,uint32_t hash,unsigned version)
{
    if(!p||p->magic!=QC5_AWB_NOTE_MAGIC||!hash||p->profile_hash!=hash||(p->progress&255)!=version)return 0;
    unsigned samples=(p->progress>>8)&255,updates=(p->progress>>16)&255,ms=p->progress>>24;
    uint32_t saturated=(version==3||version==4||version==5)?p->readback&(1u<<24):0;
    uint32_t empty=version==4?p->readback&QC5_AWB_EMPTY_STATS_FLAG:version==5?p->readback&QC5_AWB_NO_ZONES_FLAG:0;
    /* Version4 explicitly represents rotating, decoded all-zero statistics.
     * A legacy note is still required to have populated zones. */
    if(empty&&(p->ct!=5000||(version==4&&p->stats_hash!=QC5_AWB_EMPTY_STATS_HASH)||p->readback&(1u<<23)))return 0;
    return samples>=16&&samples<=248&&updates&&updates<=samples&&ms&&ms<=40&&
        (!saturated||samples==248)&&p->ct>=1000&&p->ct<=30000&&(p->stats_hash||version==5)&&
        ((p->readback&~(saturated|empty))&0xffcfffffu)==(empty?0x004fffffu:0x00cfffffu);
}
static int qc5_awb_note_valid(const struct qc5_awb_note *p,uint32_t hash)
{return qc5_awb_note_version_valid(p,hash,1);}
#endif
