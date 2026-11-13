/* SPDX-License-Identifier: GPL-2.0-or-later
 * The OEM calls 0x7804=0x111 a register trigger. Its bit 0 can self-clear;
 * 0x110 alone is not evidence that the MDNS algorithm has stopped.
 * Coefficients are verified after the commit, buffer addresses before it.
 */
#ifndef QC5_MDNS_STROBE_H
#define QC5_MDNS_STROBE_H
#include "mdns-reference.h"
struct qc5_mdns_strobe {uint32_t active_hash,stopped,stable_data,stop_commits;};
static uint32_t qc5_mdns_observed(uint32_t off,uint32_t value)
{
 if(off==0x7804)return value&~1u;
#ifdef QC5_MDNS_READBACK
 /* 0x7818 is the OEM refresh command/status port, not a coefficient.
  * OEM refresh writes exactly 0x21; bit 0 self-clears. Only commanded bit 5
  * has stable readback meaning in this path. Observed status includes 0x120,
  * 0x1020 and 0x10020 after valid DMA completion. Do not compare unknown
  * status/reserved bits with the command word. mdns_temporal_write rejects
  * any other command BEFORE MMIO; all coefficient/DMA checks stay exact. */
 if(off==0x7818)return value&0x20u;
#endif
#ifdef QC5_MDNS_STA
 /* H39's complete first-frame snapshot and the native running ISP both
  * read bit 0 clear at these two STA ports despite native stores of 0x14f1.
  * No other STA port or configuration bit is excluded from comparison. */
 if(off==0x7890||off==0x78a8)return value&~1u;
#endif
 return value;
}
static uint32_t qc5_mdns_bypass_value(uint32_t off,uint32_t expected)
{
 if(off==0x7810)return 0x10001;
 if(off==0x7814)return 0xf01100;
 if(off==0x7808)return 0;
 if(off==0x7818)return 0x21;
 return expected;
}
static int qc5_mdns_strobe_verify(struct qc5_mdns_reference *s,struct qc5_mdns_state *c,unsigned stopped)
{
 if(!s||!c||!s->read||!c->ready||!c->profile||c->bank>1||c->gain>=25||stopped>1)return 0;
 c->read_hash=c->fault_offset=c->fault_value=0;uint32_t h=2166136261u;
 for(unsigned n=0;n<QC5_MDNS_READ;n++){
  uint32_t off=qc5_mdns_read_offsets[n],value=s->read(s->opaque,off);
  uint32_t expected=qc5_mdns_hold_dma(off,c->profile->expected[c->bank][c->gain][n]);
  if(stopped)expected=qc5_mdns_bypass_value(off,expected);
  value=qc5_mdns_observed(off,value);expected=qc5_mdns_observed(off,expected);
  if(value!=expected){c->fault_offset=off;c->fault_value=value;return 0;}
  h=qc5_mdns_hash_word(qc5_mdns_hash_word(h,off),value);
 }
 c->read_hash=h;return h!=0;
}
static int qc5_mdns_strobe_commit(struct qc5_mdns_strobe *state,struct qc5_mdns_reference *s,struct qc5_mdns_state *c)
{
 if(!state||!s||!c||!s->prepared||s->finished||state->stopped||!qc5_mdns_commit_available(s->arms)||!s->read||!s->write||
    !c->ready||!c->profile||c->bank>1||c->gain>=25||!c->profile->checksum||
    qc5_mdns_checksum(c->profile)!=c->profile->checksum)return 0;
 uint32_t control=s->read(s->opaque,0x7804);
 if(control!=0x110)return qc5_mdns_reference_fail(s,0x7804,control);
 if(!s->ever_armed&&(s->read(s->opaque,0x800)||s->read(s->opaque,0xd040)))return 0;
 if(!qc5_mdns_reference_readback(s)||(!s->ever_armed&&!qc5_mdns_reference_guards(s)))return 0;
 /* Preserve ownership even if the store commits but its confirmation fails. */
 s->armed=s->ever_armed=1;++s->arms;
 if(!s->write(s->opaque,0x7804,0x111))return qc5_mdns_reference_fail(s,0x7804,0x111);
 for(unsigned attempt=0;attempt<32;attempt++){
  if(!qc5_mdns_strobe_verify(s,c,0))continue;
  if(!qc5_mdns_reference_readback(s))return 0;
  return 1;
 }
 return 0;
}
static int qc5_mdns_strobe_stop(struct qc5_mdns_strobe *state,struct qc5_mdns_reference *s,struct qc5_mdns_state *c)
{
 if(!state||!s||!s->prepared||!s->read||!s->write)return 0;
 if(s->read(s->opaque,0x800)||s->read(s->opaque,0xd040))return 0;
 if(!s->ever_armed)return !(s->read(s->opaque,0x7804)&1u);
 int active_valid=qc5_mdns_strobe_verify(s,c,0);
 if(active_valid)state->active_hash=c->read_hash;
 /* Exact native bypass sequence, with writes held until its trigger. */
 static const struct qc5_mdns_pair bypass[]={
  {0x7804,0x110},{0x7810,0x10001},{0x7814,0xf01100},{0x7808,0},{0x7818,0x21},{0x7804,0x111}
 };
 int written=1;
 for(unsigned n=0;n<sizeof bypass/sizeof *bypass;n++)written&=s->write(s->opaque,bypass[n].offset,bypass[n].value);
 ++state->stop_commits;
 if(!written)return 0;
 unsigned bypass_valid=0;
 for(unsigned attempt=0;attempt<32&&!bypass_valid;attempt++){
  bypass_valid=1;
  for(unsigned n=1;n<=4;n++)bypass_valid&=qc5_mdns_observed(bypass[n].offset,s->read(s->opaque,bypass[n].offset))==qc5_mdns_observed(bypass[n].offset,bypass[n].value);
  bypass_valid&=(s->read(s->opaque,0x7804)&~1u)==0x110;
 }
 if(!bypass_valid||!s->write(s->opaque,0x7804,0x110)||s->read(s->opaque,0x7804)!=0x110)return 0;
 s->armed=0;state->stopped=1;
 return active_valid&&qc5_mdns_strobe_verify(s,c,1)&&qc5_mdns_reference_readback(s);
}
static uint32_t qc5_mdns_strobe_data(struct qc5_mdns_reference *s,uint32_t *changed)
{
 uint32_t h=2166136261u,count=0,start=s->plan.base-QC5_MDNS_RESERVE_START;
 for(unsigned n=0;n<s->plan.allocated;n++){uint8_t v=s->memory[start+n];h=(h^v)*16777619u;count+=v!=0;}
 *changed=count;return h;
}
static int qc5_mdns_strobe_finish(struct qc5_mdns_strobe *state,struct qc5_mdns_reference *s,struct qc5_mdns_state *c,unsigned stop)
{
 if(!state||!s||!s->prepared||!s->memory||!s->ever_armed||s->armed||s->finished||!state->stopped||
    !state->active_hash||stop!=31||!s->read||s->read(s->opaque,0x800)||s->read(s->opaque,0xd040))return 0;
 if(!qc5_mdns_reference_readback(s)||!qc5_mdns_strobe_verify(s,c,1)||!qc5_mdns_reference_guards(s))return 0;
 uint32_t changed=0,again=0;uint32_t h=qc5_mdns_strobe_data(s,&changed);
 uint32_t second=qc5_mdns_strobe_data(s,&again);
 if(!changed||h!=second||changed!=again||!qc5_mdns_reference_guards(s))return 0;
 s->data_hash=h;s->changed_bytes=changed;s->finished=1;state->stable_data=1;return 1;
}
#endif
