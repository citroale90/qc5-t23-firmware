/* SPDX-License-Identifier: GPL-3.0-only
 * Exercise actual stream-core, mock NOR and ordinary source files only.
 */
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "stream-core.h"
static u8 flash[0x800000],before[0x800000],target[0x750000];
static unsigned erases,programs,events,case_count,max_source_read;static int fault,corrupt_source,cut_after;
static int source(void *ctx,u32 at,u32 n,u8 *out)
{if(at>QFR_PACKAGE_BYTES||n>QFR_PACKAGE_BYTES-at||n>32768||at%512||n%512)abort();if(n>max_source_read)max_source_read=n;if(pread(*(int *)ctx,out,n,at)!=(int)n)return -1;if(corrupt_source&&at>=512)out[0]^=1;return 0;}
static int read_nor(void *ctx,u32 at,u32 n,u8 *out)
{(void)ctx;if(at>0x800000||n>0x800000-at||n>32768)abort();memcpy(out,flash+at,n);return 0;}
static int erase_nor(void *ctx,u32 at,u32 n)
{(void)ctx;if(at<0x40000||at>0x790000-n||n!=32768||at%32768)abort();erases++;if(fault==1||(cut_after&&programs>=(unsigned)cut_after))return -1;memset(flash+at,255,n);return 0;}
static int program_nor(void *ctx,u32 at,u32 n,const u8 *in)
{(void)ctx;if(at<0x40000||at>0x790000-n||n!=32768||at%32768)abort();programs++;if(fault==2)return -1;for(u32 i=0;i<n;i++)if((flash[at+i]&in[i])!=in[i])abort();memcpy(flash+at,in,n);if(fault==3)flash[at]^=1;return 0;}
static void progress(void *ctx,u32 phase,u32 at){(void)ctx;(void)phase;(void)at;events++;}
static void need(int yes,const char *name){if(!yes){fprintf(stderr,"FAIL %s\n",name);exit(1);}case_count++;}
static int native_exact(void){return !memcmp(flash+0x40000,target,0x4b0000)&&!memcmp(flash+0x4f8000,target+0x4b8000,0x298000);}
static void reset(void){memcpy(flash,before,sizeof flash);erases=programs=events=0;fault=corrupt_source=cut_after=0;}
static void protected(void){need(!memcmp(flash,before,0x40000)&&!memcmp(flash+0x790000,before+0x790000,0x70000),"protected regions");}
int main(int argc,char **argv)
{
 if(argc!=4)return 64;int snap=open(argv[1],O_RDONLY),upfd=open(argv[2],O_RDONLY),backfd=open(argv[3],O_RDONLY);need(snap>=0&&upfd>=0&&backfd>=0,"input files");
 need(read(snap,before,sizeof before)==sizeof before,"snapshot");need(pread(upfd,target,sizeof target,512)==sizeof target,"target");
 struct qsr_source up={&upfd,source},back={&backfd,source};struct qfr_io io={0,read_nor,erase_nor,program_nor,progress};u8 a[32768],b[32768];int rc;
 reset();rc=qsr_verify(&up,&back,&io,a,b);need(rc==3&&!erases&&!programs,"read-only preflight");protected();
 rc=qsr_install(&up,&back,&io,a,b);need(rc==1&&native_exact(),"native install");protected();need(qsr_native_ready(&io,b),"native complete gate");
 memset(flash+0x4f0000,0xa5,8192);need(qsr_native_ready(&io,b),"config excluded from firmware hash");erases=programs=0;rc=qsr_install(&up,&back,&io,a,b);need(rc==2&&!erases&&!programs,"idempotent install");need(flash[0x4f0000]==0xa5&&flash[0x4f1fff]==0xa5,"upgrade preserves WiFi config");
 rc=qsr_restore(&back,&io,a,b);need(rc==1&&!memcmp(flash,before,sizeof flash),"complete recovery");need(!qsr_native_ready(&io,b),"stock not accepted as native");
 erases=programs=0;rc=qsr_restore(&back,&io,a,b);need(rc==2&&!erases&&!programs,"idempotent restore");
 reset();flash[0x7ffe00]^=1;rc=qsr_install(&up,&back,&io,a,b);need(rc==QFR_BAD_UNIT&&!erases&&!programs,"other unit rejected");
 reset();corrupt_source=1;rc=qsr_install(&up,&back,&io,a,b);need(rc==QFR_BAD_PACKAGE&&!erases&&!programs,"damaged source rejected");
 for(int f=1;f<=3;f++){
  reset();fault=f;rc=qsr_install(&up,&back,&io,a,b);need(rc==(f==1?QFR_ERASE:f==2?QFR_PROGRAM:QFR_READBACK),"I/O failure detected");protected();
  fault=0;rc=qsr_restore(&back,&io,a,b);need(rc>0&&!memcmp(flash,before,sizeof flash),"recovery after I/O fault");
 }
 for(int n=1;n<=3;n++){
  reset();cut_after=n*31;rc=qsr_install(&up,&back,&io,a,b);need(rc==QFR_ERASE,"power cut boundary");protected();
  cut_after=0;rc=qsr_install(&up,&back,&io,a,b);need(rc>0&&native_exact(),"resume partial install");protected();
  rc=qsr_restore(&back,&io,a,b);need(rc>0&&!memcmp(flash,before,sizeof flash),"restore resumed install");
 }
 need(max_source_read<=32768,"bounded stream reads");
 printf("{\"passed\":true,\"checks\":%u,\"largest_source_read\":%u,\"physical_flash_written\":false}\n",case_count,max_source_read);return 0;
}
