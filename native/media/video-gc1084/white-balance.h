#ifndef QC5_WHITE_BALANCE_H
#define QC5_WHITE_BALANCE_H
#ifndef QC5_WB_PROFILE_HEADER
#define QC5_WB_PROFILE_HEADER "gc1084-wb.private.h"
#endif
#include QC5_WB_PROFILE_HEADER
#ifdef QC5_DYNAMIC_AWB
static const uint32_t *wb_active_expected(void);
static uint32_t wb_active_hash(void);
#else
static const uint32_t *wb_active_expected(void){return wb_expected;}
static uint32_t wb_active_hash(void){return QC5_WB_HASH;}
#endif

static int wb_apply(volatile uint32_t *isp,struct qc5_preview *record,
                     const struct color_row *plan,unsigned int count)
{
    if(!plan||count!=8)return core_refuse(record,0x1800,count);
    uint32_t hash=2166136261u;
    for(unsigned int n=0;n<8;++n){
        if(plan[n].offset != (n%2 ? wb_offsets[n/2] : 0x1800u) ||
            plan[n].value != (n%2 ? wb_expected[n/2] : 1u))
            return core_refuse(record,plan[n].offset,plan[n].value);
        hash=color_hash_pair(hash,plan[n].offset,plan[n].value);
    }
    if(hash!=QC5_WB_HASH||!color_idle(isp,record))return 0;
    for(unsigned int n=0;n<8;++n)color_write(isp,plan[n].offset,plan[n].value);
    return 1;
}

static int wb_finish(volatile uint32_t *isp,struct qc5_preview *record,
                      struct msca_record *data)
{
    data->data.success.wb.plan_hash=wb_active_hash();
    data->data.success.wb.read_count=0;
    for(unsigned int n=0;n<4;++n){
        uint32_t value=color_read(isp,wb_offsets[n]);
        data->data.success.wb.values[n]=value;
        ++data->data.success.wb.read_count;
        if(value!=wb_active_expected()[n])return core_refuse(record,wb_offsets[n],value);
    }
    return 1;
}

static int wb_record_valid(const struct msca_record *data)
{
    return data->data.success.wb.plan_hash==wb_active_hash() &&
        data->data.success.wb.read_count==4 &&
        !memcmp(data->data.success.wb.values,wb_active_expected(),sizeof(wb_expected));
}
#endif
