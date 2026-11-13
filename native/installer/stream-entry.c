/* SPDX-License-Identifier: GPL-3.0-only
 * No complete capsule is loaded in U-Boot's heap (0x80f8c000..0x81f90000).
 * All bounded buffers are below it. No BOOT/ENV/WWW/PARAM write backend.
 */
#include "uboot-call.h"
#include "fat-slot.h"
#include "stream-core.h"
#include "release.generated.h"
u32 q73_uboot_relocation=0xffffffffu;
#define MMC_BUFFER ((u8 *)0x80d60000)
#define SOURCE_BUFFER ((u8 *)0x80d70000)
#define NOR_BUFFER ((u8 *)0x80d80000)
#define MAX_CLUSTERS 1024u
struct chain {u32 lba[MAX_CLUSTERS],count,spc;};
#define files ((struct chain *)0x80dc0000)
static u8 journal[512];static u32 sequence,mode;static int journal_failed;static void *spi;
static u32 crc(const u8 *p,u32 n){u32 c=~0u;while(n--){c^=*p++;for(u32 i=0;i<8;i++)c=(c>>1)^(0xedb88320u&-(c&1));}return ~c;}
static void put(u8 *p,u32 n){p[0]=n;p[1]=n>>8;p[2]=n>>16;p[3]=n>>24;}
static int alive(void)
{
 volatile u32 *w=(volatile u32 *)0xb0002000;
 /* Exact original U-Boot writes at 0x80129140..0x80129160. Counter only. */
 if(w[0]!=51200||w[1]!=1||w[3]!=0x1a||w[2]>=51200)return 0;
 if(w[2]>=64){w[2]=0;__asm__ volatile("sync":::"memory");}return 1;
}
static void hex(char *p,u32 n){u32 at=0;int first=1;for(int s=28;s>=0;s-=4){u32 x=(n>>s)&15;if(x||!first||!s){p[at++]="0123456789abcdef"[x];first=0;}}p[at]=0;}
static u32 append(char *p,u32 at,const char *s){while(*s)p[at++]=*s++;return at;}
static int transfer(u32 address,u32 lba,u32 sectors,int writing)
{
 if(!alive())return -1;char text[72],n[9];u32 at=append(text,0,writing?"mmc write ":"mmc read ");
 hex(n,address);at=append(text,at,n);text[at++]=' ';hex(n,lba);at=append(text,at,n);text[at++]=' ';hex(n,sectors);at=append(text,at,n);text[at]=0;
 return q73_command(text)?-1:0;
}
static int sector_read(void *c,u32 lba,u8 *out){(void)c;if(transfer((u32)MMC_BUFFER,lba,1,0))return -1;for(u32 n=0;n<512;n++)out[n]=MMC_BUFFER[n];return 0;}
static int sector_write(void *c,u32 lba,const u8 *in){(void)c;for(u32 n=0;n<512;n++)MMC_BUFFER[n]=in[n];return transfer((u32)MMC_BUFFER,lba,1,1);}
static const struct qtf_io card={0,sector_read,sector_write};
static int open_chain(const char name[11],struct chain *out)
{
 u8 s[512];if(sector_read(0,0,s)||qtf_word(s+510)!=0xaa55||(s[450]!=0xb&&s[450]!=0xc))return 0;
 for(u32 i=462;i<510;i++)if(s[i])return 0;
 u32 first=qtf_le(s+454),part=qtf_le(s+458);if(!first||!part||first>~0u-part||sector_read(0,first,s))return 0;
 if(qtf_word(s+510)!=0xaa55||qtf_word(s+11)!=512||qtf_word(s+17)||qtf_word(s+22)||qtf_word(s+42)||qtf_word(s+40))return 0;
 u32 spc=s[13],reserved=qtf_word(s+14),nf=s[16],total=qtf_le(s+32),fs=qtf_le(s+36),root=qtf_le(s+44)&0xfffffff;
 if(spc<16||spc>128||(spc&(spc-1))||reserved<2||nf!=2||!total||total>part||!fs||fs>total/2||reserved>=total-2*fs)return 0;
 u32 used=reserved+2*fs,clusters=(total-used)/spc,data=first+used,fat=first+reserved;
 if(clusters<65525||clusters>0xfffffee||fs<(clusters+129)/128||root<2||root>=clusters+2)return 0;
 u32 dirs[64],dc=0,cluster=root,start=0;
 while(dc<64&&!start){
  if(cluster<2||cluster>=clusters+2)return 0;for(u32 j=0;j<dc;j++)if(dirs[j]==cluster)return 0;dirs[dc++]=cluster;
  for(u32 page=0;page<spc&&!start;page++){
   if(sector_read(0,data+(cluster-2)*spc+page,s))return 0;
   for(u32 off=0;off<512;off+=32){
    if(!s[off])return 0;if(s[off]==0xe5||s[off+11]==0xf)continue;
    if(!qtf_equal(s+off,(const u8 *)name,11))continue;
    if((s[off+11]&0x18)||qtf_le(s+off+28)!=QFR_PACKAGE_BYTES)return 0;
    start=(qtf_word(s+off+20)<<16)|qtf_word(s+off+26);break;
   }
  }
  if(!start){if(sector_read(0,fat+cluster/128,s))return 0;cluster=qtf_le(s+(cluster%128)*4)&0xfffffff;if(cluster>=0xffffff8)return 0;}
 }
 if(!start)return 0;
 for(;;){
  if(sector_read(0,fat+cluster/128,s))return 0;u32 next=qtf_le(s+(cluster%128)*4)&0xfffffff;if(next>=0xffffff8)break;
  if(next<2||next>=clusters+2||dc==64)return 0;for(u32 j=0;j<dc;j++)if(dirs[j]==next)return 0;dirs[dc++]=next;cluster=next;
 }
 u32 count=(QFR_PACKAGE_BYTES+spc*512-1)/(spc*512);if(!count||count>MAX_CLUSTERS)return 0;
 struct qtf_slot arm,log;if(!qtf_find(&card,"Q73ARM  BIN",8,&arm,s)||!qtf_find(&card,"Q73LOG  BIN",512,&log,s))return 0;
 cluster=start;out->spc=spc;out->count=count;
 for(u32 i=0;i<count;i++){
  if(cluster<2||cluster>=clusters+2)return 0;u32 lba=data+(cluster-2)*spc;
  if((arm.lba>=lba&&arm.lba-lba<spc)||(log.lba>=lba&&log.lba-lba<spc))return 0;
  for(u32 j=0;j<dc;j++)if(cluster==dirs[j])return 0;for(u32 j=0;j<i;j++)if(out->lba[j]==lba)return 0;out->lba[i]=lba;
  if(sector_read(0,fat+cluster/128,s))return 0;u32 next=qtf_le(s+(cluster%128)*4)&0xfffffff;
  if(sector_read(0,fat+fs+cluster/128,s)||(qtf_le(s+(cluster%128)*4)&0xfffffff)!=next)return 0;
  if(i+1==count){if(next<0xffffff8)return 0;}else if(next>=0xffffff8)return 0;cluster=next;
 }
 return 1;
}
static int source_read(void *ctx,u32 at,u32 n,u8 *out)
{
 struct chain *c=ctx;if(journal_failed||at>QFR_PACKAGE_BYTES||n>QFR_PACKAGE_BYTES-at||(at%512)||(n%512))return -1;
 while(n){u32 index=at/(c->spc*512),within=(at%(c->spc*512))/512,sectors=c->spc-within;if(index>=c->count)return -1;if(sectors>n/512)sectors=n/512;
  if(transfer((u32)MMC_BUFFER,c->lba[index]+within,sectors,0))return -1;u32 done=sectors*512;
  for(u32 i=0;i<done;i++)out[i]=MMC_BUFFER[i];at+=done;n-=done;out+=done;
 }return 0;
}
static int read_flash(void *ctx,u32 at,u32 n,u8 *out)
{if(journal_failed||at>0x800000||n>0x800000-at||!alive())return -1;return q73_uboot_call(*(u32 *)((u8 *)ctx+0x20),(u32)ctx,at,n,(u32)out);}
static int writable(u32 at,u32 n){return n==0x8000&&at>=0x40000&&at<=0x790000-n&&!(at%0x8000);}
static int erase_flash(void *ctx,u32 at,u32 n)
{
 if(journal_failed||!writable(at,n)||!alive())return -1;u32 f=*(u32 *)((u8 *)ctx+0x28);
 int rc=q73_uboot_call(f,(u32)ctx,at,0x4000,0);return rc?rc:!alive()?-1:q73_uboot_call(f,(u32)ctx,at+0x4000,0x4000,0);
}
static int program_flash(void *ctx,u32 at,u32 n,const u8 *data)
{if(journal_failed||!writable(at,n)||!alive())return -1;return q73_uboot_call(*(u32 *)((u8 *)ctx+0x24),(u32)ctx,at,n,(u32)data);}
static int progress_result(u32 phase,u32 at,int result)
{
 u8 next[512],s[512],v[512];if(journal_failed)return 0;for(u32 i=0;i<512;i++)next[i]=journal[i];
 put(next+12,++sequence);put(next+16,mode);put(next+20,phase);put(next+24,at);put(next+28,(u32)result);put(next+508,crc(next,508));
 if(spi){put(next+96,*(u32 *)((u8 *)spi+0xc));put(next+100,*(u32 *)((u8 *)spi+0x10));put(next+104,*(u32 *)((u8 *)spi+0x14));put(next+508,crc(next,508));}
 if(!qtf_replace(&card,"Q73LOG  BIN",512,journal,next,s,v)){journal_failed=1;return 0;}for(u32 i=0;i<512;i++)journal[i]=next[i];return 1;
}
static void progress(void *ctx,u32 phase,u32 at){(void)ctx;if(phase)progress_result(phase,at,0);}
static int consume(const u8 expected[8]){u8 s[512],v[512],zero[8]={0};return qtf_replace(&card,"Q73ARM  BIN",8,expected,zero,s,v);}
static void boot(void)
{if(!q73_command("sf probe")&&!q73_command("sf read 80600000 40000 200000"))(void)q73_command("bootm 80600000");}
__attribute__((noreturn)) void qc5_sd_recovery_start(void)
{
 u8 s[512],marker[8];struct qtf_slot slot;struct qfr_io io={0,read_flash,erase_flash,program_flash,progress};int rc;
 if(!q73_context_valid()||!alive()||!qtf_find(&card,"Q73ARM  BIN",8,&slot,s)||sector_read(0,slot.lba,s))goto halt;
 u32 any=0;for(u32 i=0;i<8;i++){marker[i]=s[i];any|=marker[i];}
 if(any&&(!qtf_equal(marker,(const u8 *)"QC5",3)||!qtf_equal(marker+4,(const u8 *)"73\r\n",4)||(marker[3]!='U'&&marker[3]!='R'&&marker[3]!='C')))goto halt;
 spi=(void *)q73_uboot_call(0x801164f8u+q73_uboot_relocation,0,0,1000000,3);
 if(!spi||!*(u32 *)((u8 *)spi+0x20)||!*(u32 *)((u8 *)spi+0x24)||!*(u32 *)((u8 *)spi+0x28))goto halt;io.context=spi;
 /* A consumed or zero marker may only boot the complete native image.
  * Never fall through to OEM rcS after an incomplete or failed update. */
 if(!any){if(qsr_native_ready(&io,NOR_BUFFER))boot();goto halt;}
 mode=marker[3];
 if(!qtf_find(&card,"Q73LOG  BIN",512,&slot,s)||sector_read(0,slot.lba,journal)||!qtf_equal(journal,(const u8 *)"QT4LOG\r\n",8)||qtf_le(journal+8)!=1||qtf_le(journal+508)!=crc(journal,508))goto halt;
 static const u8 wanted[32]=QTU_PAYLOAD_SHA_BYTES;if(!qtf_equal(journal+32,wanted,32))goto halt;
 sequence=qtf_le(journal+12);if(sequence>0xffff0000u||!progress_result(6,0,0))goto halt;
 if(!open_chain("QC5FULL BIN",&files[0])||(mode!='R'&&!open_chain("QC5UPD  BIN",&files[1]))){progress_result(9,0,-20);goto halt;}
 if(mode!='R')for(u32 i=0;i<files[0].count;i++)for(u32 j=0;j<files[1].count;j++)if(files[0].lba[i]==files[1].lba[j]){progress_result(9,0,-21);goto halt;}
 struct qsr_source back={&files[0],source_read},up={&files[1],source_read};
 rc=mode=='R'?qsr_restore(&back,&io,SOURCE_BUFFER,NOR_BUFFER):mode=='C'?qsr_verify(&up,&back,&io,SOURCE_BUFFER,NOR_BUFFER):qsr_install(&up,&back,&io,SOURCE_BUFFER,NOR_BUFFER);
 if(rc<0){progress_result(9,qtf_le(journal+24),rc);goto halt;}
 if(journal_failed||!progress_result(8,0x790000,rc)||!consume(marker))goto halt;
 if(mode=='U'||mode=='R')boot();
halt:
 /* Keep the processor out of both operating systems after any refusal. */
 for(;;){(void)alive();__asm__ volatile("nop");}
}
