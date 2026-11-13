/* SPDX-License-Identifier: GPL-3.0-only
 * Two 4KiB banks, immutable Ed25519 identity, byte-verified replication.
 * No device access and no secret output. */
#ifndef QC5_SSH_KEY_CORE_H
#define QC5_SSH_KEY_CORE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef uint8_t u8;typedef uint32_t u32;typedef uint64_t u64;typedef size_t usize;
#include "qc5_sha256.h"
#define QKS_KEY 83u
#define QKS_BANK 4096u
#define QKS_RECORD 512u
struct qks_io{void *ctx;int(*read)(void*,unsigned,void*,size_t);int(*erase)(void*,unsigned);int(*write)(void*,unsigned,const void*,size_t);};
static int qks_key(const u8 *k){static const u8 prefix[19]={0,0,0,11,'s','s','h','-','e','d','2','5','5','1','9',0,0,0,64};return k&&!memcmp(k,prefix,sizeof prefix);}
static uint64_t qks_gen(const u8 *p){uint64_t n=0;for(unsigned i=0;i<8;i++)n|=(uint64_t)p[16+i]<<(8*i);return n;}
static int qks_blank(const u8 *p){for(unsigned i=0;i<512;i++)if(p[i]!=255)return 0;return 1;}
static int qks_decode(const u8 *p,const u8 mac[6],u8 key[QKS_KEY]){
 memset(key,0,QKS_KEY);u8 sum[32];
 if(memcmp(p,"QC5SSH1\0",8)||p[8]!=1||p[9]||p[10]||p[11]||p[12]!=QKS_KEY||p[13]||p[14]||p[15]||!qks_gen(p)||memcmp(p+24,mac,6)||p[30]||p[31]||!qks_key(p+32))return 0;
 for(unsigned i=32+QKS_KEY;i<480;i++)if(p[i])return 0;
 hash(p,480,sum);if(memcmp(sum,p+480,32))return 0;memcpy(key,p+32,QKS_KEY);return 1;
}
static int qks_load(const struct qks_io *io,const u8 mac[6],u8 out[QKS_KEY],uint64_t *gen,unsigned *mask){
 u8 a[512],b[512],ka[QKS_KEY],kb[QKS_KEY];int rc=-1;memset(out,0,QKS_KEY);*gen=0;*mask=0;
 if(io->read(io->ctx,0,a,512)||io->read(io->ctx,4096,b,512))goto done;
 int va=qks_decode(a,mac,ka),vb=qks_decode(b,mac,kb);
 if(va&&vb&&memcmp(ka,kb,QKS_KEY))goto done; /* Never silently rotate identity. */
 if(!va&&!vb){rc=qks_blank(a)&&qks_blank(b)?0:-1;goto done;}
 *mask=(va?1:0)|(vb?2:0);int pick=va&&(!vb||qks_gen(a)>=qks_gen(b))?0:1;
 memcpy(out,pick?kb:ka,QKS_KEY);*gen=qks_gen(pick?b:a);rc=1;
done:memset(ka,0,sizeof ka);memset(kb,0,sizeof kb);memset(a,0,sizeof a);memset(b,0,sizeof b);return rc;
}
static void qks_encode(u8 b[512],const u8 mac[6],const u8 key[QKS_KEY],uint64_t gen){
 memset(b,0,512);memcpy(b,"QC5SSH1\0",8);b[8]=1;b[12]=QKS_KEY;for(unsigned i=0;i<8;i++)b[16+i]=(u8)(gen>>(8*i));memcpy(b+24,mac,6);memcpy(b+32,key,QKS_KEY);hash(b,480,b+480);
}
static int qks_store(const struct qks_io *io,const u8 mac[6],const u8 key[QKS_KEY]){
 u8 old[QKS_KEY],b[512],check[512];uint64_t gen;unsigned mask;int rc=-1;
 if(!qks_key(key)||!io->erase||!io->write)return -1;
 int found=qks_load(io,mac,old,&gen,&mask);if(found<0||found&&memcmp(old,key,QKS_KEY))goto done;
 for(unsigned bank=0;bank<2;bank++){
  if(mask&(1u<<bank))continue;if(gen==UINT64_MAX)goto done;
  qks_encode(b,mac,key,++gen);unsigned at=bank*4096;
  if(io->erase(io->ctx,at)||io->write(io->ctx,at,b,512)||io->read(io->ctx,at,check,512)||memcmp(check,b,512))goto done;
  mask|=1u<<bank;
 }
 rc=0;
done:memset(old,0,sizeof old);memset(b,0,sizeof b);memset(check,0,sizeof check);return rc;
}
#endif
