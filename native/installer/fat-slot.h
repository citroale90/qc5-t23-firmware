/* SPDX-License-Identifier: GPL-3.0-only
 * Locate one existing small 8.3 file in the root of a bounded FAT32 volume.
 * Only that file's first data sector may be replaced. No allocation, FAT,
 * directory, boot-sector or arbitrary caller-supplied LBA writes exist.
 */
#ifndef QC5_FAT_SLOT_H
#define QC5_FAT_SLOT_H
#include "full-recovery.h"
struct qtf_io {void *context;int (*read)(void *,u32,u8 *);int (*write)(void *,u32,const u8 *);};
struct qtf_slot {u32 lba,size;};
static u32 qtf_le(const u8 *p){return (u32)p[0]|(u32)p[1]<<8|(u32)p[2]<<16|(u32)p[3]<<24;}
static u32 qtf_word(const u8 *p){return (u32)p[0]|(u32)p[1]<<8;}
static int qtf_equal(const u8 *a,const u8 *b,u32 n){for(u32 i=0;i<n;i++)if(a[i]!=b[i])return 0;return 1;}
static int qtf_find(const struct qtf_io *io,const char name[11],u32 wanted,struct qtf_slot *slot,u8 sector[512])
{
 if(!io||!io->read||!slot||!wanted||wanted>512||io->read(io->context,0,sector)||qtf_word(sector+510)!=0xaa55)return 0;
 /* One ordinary FAT32 partition, no extended/GPT ambiguity. */
 if(sector[450]!=0x0b&&sector[450]!=0x0c)return 0;
 for(u32 n=462;n<510;n++)if(sector[n])return 0;
 u32 first=qtf_le(sector+454),part=qtf_le(sector+458);if(!first||!part||first>0xffffffffu-part||io->read(io->context,first,sector))return 0;
 if(qtf_word(sector+510)!=0xaa55||qtf_word(sector+11)!=512||qtf_word(sector+17)||qtf_word(sector+22)||qtf_word(sector+42))return 0;
 u32 spc=sector[13],reserved=qtf_word(sector+14),fats=sector[16],total=qtf_le(sector+32),fatsize=qtf_le(sector+36),root=qtf_le(sector+44)&0x0fffffffu,flags=qtf_word(sector+40);
 if(!spc||spc>128||(spc&(spc-1))||reserved<2||!fats||fats>2||!total||total>part||!fatsize||fatsize>total/fats||reserved>=total-fats*fatsize)return 0;
 u32 used=reserved+fats*fatsize,clusters=(total-used)/spc,active=(flags&128)?flags&15:0;
 if(clusters<65525||clusters>0x0fffffeeu||root<2||root>=clusters+2||active>=fats||fatsize<(clusters+2+127)/128)return 0;
 u32 data=first+used,fat=first+reserved+active*fatsize,cluster=root,entries=0;
 for(u32 depth=0;depth<64;depth++){
  if(cluster<2||cluster>=clusters+2)return 0;
  for(u32 page=0;page<spc;page++){
   u32 lba=data+(cluster-2)*spc+page;if(io->read(io->context,lba,sector))return 0;
   for(u32 off=0;off<512;off+=32){
    if(++entries>4096||!sector[off])return 0;
    if(sector[off]==0xe5||sector[off+11]==0x0f)continue;
    if(!qtf_equal(sector+off,(const u8 *)name,11))continue;
    if(sector[off+11]&0x18||qtf_le(sector+off+28)!=wanted)return 0;
    u32 start=(qtf_word(sector+off+20)<<16)|qtf_word(sector+off+26);if(start<2||start>=clusters+2||start==root)return 0;
    u32 nextpage=fat+start/128;if(io->read(io->context,nextpage,sector))return 0;
    u32 next=qtf_le(sector+(start%128)*4)&0x0fffffffu;if(next<0x0ffffff8u)return 0;
    slot->lba=data+(start-2)*spc;slot->size=wanted;return 1;
   }
  }
  if(io->read(io->context,fat+cluster/128,sector))return 0;
  u32 next=qtf_le(sector+(cluster%128)*4)&0x0fffffffu;if(next>=0x0ffffff8u||next==cluster)return 0;cluster=next;
 }
 return 0;
}
static int qtf_replace(const struct qtf_io *io,const char name[11],u32 n,const u8 *expected,const u8 *replacement,u8 sector[512],u8 verify[512])
{
 struct qtf_slot slot;if(!io||!io->write||!expected||!replacement||!qtf_find(io,name,n,&slot,sector)||io->read(io->context,slot.lba,sector)||!qtf_equal(sector,expected,n))return 0;
 for(u32 i=0;i<512;i++)verify[i]=sector[i];for(u32 i=0;i<n;i++)verify[i]=replacement[i];
 if(io->write(io->context,slot.lba,verify)||io->read(io->context,slot.lba,sector))return 0;
 return qtf_equal(sector,verify,512);
}
#endif
