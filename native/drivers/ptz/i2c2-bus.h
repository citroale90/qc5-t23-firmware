/* SPDX-License-Identifier: GPL-2.0-only
 * Exclusive, bounded T23 DesignWare I2C2 transport for address0x10 only.
 * No Linux I2C bus scan, sensor bus, arbitrary address, SFC or flash access.
 */
#ifndef QC5_NATIVE_I2C2_BUS_H
#define QC5_NATIVE_I2C2_BUS_H
#include "motor-id-plan.h"
struct qbus {struct qm_state bus;uint32_t old_mask,old_gate;unsigned opened,owned,route_changed,gate_touched;};
static int qb_wait(struct qbus *b,unsigned reg,unsigned mask,unsigned expected)
{
 for(unsigned n=0;n<100;n++){
  if((qm_read(&b->bus,QM_I2C2,reg)&mask)==expected)return 1;
  b->bus.io.delay_us(b->bus.io.opaque,100);
 }return 0;
}
static int qb_regwrite(struct qbus *b,unsigned region,unsigned reg,unsigned value)
{return b->bus.io.write(b->bus.io.opaque,region,reg,value);}
static int qb_transfer(struct qbus *b,unsigned reg,unsigned *value,unsigned read)
{
 if(!b||!b->opened||!value||read>1||reg>0x1d||(!read&&(reg>0x1b||*value>255||(reg==0x16&&*value>2))))return 0;
 if(!qb_wait(b,0x70,32,0))return 0;(void)qm_read(&b->bus,QM_I2C2,0x40);
 if(!qb_wait(b,0x70,2,2)||!qb_regwrite(b,QM_I2C2,0x10,reg)||!qb_wait(b,0x70,2,2)||
  !qb_regwrite(b,QM_I2C2,0x10,read?0x700:0x200|*value))return 0;
 unsigned received=0;
 for(unsigned n=0;n<100;n++){
  uint32_t irq=qm_read(&b->bus,QM_I2C2,0x34);
  if(irq&0x40){(void)qm_read(&b->bus,QM_I2C2,0x80);(void)qm_read(&b->bus,QM_I2C2,0x40);return 0;}
  if(read&&!received&&(qm_read(&b->bus,QM_I2C2,0x70)&8)){*value=qm_read(&b->bus,QM_I2C2,0x10)&255;received=1;}
  if((!read||received)&&(irq&0x200)){
   (void)qm_read(&b->bus,QM_I2C2,0x40);
   return qb_wait(b,0x70,32,0)&&!qm_read(&b->bus,QM_I2C2,0x78);
  }
  b->bus.io.delay_us(b->bus.io.opaque,100);
 }return 0;
}
static int qb_close(struct qbus *b)
{
 if(!b)return 0;int stopped=!b->owned&&!(qm_read(&b->bus,QM_I2C2,0x70)&32);
 if(b->owned)stopped=qb_regwrite(b,QM_I2C2,0x6c,0)&&qb_wait(b,0x9c,1,0)&&qb_wait(b,0x70,32,0);
 int ok=stopped;
 if(stopped){
  if(b->route_changed){
   if(!qb_regwrite(b,QM_GPIO_B,0x24,b->old_mask&(3u<<20))||qm_read(&b->bus,QM_GPIO_B,0x20)!=b->old_mask)ok=0;
  }
  if(b->gate_touched){uint32_t now=qm_read(&b->bus,QM_CPM,0x20),restore=(now&~(1u<<9))|(b->old_gate&(1u<<9));
   if(!qb_regwrite(b,QM_CPM,0x20,restore)||((qm_read(&b->bus,QM_CPM,0x20)^b->old_gate)&(1u<<9)))ok=0;}
 }
 b->opened=0;return ok;
}
static int qb_open(struct qbus *b,const struct qm_io *io)
{
 if(!b||!io||!io->read||!io->write||!io->delay_us)return 0;memset(b,0,sizeof *b);b->bus.io=*io;
 const uint32_t pins=3u<<20;
 b->old_mask=qm_read(&b->bus,QM_GPIO_B,0x20);b->old_gate=qm_read(&b->bus,QM_CPM,0x20);
 if((qm_read(&b->bus,QM_GPIO_B,0x10)&pins)||(qm_read(&b->bus,QM_GPIO_B,0x30)&pins)!=pins||
  (qm_read(&b->bus,QM_GPIO_B,0x40)&pins)||((b->old_mask&pins)!=0&&(b->old_mask&pins)!=pins))return 0;
 unsigned apb=qm_apb(&b->bus);if(!apb)return 0;
 b->gate_touched=1;if(!qb_regwrite(b,QM_CPM,0x20,b->old_gate&~(1u<<9)))goto bad;
 if((qm_read(&b->bus,QM_I2C2,0x70)&32)||qm_read(&b->bus,QM_I2C2,0x74)||qm_read(&b->bus,QM_I2C2,0x78)||qm_read(&b->bus,QM_I2C2,0x88))goto bad;
 b->owned=1;if(!qb_regwrite(b,QM_I2C2,0x6c,0)||!qb_wait(b,0x9c,1,0))goto bad;
 if(b->old_mask&pins){b->route_changed=1;if(!qb_regwrite(b,QM_GPIO_B,0x28,pins))goto bad;}
 unsigned half=apb/200000,quarter=apb/400000;
 if(!qb_regwrite(b,QM_I2C2,0x30,0)||!qb_regwrite(b,QM_I2C2,0x88,0)||!qb_regwrite(b,QM_I2C2,0,0x63)||
  !qb_regwrite(b,QM_I2C2,4,0x10)||!qb_regwrite(b,QM_I2C2,0x14,half>14?half-8:6)||
  !qb_regwrite(b,QM_I2C2,0x18,half>9?half-1:8)||!qb_regwrite(b,QM_I2C2,0x7c,quarter)||
  !qb_regwrite(b,QM_I2C2,0x94,quarter>256?255:quarter-1)||!qb_regwrite(b,QM_I2C2,0x6c,1)||!qb_wait(b,0x9c,1,1))goto bad;
 b->opened=1;unsigned first=0,second=0;
 if(!qb_transfer(b,0x1d,&first,1)||!qb_transfer(b,0x1d,&second,1)||first!=0x53||second!=0x53)goto bad;
 return 1;
bad:(void)qb_close(b);return 0;
}
static int qb_motor_write(void *opaque,unsigned reg,unsigned value)
{return qb_transfer(opaque,reg,&value,0);}
#endif
