/* SPDX-License-Identifier: GPL-2.0-only
 * Finite T23 I2C2 identity read, not a motor or IR-cut command interface.
 * Only register 0x1d at address 0x10 is selected. No data-value write exists.
 * Use solely when this RAM kernel exclusively owns the hardware controller.
 */
#ifndef QC5_MOTOR_ID_PLAN_H
#define QC5_MOTOR_ID_PLAN_H
#ifndef __KERNEL__
#include <stdint.h>
#endif
#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif
enum qm_region {QM_CPM,QM_GPIO_B,QM_I2C2};
enum qm_fault {QM_OK,QM_ARGUMENT,QM_ROUTE,QM_CLOCK,QM_BUSY,QM_WRITE,QM_TIMEOUT,QM_NACK,QM_CHANGED};
struct qm_io {
 void *opaque;
 uint32_t (*read)(void *,unsigned,uint32_t);
 int (*write)(void *,unsigned,uint32_t,uint32_t);
 void (*delay_us)(void *,unsigned);
};
struct qm_result {uint32_t status,stage,apb_hz,identity,responded,stopped,gate_restored,abort_source,writes;};
struct qm_state {struct qm_io io;struct qm_result note;unsigned owned,gate_touched,gate_open;uint32_t old_gate;};
static uint32_t qm_read(struct qm_state *s,unsigned region,uint32_t offset)
{return s->io.read(s->io.opaque,region,offset);}
static int qm_fail(struct qm_state *s,unsigned fault)
{if(!s->note.status)s->note.status=fault;return 0;}
static int qm_write(struct qm_state *s,unsigned region,uint32_t offset,uint32_t value)
{
 if(s->note.status)return 0;
 if(!s->io.write(s->io.opaque,region,offset,value))return qm_fail(s,QM_WRITE);
 ++s->note.writes;return 1;
}
static int qm_wait(struct qm_state *s,unsigned offset,uint32_t mask,uint32_t wanted)
{
 for(unsigned n=0;n<100&&!s->note.status;n++){
  if((qm_read(s,QM_I2C2,offset)&mask)==wanted)return 1;
  s->io.delay_us(s->io.opaque,100);
 }
 return qm_fail(s,QM_TIMEOUT);
}
static uint32_t qm_pll(uint32_t value)
{
 unsigned m=value>>20,n=(value>>14)&63,od1=(value>>11)&7,od0=(value>>8)&7;
 if((value&9)!=9||!m||!n||!od1||!od0)return 0;
 uint64_t rate=div_u64(UINT64_C(24000000)*m,n*od1*od0);
 return rate<=2400000000u?(uint32_t)rate:0;
}
static uint32_t qm_apb(struct qm_state *s)
{
 uint32_t cpccr=qm_read(s,QM_CPM,0);unsigned src=cpccr>>30,ahb=(cpccr>>24)&3;
 uint32_t sclka=src==2?qm_pll(qm_read(s,QM_CPM,0x10)):src==1?24000000u:0;
 uint32_t parent=ahb==1?sclka:ahb==2?qm_pll(qm_read(s,QM_CPM,0x14)):0;
 uint32_t rate=parent/(((cpccr>>16)&15)+1);
 return rate>=1000000u&&rate<=200000000u?rate:0;
}
static int qm_read_id(struct qm_state *s,uint8_t *out)
{
 if(!qm_wait(s,0x70,1u<<5,0))return 0;
 (void)qm_read(s,QM_I2C2,0x40);
 const uint32_t commands[]={0x1d,0x700}; /* register then restart/read/STOP */
 for(unsigned n=0;n<2;n++){
  if(!qm_wait(s,0x70,2,2)||!qm_write(s,QM_I2C2,0x10,commands[n]))return 0;
 }
 unsigned received=0;
 for(unsigned n=0;n<100;n++){
  uint32_t irq=qm_read(s,QM_I2C2,0x34);
  if(irq&0x40){s->note.abort_source=qm_read(s,QM_I2C2,0x80);(void)qm_read(s,QM_I2C2,0x40);return qm_fail(s,QM_NACK);}
  if(!received&&(qm_read(s,QM_I2C2,0x70)&8)){*out=(uint8_t)qm_read(s,QM_I2C2,0x10);received=1;}
  if(received&&(irq&0x200)){
   (void)qm_read(s,QM_I2C2,0x40);
   if(qm_read(s,QM_I2C2,0x78))return qm_fail(s,QM_BUSY);
   return qm_wait(s,0x70,1u<<5,0);
  }
  s->io.delay_us(s->io.opaque,100);
 }
 return qm_fail(s,QM_TIMEOUT);
}
static void qm_cleanup(struct qm_state *s)
{
 if(s->owned){
  for(unsigned attempt=0;attempt<3&&!s->note.stopped;attempt++){
   int a=s->io.write(s->io.opaque,QM_I2C2,0x6c,0);
   for(unsigned n=0;n<100;n++){
    if(!(qm_read(s,QM_I2C2,0x9c)&1)&&!(qm_read(s,QM_I2C2,0x70)&(1u<<5))){s->note.stopped=a;break;}
    s->io.delay_us(s->io.opaque,100);
   }
  }
  if(!s->note.stopped)qm_fail(s,QM_TIMEOUT);
 }
 /* Never cut the clock of a controller that could still be active. */
 if(s->gate_touched&&(!s->gate_open||s->note.stopped||(!s->owned&&!(qm_read(s,QM_I2C2,0x70)&(1u<<5))))){
  for(unsigned attempt=0;attempt<3&&!s->note.gate_restored;attempt++){
   uint32_t value=(qm_read(s,QM_CPM,0x20)&~(1u<<9))|(s->old_gate&(1u<<9));
   int a=s->io.write(s->io.opaque,QM_CPM,0x20,value);
   s->note.gate_restored=a&&((qm_read(s,QM_CPM,0x20)^s->old_gate)&(1u<<9))==0;
  }
  if(!s->note.gate_restored)qm_fail(s,QM_WRITE);
 }
}
static int qm_probe(struct qm_state *s)
{
 if(!s)return 0;memset(&s->note,0,sizeof s->note);s->owned=s->gate_touched=s->gate_open=0;
 if(!s->io.read||!s->io.write||!s->io.delay_us)return qm_fail(s,QM_ARGUMENT);
 s->note.stage=1;const uint32_t pins=3u<<20;
 if((qm_read(s,QM_GPIO_B,0x10)&pins)||(qm_read(s,QM_GPIO_B,0x20)&pins)||
    (qm_read(s,QM_GPIO_B,0x30)&pins)!=pins||(qm_read(s,QM_GPIO_B,0x40)&pins))return qm_fail(s,QM_ROUTE);
 s->note.apb_hz=qm_apb(s);if(!s->note.apb_hz)return qm_fail(s,QM_CLOCK);
 s->old_gate=qm_read(s,QM_CPM,0x20);s->gate_touched=1;
 if(!qm_write(s,QM_CPM,0x20,s->old_gate&~(1u<<9)))goto done;
 if(qm_read(s,QM_CPM,0x20)&(1u<<9)){qm_fail(s,QM_CLOCK);goto done;}
 s->gate_open=1;
 s->note.stage=2;
 if((qm_read(s,QM_I2C2,0x70)&(1u<<5))||qm_read(s,QM_I2C2,0x74)||qm_read(s,QM_I2C2,0x78)||qm_read(s,QM_I2C2,0x88)){
  qm_fail(s,QM_BUSY);goto done;
 }
 s->owned=1;
 if(!qm_write(s,QM_I2C2,0x6c,0)||!qm_wait(s,0x9c,1,0))goto done;
 uint32_t half=s->note.apb_hz/200000u,quarter=s->note.apb_hz/400000u;
 if(!qm_write(s,QM_I2C2,0x30,0)||!qm_write(s,QM_I2C2,0x88,0)||
    !qm_write(s,QM_I2C2,0,0x63)||!qm_write(s,QM_I2C2,4,0x10)||
    !qm_write(s,QM_I2C2,0x14,half>14?half-8:6)||!qm_write(s,QM_I2C2,0x18,half>9?half-1:8)||
    !qm_write(s,QM_I2C2,0x7c,quarter)||!qm_write(s,QM_I2C2,0x94,quarter>256?255:quarter-1))goto done;
 (void)qm_read(s,QM_I2C2,0x40);
 if(!qm_write(s,QM_I2C2,0x6c,1)||!qm_wait(s,0x9c,1,1))goto done;
 s->note.stage=3;uint8_t first=0,second=0;
 if(!qm_read_id(s,&first)||!qm_read_id(s,&second))goto done;
 if(first!=second){qm_fail(s,QM_CHANGED);goto done;}
 s->note.identity=first;s->note.responded=1;s->note.stage=4;
done:
 qm_cleanup(s);
 return !s->note.status&&s->note.responded&&s->note.stopped&&s->note.gate_restored;
}
#endif
