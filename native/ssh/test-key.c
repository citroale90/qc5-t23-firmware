#include <assert.h>
#include <stdio.h>
#include "key-core.h"
static u8 media[8192];static int cutoff=-1,writes,erases;
static int rd(void*c,unsigned at,void*b,size_t n){(void)c;assert(at+n<=8192);memcpy(b,media+at,n);return 0;}
static int er(void*c,unsigned at){(void)c;assert(at==0||at==4096);erases++;memset(media+at,255,4096);return 0;}
static int wr(void*c,unsigned at,const void*b,size_t n){(void)c;assert((at==0||at==4096)&&n==512);writes++;unsigned amount=cutoff<0?512:(unsigned)cutoff;memcpy(media+at,b,amount);return amount==512?0:-1;}
int main(void){u8 mac[6]={0x02,0x00,0x00,0x00,0x00,0x01},key[83],other[83],out[83],saved[8192];struct qks_io io={0,rd,er,wr};uint64_t gen;unsigned mask;memset(key,42,sizeof key);memcpy(key,(u8[]){0,0,0,11,'s','s','h','-','e','d','2','5','5','1','9',0,0,0,64},19);memcpy(other,key,83);other[40]^=1;
 /* Untouched OEM units can retain uniform zero padding through install. */
 memset(media,0,sizeof media);assert(qks_load(&io,mac,out,&gen,&mask)==0);assert(!qks_store(&io,mac,key));assert(qks_load(&io,mac,out,&gen,&mask)==1&&mask==3);
 memset(media,0,sizeof media);media[500]=1;assert(qks_load(&io,mac,out,&gen,&mask)<0);assert(qks_store(&io,mac,key)<0);
 memset(media,255,sizeof media);assert(!qks_load(&io,mac,out,&gen,&mask));assert(!qks_store(&io,mac,key));assert(qks_load(&io,mac,out,&gen,&mask)==1&&mask==3&&gen==2&&!memcmp(key,out,83));memcpy(saved,media,8192);
 int w=writes,e=erases;for(int i=0;i<1000;i++)assert(!qks_store(&io,mac,key));assert(writes==w&&erases==e);assert(qks_store(&io,mac,other)<0&&!memcmp(saved,media,8192));mac[5]^=1;assert(qks_load(&io,mac,out,&gen,&mask)<0);mac[5]^=1;
 for(int bank=0;bank<2;bank++){
  memcpy(media,saved,8192);media[bank*4096+50]^=1;assert(qks_load(&io,mac,out,&gen,&mask)==1&&mask==(unsigned)(bank?1:2)&&!memcmp(key,out,83));assert(!qks_store(&io,mac,key));assert(qks_load(&io,mac,out,&gen,&mask)==1&&mask==3);
 }
 for(int n=0;n<=512;n++){
  memcpy(media,saved,8192);memset(media+4096,255,4096);cutoff=n;(void)qks_store(&io,mac,key);assert(qks_load(&io,mac,out,&gen,&mask)==1&&!memcmp(key,out,83));cutoff=-1;assert(!qks_store(&io,mac,key));assert(qks_load(&io,mac,out,&gen,&mask)==1&&mask==3);
 }
 memcpy(media,saved,8192);qks_encode(media+4096,mac,other,3);assert(qks_load(&io,mac,out,&gen,&mask)<0);assert(qks_store(&io,mac,key)<0);
 puts("ssh_identity_tests_passed redundant_banks=1 same_identity_no_writes_1000boots=1 immutable_identity=1 MAC_binding=1 conflict_rejected=1 repair_and_513_partial_write_boundaries=1");return 0;}
