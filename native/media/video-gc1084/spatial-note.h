#ifndef QC5_SPATIAL_NOTE_H
#define QC5_SPATIAL_NOTE_H
#include <stdint.h>
struct qc5_spatial_note { uint32_t magic, version, profile_hash, writes, updates, gain, read_hash, bypass; };
_Static_assert(sizeof(struct qc5_spatial_note)==32,"Spatial note fits existing checkpoint");
#define QC5_SPATIAL_NOTE_MAGIC UINT32_C(0x37335053) /* SP37 */
static uint32_t qc5_spatial_pair_digest(uint32_t a,uint32_t b)
{
    uint32_t h=2166136261u;
    for(unsigned n=0;n<8;n++)h=(h^(((n<4?a:b)>>(8*(n%4)))&255u))*16777619u;
    return h;
}
static int qc5_spatial_note_version_valid(const struct qc5_spatial_note *p,uint32_t gain,uint32_t expected_profile,unsigned version)
{
    return (version>=1&&version<=5)&&p&&p->magic==QC5_SPATIAL_NOTE_MAGIC&&p->version==version&&expected_profile&&p->profile_hash==expected_profile&&
        gain<=24&&p->gain==gain&&p->updates>=1&&p->updates<=121&&p->writes>=209u+37u*p->updates&&
        p->writes-(209u+37u*p->updates)<=121u&&p->read_hash&&p->bypass==(version>=4?UINT32_C(0xb57468c9):version>=2?UINT32_C(0xb57568c9):UINT32_C(0xb57768f9));
}
static int qc5_spatial_note_valid(const struct qc5_spatial_note *p,uint32_t gain,uint32_t expected_profile)
{return qc5_spatial_note_version_valid(p,gain,expected_profile,1);}
#endif
