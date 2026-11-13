/* SPDX-License-Identifier: GPL-3.0-only
 * Full KERNEL/ROOT/SYSTEM SD recovery, independent of either Linux kernel.
 * No heap, Linux, filesystem, saved journal, reset, BOOT or PARAM writes.
 */
#include "full-recovery.h"
#include "qc5_sha256.h"
#include "release.generated.h"
static const char board[]="QC5_YA_CODE_V82-T23-GC1084-AIC8800DC-USB";
static const u8 boot_sha[32]={0xed,0xb9,0xb4,0xa9,0x3a,0x21,0x12,0xd6,0x3f,0xa1,0x24,0x20,0x31,0xa4,0x08,0x93,0x93,0xd1,0x9d,0x91,0xd1,0x43,0xb8,0xa8,0x6d,0x91,0xfe,0xe1,0x66,0x30,0xf6,0x5b};
static const u8 kernel_sha[32]={0xda,0x9e,0xdc,0x55,0x70,0x97,0x42,0x18,0x3d,0x64,0x31,0xfe,0xbe,0xff,0xe9,0x30,0x40,0x8e,0x59,0x19,0xce,0x9a,0x30,0x2d,0x86,0xca,0x9b,0xaa,0x56,0xb3,0x07,0xfc};
static u32 le(const u8 *p){return (u32)p[0]|(u32)p[1]<<8|(u32)p[2]<<16|(u32)p[3]<<24;}
static int same(const u8 *a,const u8 *b,u32 n){for(u32 i=0;i<n;i++)if(a[i]!=b[i])return 0;return 1;}
static int digest(const u8 *p,u32 n,const u8 *expected)
{u8 out[32];hash(p,n,out);return same(out,expected,32);}
static void event(const struct qfr_io *io,u32 phase,u32 address)
{if(io->progress)io->progress(io->context,phase,address);}
static int read_hash(const struct qfr_io *io,u8 *scratch,u32 first,u32 bytes,const u8 *expected)
{
 Sha s;init(&s);u8 out[32];
 while(bytes){u32 n=bytes<QFR_ERASE_BYTES?bytes:QFR_ERASE_BYTES;if(io->read(io->context,first,n,scratch))return 0;add(&s,scratch,n);event(io,0,first);first+=n;bytes-=n;}
 finish(&s,out);return same(out,expected,32);
}
static int package_ok(const u8 *p,u32 bytes)
{
 static const u8 magic[8]={'Q','C','5','R','7','3',0,0};
 if(!p||bytes!=QFR_PACKAGE_BYTES||!same(p,magic,8)||le(p+8)!=512||le(p+12)!=QFR_END-QFR_BEGIN||le(p+16)!=QFR_BEGIN||le(p+20)!=QFR_END||le(p+24)!=QFR_ERASE_BYTES||le(p+28)!=1)return 0;
 u32 n=0;for(;n<sizeof board-1;n++)if(p[32+n]!=(u8)board[n])return 0;for(;n<64;n++)if(p[32+n])return 0;
 if(!same(p+96,boot_sha,32))return 0;
 for(n=224;n<480;n++)if(p[n])return 0;
 return digest(p,480,p+480)&&digest(p+512,QFR_END-QFR_BEGIN,p+192)&&digest(p+512,0x158000,kernel_sha);
}
static int platform_ok(const u8 *p,const struct qfr_io *io,u8 *scratch)
{return read_hash(io,scratch,0,0x3c000,p+96)&&read_hash(io,scratch,0x790000,0x30000,p+128)&&read_hash(io,scratch,0x7c0000,0x40000,p+160);}
static int write_payload(const u8 *p,const struct qfr_io *io,u8 *scratch)
{
 /* Restore the original kernel last. An armed bootloader transport must
  * remain in recovery until all three spans pass final verification. */
 static const u32 spans[3][2]={{0x4f8000,0x298000},{0x198000,0x360000},{0x40000,0x158000}};
 unsigned changed=0;
 for(unsigned span=0;span<3;span++)for(u32 off=0;off<spans[span][1];off+=QFR_ERASE_BYTES){
  u32 address=spans[span][0]+off;const u8 *target=p+512+address-QFR_BEGIN;
  if(address<QFR_BEGIN||address>QFR_END-QFR_ERASE_BYTES||address%QFR_ERASE_BYTES)return QFR_BAD_UNIT;
  if(io->read(io->context,address,QFR_ERASE_BYTES,scratch))return QFR_READ;
  if(same(scratch,target,QFR_ERASE_BYTES)){event(io,0,address);continue;}
  event(io,1,address);if(io->erase(io->context,address,QFR_ERASE_BYTES))return QFR_ERASE;
  event(io,2,address);if(io->program(io->context,address,QFR_ERASE_BYTES,target))return QFR_PROGRAM;
  event(io,3,address);if(io->read(io->context,address,QFR_ERASE_BYTES,scratch))return QFR_READ;
  if(!same(scratch,target,QFR_ERASE_BYTES))return QFR_READBACK;changed=1;event(io,5,address);
 }
 if(!read_hash(io,scratch,QFR_BEGIN,QFR_END-QFR_BEGIN,p+192)||!platform_ok(p,io,scratch))return QFR_READBACK;
 event(io,4,QFR_END);return changed?QFR_RESTORED:QFR_ALREADY_RESTORED;
}

int qfr_restore(const u8 *p,u32 bytes,const struct qfr_io *io,u8 *scratch,u32 capacity)
{
 if(!io||!scratch||capacity<QFR_ERASE_BYTES||!io->read||!io->erase||!io->program)return QFR_BAD_UNIT;
 if(!package_ok(p,bytes))return QFR_BAD_PACKAGE;
 if(!platform_ok(p,io,scratch))return QFR_BAD_UNIT;
 return write_payload(p,io,scratch);
}
static int update_ok(const u8 *p,u32 bytes,const u8 *backup,u32 backup_bytes)
{
 static const u8 magic[8]={'Q','C','5','U','7','3',0,0};
 static const u8 target[32]=QTU_PAYLOAD_SHA_BYTES;
 if(!package_ok(backup,backup_bytes)||!p||bytes!=QFR_PACKAGE_BYTES||!same(p,magic,8)||
    le(p+8)!=512||le(p+12)!=QFR_END-QFR_BEGIN||le(p+16)!=QFR_BEGIN||le(p+20)!=QFR_END||le(p+24)!=QFR_ERASE_BYTES||le(p+28)!=2)return 0;
 for(u32 n=32;n<192;n++)if(p[n]!=backup[n])return 0;
 if(!same(p+192,target,32)||!same(p+224,backup+192,32))return 0;
 for(u32 n=256;n<480;n++)if(p[n])return 0;
 return digest(p,480,p+480)&&digest(p+512,QFR_END-QFR_BEGIN,target);
}
int qtu_verify(const u8 *p,u32 bytes,const u8 *backup,u32 backup_bytes,const struct qfr_io *io,u8 *scratch,u32 capacity)
{
 if(!io||!scratch||capacity<QFR_ERASE_BYTES||!io->read)return QFR_BAD_UNIT;
 if(!update_ok(p,bytes,backup,backup_bytes))return QFR_BAD_PACKAGE;
 if(!platform_ok(p,io,scratch))return QFR_BAD_UNIT;
 return 3;
}
int qtu_apply(const u8 *p,u32 bytes,const u8 *backup,u32 backup_bytes,const struct qfr_io *io,u8 *scratch,u32 capacity)
{
 int result=qtu_verify(p,bytes,backup,backup_bytes,io,scratch,capacity);if(result!=3)return result;
 if(!io->erase||!io->program)return QFR_BAD_UNIT;
 return write_payload(p,io,scratch);
}
