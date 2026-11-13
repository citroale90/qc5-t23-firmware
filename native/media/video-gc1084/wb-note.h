#ifndef QC5_WB_NOTE_H
#define QC5_WB_NOTE_H
#include <stdint.h>
struct qc5_wb_note {uint32_t magic,version,plan_hash,requested_r,requested_b,read_r,read_b,read_mask;};
#define QC5_WB_NOTE_MAGIC UINT32_C(0x37334757) /* WG37 */
_Static_assert(sizeof(struct qc5_wb_note)==32,"WB note fits existing SD checkpoint");
static int qc5_wb_note_version_valid(const struct qc5_wb_note *p,uint32_t expected_hash,unsigned version)
{
    return p&&p->magic==QC5_WB_NOTE_MAGIC&&p->version==version&&expected_hash&&p->plan_hash==expected_hash&&
        (p->requested_r&0xffffc000u)==0x04000000u&&(p->requested_b&0xffffc000u)==0x04000000u&&
        (p->requested_r&0x3fffu)&&(p->requested_b&0x3fffu)&&p->requested_r==p->read_r&&
        p->requested_b==p->read_b&&p->read_mask==15;
}
static int qc5_wb_note_valid(const struct qc5_wb_note *p,uint32_t expected_hash)
{return qc5_wb_note_version_valid(p,expected_hash,1);}
#endif
