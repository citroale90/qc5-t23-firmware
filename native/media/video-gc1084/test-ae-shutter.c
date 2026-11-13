/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include "auto-exposure.h"

struct fake_sensor { unsigned writes, reads, fail_write, fail_read; struct qc5_ae_setting value; };
static int submit(void *p, struct qc5_ae_setting value)
{
    struct fake_sensor *s=p; ++s->writes;
    if(s->fail_write)return 0;
    s->value=value; return 1;
}
static int verify(void *p, struct qc5_ae_setting value)
{
    struct fake_sensor *s=p; ++s->reads;
    return !s->fail_read && value.lines==s->value.lines && value.gain==s->value.gain;
}
int main(void)
{
    assert(QC5_GC1084_PIXEL_CLOCK_HZ / QC5_GC1084_LINE_CLOCKS / QC5_GC1084_FRAME_LINES == 25);
    assert(UINT64_C(450)*QC5_GC1084_LINE_CLOCKS*1000000/QC5_GC1084_PIXEL_CLOCK_HZ==20000);
    assert(QC5_AE_INITIAL_EXPOSURE_LINES<=QC5_AE_MAX_EXPOSURE_LINES);
    unsigned cases=0;
    for(unsigned lines=4;lines<=QC5_AE_MAX_EXPOSURE_LINES;++lines)
        for(unsigned gain=0;gain<=24;++gain)
            for(unsigned mean=0;mean<=255;++mean){
                struct qc5_ae_setting before={lines,gain},next={0};
                int changed=qc5_ae_next_target(before,mean,128,&next);
                assert(changed>=0 && next.lines>=4 && next.lines<=QC5_AE_MAX_EXPOSURE_LINES && next.gain<=24);
                assert(changed==(next.lines!=lines || next.gain!=gain));
                if(mean>=116 && mean<=140)assert(!changed);
                if(mean<116 && lines==QC5_AE_MAX_EXPOSURE_LINES){
                    assert(next.lines==lines && next.gain==gain+(gain<24));
                }
                ++cases;
            }
    struct qc5_ae_setting next;
    assert(qc5_ae_next_target((struct qc5_ae_setting){QC5_AE_MAX_EXPOSURE_LINES+1,0},0,128,&next)==-1);
    assert(qc5_ae_next_target((struct qc5_ae_setting){4,25},0,128,&next)==-1);
    assert(qc5_ae_next_target((struct qc5_ae_setting){4,0},256,128,&next)==-1);
    struct fake_sensor sensor={0};
    struct qc5_ae_controller ae={.selected={QC5_AE_MAX_EXPOSURE_LINES,8},.target=128};
    assert(qc5_ae_tick(&ae,0,1000000,0,submit,verify,&sensor));
    assert(!sensor.writes && !ae.pending && ae.selected.gain==8);
    assert(qc5_ae_tick(&ae,0,1000000,1,submit,verify,&sensor));
    assert(sensor.writes==1 && ae.pending && ae.due_us==1250000 && ae.selected.gain==9);
    assert(qc5_ae_tick(&ae,0,1249999,1,submit,verify,&sensor));
    assert(!sensor.reads && sensor.writes==1);
    assert(qc5_ae_tick(&ae,128,1250000,1,submit,verify,&sensor));
    assert(sensor.reads==1 && !ae.pending && sensor.writes==1);
    sensor.fail_write=1;
    assert(!qc5_ae_tick(&ae,0,2000000,1,submit,verify,&sensor));
    assert(!ae.pending && ae.selected.gain==9);
    sensor.fail_write=0;
    assert(qc5_ae_tick(&ae,0,2000000,1,submit,verify,&sensor));
    sensor.fail_read=1;
    assert(!qc5_ae_tick(&ae,128,2250000,1,submit,verify,&sensor));
    assert(ae.pending);
    printf("AE cap=%u: %u states; settle, gain saturation and failed I/O passed\n",QC5_AE_MAX_EXPOSURE_LINES,cases);
    return 0;
}
