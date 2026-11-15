/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include "wifi-flash-core.h"
static u8 flash[8192];static int calls,cut_call,cut_byte,erases;
static int rd(void*x,unsigned at,void*p,size_t n){(void)x;assert(at+n<=sizeof flash);memcpy(p,flash+at,n);return 0;}
static int er(void*x,unsigned at){(void)x;assert(at==0||at==4096);memset(flash+at,255,4096);erases++;return 0;}
static int wr(void*x,unsigned at,const void*p,size_t n){(void)x;assert(n==512);calls++;size_t limit=calls==cut_call?(size_t)cut_byte:n;for(size_t i=0;i<limit;i++)flash[at+i]&=((const u8*)p)[i];return calls==cut_call?-1:0;}
static int legacy(const u8*b,const u8 mac[6],struct qc5_wifi_profile*p){
 if(memcmp(b,"QC5WIFI1",8)||b[8]!=1)return 0;
 for(unsigned i=34;i<40;i++)if(b[i])return 0;
 for(unsigned i=136;i<480;i++)if(b[i])return 0;
 return qwf_decode(b,mac,p);
}
int main(void){
 struct qwf_io io={0,rd,er,wr};u8 mac[6]={2,0,0,0,0,1},foreign[6]={2,0,0,0,0,2};
 struct qc5_wifi_profile a={"legacy","legacy-password",0},b={"new-network","new-password",0},got;
 char host[64];assert(qc5_hostname_normalize(host,"Camera-Sala")&&!strcmp(host,"camera-sala"));
 for(const char**v=(const char*[]){"-start","end-","has.dot","bad space","x\ncmd","x/y",NULL};*v;v++)assert(!qc5_hostname_normalize(host,*v));
 char long_name[65];memset(long_name,'a',64);long_name[64]=0;assert(!qc5_hostname_normalize(host,long_name));long_name[63]=0;assert(qc5_hostname_normalize(host,long_name));
 strcpy(b.hostname,"camera-sala");memset(flash,255,sizeof flash);assert(!qwf_store(&io,mac,&a));u8 original[8192];memcpy(original,flash,sizeof flash);
 uint64_t generation;int bank;
 for(int byte=0;byte<512;byte++){
  memcpy(flash,original,sizeof flash);calls=0;cut_call=1;cut_byte=byte;
  assert(qwf_store(&io,mac,&b)<0);assert(qwf_load(&io,mac,&got,&generation,&bank)==1&&qc5_wifi_profile_equal(&a,&got));
 }
 for(int byte=0;byte<512;byte++){
  memcpy(flash,original,sizeof flash);calls=0;cut_call=2;cut_byte=byte;
  assert(!qwf_store(&io,mac,&b));assert(qwf_load(&io,mac,&got,&generation,&bank)==1&&qc5_wifi_profile_equal(&b,&got));
  cut_call=0;assert(!qwf_store(&io,mac,&b));
  assert((legacy(flash,mac,&got)||legacy(flash+4096,mac,&got))&&qc5_wifi_equal(&b,&got)&&!got.hostname[0]);
 }
 cut_call=0;memcpy(flash,original,sizeof flash);assert(!qwf_store(&io,mac,&b));
 assert(qwf_load(&io,mac,&got,&generation,&bank)==1&&qc5_wifi_profile_equal(&b,&got));
 int writes=calls,erase_count=erases;for(int i=0;i<100;i++)assert(!qwf_store(&io,mac,&b));assert(calls==writes&&erases==erase_count);
 assert(!qwf_load(&io,foreign,&got,&generation,&bank));
 b.hostname[0]=0;assert(!qwf_store(&io,mac,&b));assert(qwf_load(&io,mac,&got,&generation,&bank)==1&&!got.hostname[0]);
 assert(legacy(flash+bank*4096,mac,&got));
 puts("hostname banks PASS: legacy migration/downgrade, 1024 byte-cut cases, mirror repair, MAC binding, wear prevention, normalization, automatic reset");
}
