/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QC5_SENSOR_ORIENTATION_H
#define QC5_SENSOR_ORIENTATION_H
#include <stdint.h>
#define QC5_ORIENTATION_MAGIC UINT32_C(0x37334f53) /* SO37 */
struct qc5_orientation_note {
    uint32_t magic,version,mode,before[2],after[2],bayer;
};
_Static_assert(sizeof(struct qc5_orientation_note)==32,"Orientation note fits existing proof");
static int qc5_orientation_note_valid(const struct qc5_orientation_note *p)
{
    if(!p||p->magic!=QC5_ORIENTATION_MAGIC||p->version!=1||p->mode!=3||p->bayer!=1)return 0;
    for(unsigned n=0;n<2;++n)
        if(p->before[n]>255||p->after[n]>255||p->after[n]!=((p->before[n]&252u)|p->mode))return 0;
    return 1;
}
struct qc5_orientation_io {
    void *context;
    int (*read)(void *,uint16_t,uint8_t *);
    int (*write)(void *,uint16_t,uint8_t);
    void (*pause)(void *,unsigned);
    unsigned settle_us;
};
static int qc5_orientation_apply(const struct qc5_orientation_io *io,unsigned mode,
                                  struct qc5_orientation_note *out)
{
    if(!out)return 0;
    *out=(struct qc5_orientation_note){0};
    if(!io||!io->read||!io->write||mode>3||io->settle_us>250000u||(io->settle_us&&!io->pause))return 0;
    static const uint16_t regs[2]={0x0015,0x0d15};
    struct qc5_orientation_note p={.version=1,.mode=mode,.bayer=1};
    *out=p;
    for(unsigned n=0;n<2;++n){
        uint8_t v=0;if(!io->read(io->context,regs[n],&v))return 0;p.before[n]=v;*out=p;
    }
    for(unsigned n=0;n<2;++n)
        if(!io->write(io->context,regs[n],(uint8_t)((p.before[n]&252u)|mode)))return 0;
    if(io->settle_us)io->pause(io->context,io->settle_us);
    for(unsigned n=0;n<2;++n){
        uint8_t v=0;
        if(!io->read(io->context,regs[n],&v))return 0;
        p.after[n]=v;*out=p;
        if(v!=((p.before[n]&252u)|mode))return 0;
    }
    p.magic=QC5_ORIENTATION_MAGIC;*out=p;return 1;
}
#endif
