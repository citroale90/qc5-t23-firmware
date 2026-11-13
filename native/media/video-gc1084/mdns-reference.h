/* SPDX-License-Identifier: GPL-2.0-or-later
 * MDNS reference-buffer authority, checked before every live commit.
 * The OS adapter must first verify a matching no-map DT reservation and that
 * no System RAM range overlaps it. No global ISP bypass register is written.
 */
#ifndef QC5_MDNS_REFERENCE_H
#define QC5_MDNS_REFERENCE_H
#include <stddef.h>
#include "mdns-layout.h"
#include "isp-mdns-filter.h"
#define QC5_MDNS_REFERENCE_BYTES (QC5_MDNS_RESERVE_END-QC5_MDNS_RESERVE_START)
struct qc5_mdns_reference {
 struct qc5_mdns_layout plan;
 volatile uint8_t *memory;
 qc5_mdns_read read;
 qc5_mdns_write write;
 void *opaque;
 uint32_t prepared,armed,ever_armed,finished;qc5_mdns_count arms;
 uint32_t config_hash,data_hash,changed_bytes;
 uint32_t fault_offset,fault_value;
};
static int qc5_mdns_reference_fail(struct qc5_mdns_reference *s,uint32_t off,uint32_t value)
{if(s){s->fault_offset=off;s->fault_value=value;}return 0;}
static int qc5_mdns_reference_guards(struct qc5_mdns_reference *s)
{
 if(!s||!s->memory||!qc5_mdns_reserved(&s->plan))return 0;
 for(uint32_t n=0;n<QC5_MDNS_REFERENCE_BYTES;n++){
  uint32_t physical=QC5_MDNS_RESERVE_START+n;
  if(physical>=s->plan.base&&physical-s->plan.base<s->plan.allocated)continue;
  if(s->memory[n]!=0xa5)return qc5_mdns_reference_fail(s,physical,s->memory[n]);
 }
 return 1;
}
static int qc5_mdns_reference_readback(struct qc5_mdns_reference *s)
{
 if(!s||!s->prepared||!s->read||!qc5_mdns_reserved(&s->plan))return 0;
 uint32_t h=2166136261u;s->config_hash=0;
 for(unsigned n=0;n<20;n++){
  struct qc5_mdns_pair r=s->plan.registers[n];uint32_t value=s->read(s->opaque,r.offset);
  if(value!=r.value)return qc5_mdns_reference_fail(s,r.offset,value);
  h=qc5_mdns_hash_word(qc5_mdns_hash_word(h,r.offset),value);
 }
 s->config_hash=h;return h!=0;
}
static int qc5_mdns_reference_prepare(struct qc5_mdns_reference *s,const struct qc5_mdns_layout *plan,
 volatile uint8_t *memory,size_t length,qc5_mdns_read read,qc5_mdns_write write,void *opaque)
{
 /* All authority/idle checks precede both RAM and MMIO writes. */
 if(!s||s->prepared||s->ever_armed||!qc5_mdns_reserved(plan)||!memory||((uintptr_t)memory&3u)||
    length!=QC5_MDNS_REFERENCE_BYTES||!read||!write)return 0;
 if((read(opaque,0x7804)&1u)||read(opaque,0x800)||read(opaque,0xd040))return 0;
 *s=(struct qc5_mdns_reference){.plan=*plan,.memory=memory,.read=read,.write=write,.opaque=opaque};
 for(uint32_t n=0;n<QC5_MDNS_REFERENCE_BYTES;n++)memory[n]=0xa5;
 for(uint32_t n=0;n<plan->allocated;n++)memory[plan->base-QC5_MDNS_RESERVE_START+n]=0;
 for(unsigned n=0;n<20;n++){
  struct qc5_mdns_pair r=plan->registers[n];
  if(!write(opaque,r.offset,r.value))return qc5_mdns_reference_fail(s,r.offset,r.value);
 }
 s->prepared=1;
 if(!qc5_mdns_reference_readback(s)){s->prepared=0;return 0;}
 return qc5_mdns_reference_guards(s);
}
static int qc5_mdns_reference_arm(struct qc5_mdns_reference *s,struct qc5_mdns_state *coefficients)
{
 if(!s||!s->prepared||s->finished||!qc5_mdns_commit_available(s->arms)||!coefficients||!coefficients->ready||!coefficients->profile||
    coefficients->bank>1||coefficients->gain>=25||!s->read||!s->write)return 0;
 uint32_t control=s->read(s->opaque,0x7804);
 if(control!=0x110)return qc5_mdns_reference_fail(s,0x7804,control);
 if(!s->ever_armed&&(s->read(s->opaque,0x800)||s->read(s->opaque,0xd040)))return 0;
 if(coefficients->profile->expected[coefficients->bank][coefficients->gain][0]!=0x111||
    !qc5_mdns_reference_readback(s)||!qc5_mdns_verify(coefficients,s->read,s->opaque)||
    (!s->ever_armed&&!qc5_mdns_reference_guards(s)))return 0;
 /* Sole enable store: exact native control, after verified ownership. */
 s->armed=s->ever_armed=1;++s->arms;
 int issued=s->write(s->opaque,0x7804,0x111);control=s->read(s->opaque,0x7804);
 if(!issued||control!=0x111){
  /* A failed callback can follow a committed store. Keep ownership until a
   * separate disable store AND readback prove that the writer is off. */
  int disabled=s->write(s->opaque,0x7804,0x110);
  if(disabled&&s->read(s->opaque,0x7804)==0x110)s->armed=0;
  return qc5_mdns_reference_fail(s,0x7804,control);
 }
 return 1;
}
static int qc5_mdns_reference_stop(struct qc5_mdns_reference *s)
{
 if(!s||!s->prepared||!s->read||!s->write)return 0;
 uint32_t value=s->read(s->opaque,0x7804);
 if(value!=0x110&&value!=0x111)return qc5_mdns_reference_fail(s,0x7804,value);
 if(!s->write(s->opaque,0x7804,0x110))return qc5_mdns_reference_fail(s,0x7804,0x110);
 value=s->read(s->opaque,0x7804);
 if(value!=0x110)return qc5_mdns_reference_fail(s,0x7804,value);
 s->armed=0;return 1;
}
static int qc5_mdns_reference_finish(struct qc5_mdns_reference *s,struct qc5_mdns_state *coefficients,unsigned stop)
{
 if(!s||!s->prepared||!s->ever_armed||s->armed||s->finished||stop!=31||!s->read||
    s->read(s->opaque,0x7804)!=0x110||s->read(s->opaque,0x800)||s->read(s->opaque,0xd040))return 0;
 if(!qc5_mdns_reference_readback(s)||!qc5_mdns_verify(coefficients,s->read,s->opaque)||!qc5_mdns_reference_guards(s))return 0;
 uint32_t h=2166136261u,changed=0,start=s->plan.base-QC5_MDNS_RESERVE_START;
 for(uint32_t n=0;n<s->plan.allocated;n++){
  uint8_t value=s->memory[start+n];h=(h^value)*16777619u;changed+=value!=0;
 }
 s->data_hash=h;s->changed_bytes=changed;
 if(!changed)return qc5_mdns_reference_fail(s,s->plan.base,0);
 s->finished=1;return 1;
}
#endif
