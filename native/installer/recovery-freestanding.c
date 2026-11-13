/* SPDX-License-Identifier: GPL-3.0-only
 * Only compiler support needed by the standalone little-endian MIPS loader.
 */
#include "full-recovery.h"
void *memcpy(void *out,const void *in,usize n)
{u8 *a=out;const u8 *b=in;while(n--)*a++=*b++;return out;}
u64 __lshrdi3(u64 v,int count)
{
 union {u64 all;struct {u32 lo,hi;} part;} a,b;a.all=v;
 if(count<=0)return v;if(count<32){b.part.lo=(a.part.lo>>count)|(a.part.hi<<(32-count));b.part.hi=a.part.hi>>count;}
 else {b.part.lo=count<64?a.part.hi>>(count-32):0;b.part.hi=0;}return b.all;
}
