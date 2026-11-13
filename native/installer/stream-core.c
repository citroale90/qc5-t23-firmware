/* SPDX-License-Identifier: GPL-3.0-only
 * Verified, bounded SD source reads, independent of either Linux kernel.
 * Only KERNEL/ROOT/SYSTEM are writable. No heap or package-sized RAM buffer.
 */
#include "stream-core.h"
#include "qc5_sha256.h"
#include "release.generated.h"
static const char board[]="QC5_YA_CODE_V82-T23-GC1084-AIC8800DC-USB";
static const u8 boot_sha[32]={0xed,0xb9,0xb4,0xa9,0x3a,0x21,0x12,0xd6,0x3f,0xa1,0x24,0x20,0x31,0xa4,0x08,0x93,0x93,0xd1,0x9d,0x91,0xd1,0x43,0xb8,0xa8,0x6d,0x91,0xfe,0xe1,0x66,0x30,0xf6,0x5b};
static const u8 kernel_sha[32]={0xda,0x9e,0xdc,0x55,0x70,0x97,0x42,0x18,0x3d,0x64,0x31,0xfe,0xbe,0xff,0xe9,0x30,0x40,0x8e,0x59,0x19,0xce,0x9a,0x30,0x2d,0x86,0xca,0x9b,0xaa,0x56,0xb3,0x07,0xfc};
static const u8 native_sha[32]=QTU_PAYLOAD_SHA_BYTES;
static u32 le(const u8 *p){return (u32)p[0]|(u32)p[1]<<8|(u32)p[2]<<16|(u32)p[3]<<24;}
static int same(const u8 *a,const u8 *b,u32 n){for(u32 i=0;i<n;i++)if(a[i]!=b[i])return 0;return 1;}
static void event(const struct qfr_io *io,u32 phase,u32 address){if(io->progress)io->progress(io->context,phase,address);}
static int head(const struct qsr_source *s,u8 h[512],int forward)
{
 if(!s||!s->read||s->read(s->context,0,512,h))return 0;
 const char *magic=forward?"QC5U73\0\0":"QC5R73\0\0";
 if(!same(h,(const u8 *)magic,8)||le(h+8)!=512||le(h+12)!=0x750000||le(h+16)!=0x40000||le(h+20)!=0x790000||le(h+24)!=0x8000||le(h+28)!=(forward?2u:1u))return 0;
 for(u32 i=0;i<64;i++)if(h[32+i]!=(i<sizeof board-1?(u8)board[i]:0))return 0;
 if(!same(h+96,boot_sha,32))return 0;
 for(u32 i=forward?256:224;i<480;i++)if(h[i])return 0;
 u8 digest[32];hash(h,480,digest);return same(digest,h+480,32)&&(!forward||same(h+192,native_sha,32));
}
static int source_hash(const struct qsr_source *s,u32 first,u32 bytes,const u8 wanted[32],u8 *buffer)
{
 Sha state;init(&state);u8 digest[32];
 while(bytes){u32 n=bytes<0x8000?bytes:0x8000;if(s->read(s->context,first,n,buffer))return 0;add(&state,buffer,n);first+=n;bytes-=n;}
 finish(&state,digest);return same(digest,wanted,32);
}
static int flash_hash(const struct qfr_io *io,u32 first,u32 bytes,const u8 wanted[32],u8 *buffer,int native)
{
 Sha state;init(&state);u8 digest[32];
 while(bytes){u32 n=bytes<0x8000?bytes:0x8000;if(io->read(io->context,first,n,buffer))return 0;if(native && first==0x4f0000)for(u32 i=0;i<n;i++)buffer[i]=255;add(&state,buffer,n);first+=n;bytes-=n;}
 finish(&state,digest);return same(digest,wanted,32);
}
static int platform(const u8 *h,const struct qfr_io *io,u8 *buffer)
{return flash_hash(io,0,0x3c000,h+96,buffer,0)&&flash_hash(io,0x790000,0x30000,h+128,buffer,0)&&flash_hash(io,0x7c0000,0x40000,h+160,buffer,0);}
static int rollback(const struct qsr_source *s,u8 h[512],const struct qfr_io *io,u8 *buffer)
{
 if(!head(s,h,0)||!source_hash(s,512,0x750000,h+192,buffer)||!source_hash(s,512,0x158000,kernel_sha,buffer))return QFR_BAD_PACKAGE;
 return platform(h,io,buffer)?3:QFR_BAD_UNIT;
}
int qsr_verify(const struct qsr_source *up,const struct qsr_source *back,const struct qfr_io *io,u8 *source,u8 *verify)
{
 if(!io||!io->read||!source||!verify||source==verify)return QFR_BAD_UNIT;
 u8 a[512],b[512];int rc=rollback(back,b,io,verify);if(rc!=3)return rc;
 if(!head(up,a,1)||!same(a+32,b+32,160)||!same(a+224,b+192,32)||!source_hash(up,512,0x750000,native_sha,source))return QFR_BAD_PACKAGE;
 return 3;
}
static int write_source(const struct qsr_source *src,const u8 h[512],const struct qfr_io *io,u8 *source,u8 *verify)
{
 static const u32 spans[3][2]={{0x4f8000,0x298000},{0x198000,0x360000},{0x40000,0x158000}};unsigned changed=0;
 for(unsigned j=0;j<3;j++)for(u32 n=0;n<spans[j][1];n+=0x8000){
  u32 at=spans[j][0]+n,offset=512+at-0x40000;
  if(h[28]==2 && at==0x4f0000)continue; /* Preserve Wi-Fi banks on native upgrades. */
  if(src->read(src->context,offset,0x8000,source))return QFR_READ;
  if(io->read(io->context,at,0x8000,verify))return QFR_READ;
  if(same(source,verify,0x8000))continue;
  event(io,1,at);if(io->erase(io->context,at,0x8000))return QFR_ERASE;
  event(io,2,at);if(io->program(io->context,at,0x8000,source))return QFR_PROGRAM;
  /* Re-read the SD source too: no assumption that a vendor DMA routine
   * preserved the source RAM buffer across its call. */
  event(io,3,at);if(src->read(src->context,offset,0x8000,source)||io->read(io->context,at,0x8000,verify))return QFR_READ;
  if(!same(source,verify,0x8000)){event(io,10,at);return QFR_READBACK;}
  changed=1;event(io,5,at);
 }
 if(!flash_hash(io,0x40000,0x750000,h+192,verify,h[28]==2)||!platform(h,io,verify))return QFR_READBACK;
 event(io,4,0x790000);return changed?QFR_RESTORED:QFR_ALREADY_RESTORED;
}
int qsr_install(const struct qsr_source *up,const struct qsr_source *back,const struct qfr_io *io,u8 *source,u8 *verify)
{
 int rc=qsr_verify(up,back,io,source,verify);if(rc!=3)return rc;
 u8 h[512];if(!io->erase||!io->program||!head(up,h,1))return QFR_BAD_UNIT;
 return write_source(up,h,io,source,verify);
}
int qsr_restore(const struct qsr_source *back,const struct qfr_io *io,u8 *source,u8 *verify)
{
 if(!io||!io->read||!io->erase||!io->program||!source||!verify||source==verify)return QFR_BAD_UNIT;
 u8 h[512];int rc=rollback(back,h,io,verify);if(rc!=3)return rc;
 return write_source(back,h,io,source,verify);
}
int qsr_native_ready(const struct qfr_io *io,u8 *buffer)
{return io&&io->read&&buffer&&flash_hash(io,0,0x3c000,boot_sha,buffer,0)&&flash_hash(io,0x40000,0x750000,native_sha,buffer,1);}
