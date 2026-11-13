/* SPDX-License-Identifier: GPL-2.0-only
 * Read-only discovery of ONE MBR/FAT32 volume and exact card identity.
 * Authorizes only that volume, never its MBR, other partitions or NOR.
 * Two identical metadata reads are mandatory before a future RW driver may
 * use the permit. This header contains no device or write operation.
 */
#ifndef QC5_SD_VOLUME_H
#define QC5_SD_VOLUME_H
#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/string.h>
typedef u8 uint8_t;typedef u32 uint32_t;typedef u64 uint64_t;
#else
#include <stdint.h>
#include <string.h>
#endif
struct qsv_volume {uint32_t cid[4],card_sectors,first,sectors;unsigned stage,valid;uint8_t mbr[512],boot[512];};
static uint32_t qsv_u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static unsigned qsv_u16(const uint8_t *p){return p[0]|(unsigned)p[1]<<8;}
static int qsv_init(struct qsv_volume *v,const uint32_t cid[4],uint32_t card_sectors)
{
 if(!v||!cid||!(cid[0]|cid[1]|cid[2]|cid[3])||card_sectors<131072)return 0;
 memset(v,0,sizeof *v);memcpy(v->cid,cid,16);v->card_sectors=card_sectors;return 1;
}
static int qsv_mbr(struct qsv_volume *v,const uint8_t b[512])
{
 if(b[510]!=0x55||b[511]!=0xaa)return 0;unsigned found=0;uint32_t first=0,sectors=0;
 for(unsigned n=0;n<4;n++){
  const uint8_t *p=b+446+16*n;uint32_t a=qsv_u32(p+8),count=qsv_u32(p+12);
  if(!p[4]&&!a&&!count)continue;
  if(found++||!a||!count||(p[0]!=0&&p[0]!=0x80)||(p[4]!=0xb&&p[4]!=0xc)||a<2048||count<65536||
   a>v->card_sectors||count>v->card_sectors-a)return 0;first=a;sectors=count;
 }
 if(found!=1)return 0;v->first=first;v->sectors=sectors;return 1;
}
static int qsv_bpb(struct qsv_volume *v,const uint8_t b[512])
{
 unsigned cluster=b[13],reserved=qsv_u16(b+14),fats=b[16],info=qsv_u16(b+48),backup=qsv_u16(b+50);
 uint32_t total=qsv_u32(b+32),fat=qsv_u32(b+36),root=qsv_u32(b+44),hidden=qsv_u32(b+28);
 if(b[510]!=0x55||b[511]!=0xaa||qsv_u16(b+11)!=512||!cluster||cluster>128||(cluster&(cluster-1))||reserved<32||
  !fats||fats>2||qsv_u16(b+17)||qsv_u16(b+19)||qsv_u16(b+22)||qsv_u16(b+42)||
  total!=v->sectors||hidden!=v->first||!fat||fat>total/fats||
  (info!=65535&&(!info||info>=reserved))||(backup!=65535&&(!backup||backup>=reserved)))return 0;
 uint64_t overhead=(uint64_t)reserved+(uint64_t)fats*fat;
 if(overhead>=total)return 0;uint32_t clusters=(total-(uint32_t)overhead)/cluster;
 return clusters>=65525&&clusters<0x0ffffff5u&&(uint64_t)fat*128>=clusters+2&&root>=2&&root<clusters+2;
}
static int qsv_observe(struct qsv_volume *v,const uint32_t cid[4],uint32_t lba,const uint8_t b[512])
{
 if(!v||!cid||!b)return 0;
 if(memcmp(cid,v->cid,16)){v->valid=v->stage=0;return 0;}
 if(lba==0){
  if(v->stage>=2&&!memcmp(v->mbr,b,512))return 1;
  v->valid=0;
  if(!qsv_mbr(v,b)){v->stage=0;return 0;}
  if(v->stage>=1&&!memcmp(v->mbr,b,512)){v->stage=2;return 1;}
  memcpy(v->mbr,b,512);v->stage=1;return 1;
 }
 if(lba==v->first&&v->stage>=2){
  /* FAT changes its dirty/error byte in an already mounted boot sector.
   * Keep the established permit only when every geometry byte is identical
   * and the BPB still validates. A changed geometry needs two fresh reads. */
  if(v->valid&&v->stage==4&&qsv_bpb(v,b)&&!memcmp(v->boot+11,b+11,53)){
   memcpy(v->boot,b,512);return 1;
  }
  v->valid=0;
  if(!qsv_bpb(v,b)){v->stage=2;return 0;}
  if(v->stage>=3&&!memcmp(v->boot,b,512)){v->stage=4;v->valid=1;return 1;}
  memcpy(v->boot,b,512);v->stage=3;return 1;
 }
 return 0;
}
static int qsv_authorize(const struct qsv_volume *v,const uint32_t cid[4],uint32_t first,unsigned blocks,unsigned bytes)
{
 return v&&cid&&v->valid&&v->stage==4&&!memcmp(cid,v->cid,16)&&bytes==512&&blocks&&blocks<=8&&
  first>=v->first&&first-v->first<v->sectors&&blocks<=v->sectors-(first-v->first);
}
#endif
