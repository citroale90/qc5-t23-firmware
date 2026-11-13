/* SPDX-License-Identifier: GPL-3.0-only
 * Two independent 4 KiB erase banks. No filesystem, heap or secret logging.
 * A failed erase/program leaves the previously verified bank untouched.
 */
#ifndef QWF_CORE_H
#define QWF_CORE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "qc5_wifi_profile.h"
typedef uint8_t u8;typedef uint32_t u32;typedef uint64_t u64;typedef size_t usize;
#include "qc5_sha256.h"
#define QWF_BANK 4096u
#define QWF_RECORD 512u
struct qwf_io {void *ctx;int (*read)(void*,unsigned,void*,size_t);int (*erase)(void*,unsigned);int (*write)(void*,unsigned,const void*,size_t);};
static uint64_t qwf_gen(const u8 *b){uint64_t v=0;for(unsigned i=0;i<8;i++)v|=(uint64_t)b[16+i]<<(8*i);return v;}
static int qwf_decode(const u8 *b,const u8 mac[6],struct qc5_wifi_profile *p)
{
 u8 sha[32];memset(p,0,sizeof *p);
 if(memcmp(b,"QC5WIFI1",8)||b[8]!=1||b[9]||b[10]||b[11]!=2||!qwf_gen(b)||memcmp(b+24,mac,6)||b[30]>1||b[31]||!b[32]||b[32]>32||b[33]>63)return 0;
 for(unsigned i=34;i<40;i++)if(b[i])return 0;
 for(unsigned i=12;i<16;i++)if(b[i])return 0;
 for(unsigned i=40+b[32];i<72;i++)if(b[i])return 0;
 for(unsigned i=72+b[33];i<136;i++)if(b[i])return 0;
 for(unsigned i=136;i<480;i++)if(b[i])return 0;
 hash(b,480,sha);if(memcmp(sha,b+480,32))return 0;
 memcpy(p->ssid,b+40,b[32]);memcpy(p->password,b+72,b[33]);p->open=b[30];
 if(strlen(p->ssid)!=b[32]||strlen(p->password)!=b[33]||!qc5_wifi_valid(p)){memset(p,0,sizeof *p);return 0;}return 1;
}
static int qwf_load(const struct qwf_io *io,const u8 mac[6],struct qc5_wifi_profile *p,uint64_t *generation,int *bank)
{
 u8 a[512],b[512];struct qc5_wifi_profile pa,pb;int rc=-1;
 memset(p,0,sizeof *p);*generation=0;*bank=-1;
 if(io->read(io->ctx,0,a,512)||io->read(io->ctx,QWF_BANK,b,512))goto done;
 int va=qwf_decode(a,mac,&pa),vb=qwf_decode(b,mac,&pb);
 if(va&&vb&&qwf_gen(a)==qwf_gen(b)&&memcmp(a,b,512))goto done;
 if(!va&&!vb){rc=0;goto done;}
 *bank=va&&(!vb||qwf_gen(a)>=qwf_gen(b))?0:1;
 *p=*bank?pb:pa;*generation=qwf_gen(*bank?b:a);rc=1;
done:memset(a,0,sizeof a);memset(b,0,sizeof b);memset(&pa,0,sizeof pa);memset(&pb,0,sizeof pb);return rc;
}
static int qwf_store(const struct qwf_io *io,const u8 mac[6],const struct qc5_wifi_profile *p)
{
 struct qc5_wifi_profile old;u8 b[512],check[512];uint64_t gen;int bank,rc=-1;
 if(!qc5_wifi_valid(p)||!io->erase||!io->write)return -1;
 int found=qwf_load(io,mac,&old,&gen,&bank);if(found<0)goto done;
 if(found&&qc5_wifi_equal(&old,p)){rc=0;goto done;}
 if(gen==UINT64_MAX)goto done;gen++;
 memset(b,0,sizeof b);memcpy(b,"QC5WIFI1",8);b[8]=1;b[11]=2;
 for(unsigned i=0;i<8;i++)b[16+i]=(u8)(gen>>(8*i));
 memcpy(b+24,mac,6);b[30]=(u8)p->open;b[32]=(u8)strlen(p->ssid);b[33]=(u8)strlen(p->password);
 memcpy(b+40,p->ssid,b[32]);memcpy(b+72,p->password,b[33]);hash(b,480,b+480);
 unsigned at=bank==0?QWF_BANK:0;
 if(io->erase(io->ctx,at)||io->write(io->ctx,at,b,512)||io->read(io->ctx,at,check,512)||memcmp(b,check,512))goto done;
 rc=0;
done:memset(&old,0,sizeof old);memset(b,0,sizeof b);memset(check,0,sizeof check);return rc;
}
#endif
