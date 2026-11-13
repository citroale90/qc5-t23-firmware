/* SPDX-License-Identifier: GPL-2.0-only
 * Opt-in single-sector CMD24 extension, not enabled in any camera image.
 * Uses the already proven T23 FIFO protocol. The first bring-up permits
 * only the data sectors of a separately backed-up diagnostic regular file.
 * No erase, interrupts, PDMA, GPIO, PLL, SFC or flash writes.
 */
#ifndef QC5_MSC_WRITE_PIO_H
#define QC5_MSC_WRITE_PIO_H
#include "msc-ro-pio.h"
struct qmw_permit {unsigned first_lba,sectors;};
static int qmw_cid_matches(const unsigned actual[4],const unsigned expected[4])
{
 if(!actual||!expected)return 0;unsigned nonzero=0,different=0;
 for(unsigned n=0;n<4;n++){nonzero|=expected[n];different|=actual[n]^expected[n];}
 return nonzero&&!different;
}
static int qmw_plan(unsigned cmd,unsigned arg,unsigned bytes,unsigned blocks,unsigned blockaddr,
 unsigned index,const struct qmw_permit *permit,unsigned *address,unsigned *offset)
{
 if(!permit||!address||!offset||permit->first_lba<2048||!permit->sectors||permit->sectors>0x7fffffffu||
  permit->first_lba>~0u-(permit->sectors-1)||bytes!=512||!blocks||blocks>8||index>=blocks||
  blockaddr>1||(cmd!=24&&cmd!=25)||(cmd==24&&blocks!=1)||(!blockaddr&&(arg&511)))return QMR_DENIED;
 unsigned first=blockaddr?arg:arg/512;
 if(first<permit->first_lba||first-permit->first_lba>=permit->sectors||
  blocks>permit->sectors-(first-permit->first_lba)||
  (!blockaddr&&first+blocks-1>~0u/512))return QMR_DENIED;
 *address=arg+index*(blockaddr?1:512);*offset=index*512;return QMR_OK;
}
static int qmw_wait(struct qmr_io *io,unsigned offset,unsigned mask,int set,unsigned long long start)
{
 for(unsigned tries=0;tries<1000000;tries++){
  unsigned long long now=io->now_us(io->opaque);
  if(now<start)return QMR_IO;if(now-start>=1000000)return QMR_TIMEOUT;
  if(io->read(io->opaque,4)&0x3f)return QMR_IO;
  unsigned bits=io->read(io->opaque,offset)&mask;
  if(set?bits==mask:bits==0)return QMR_OK;io->pause(io->opaque);
 }
 return QMR_TIMEOUT;
}
static int qmw_sector(struct qmr_io *io,unsigned address,unsigned blockaddr,unsigned format,
 const unsigned char *input,const struct qmw_permit *permit,unsigned response[4])
{
 unsigned checked,offset;
 if(!io||!io->read||!io->write||!io->now_us||!io->pause||!input||!response||
  format&~(128u|64u|7u)||(format&7)!=1||
  qmw_plan(24,address,512,1,blockaddr,0,permit,&checked,&offset))return QMR_DENIED;
 unsigned long long started=io->now_us(io->opaque);if(!started)return QMR_IO;
 io->write(io->opaque,0x24,~0u);io->write(io->opaque,0x28,~0u);io->write(io->opaque,0x44,0);
 io->write(io->opaque,0x18,512);io->write(io->opaque,0x1c,1);
 io->write(io->opaque,0x10,255);io->write(io->opaque,0x14,0xffffff);
 io->write(io->opaque,0x2c,24);io->write(io->opaque,0x30,checked);
 io->write(io->opaque,0x0c,format|8u|16u);io->write(io->opaque,0,6);
 int result=qmw_wait(io,0x28,4,1,started);if(result)return result;
 unsigned a=io->read(io->opaque,0x34)&65535u,b=io->read(io->opaque,0x34)&65535u,c=io->read(io->opaque,0x34)&65535u;
 response[0]=(a<<24)|(b<<8)|(c&255);io->write(io->opaque,0x28,4);
 if(response[0]&0xfdffe008u)return QMR_IO;
 for(unsigned at=0;at<512;at+=4){
  result=qmw_wait(io,4,128,0,started);if(result)return result;
  unsigned word=(unsigned)input[at]|(unsigned)input[at+1]<<8|(unsigned)input[at+2]<<16|(unsigned)input[at+3]<<24;
  io->write(io->opaque,0x3c,word);
 }
 /* DATA_DONE alone is not enough: wait for card programming completion.
  * Both indications are acknowledged only after both have been observed. */
 result=qmw_wait(io,0x28,3,1,started);
 if(!result)io->write(io->opaque,0x28,3);
 return result;
}
#endif
