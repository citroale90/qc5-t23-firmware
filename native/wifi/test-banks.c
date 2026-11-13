#include <stdio.h>
#include <assert.h>
#include "wifi-flash-core.h"
static unsigned char nor[8192];static int fail_at=-1,write_count,erases;static unsigned cuts;
static int rd(void *x,unsigned a,void *b,size_t n){(void)x;if(a>8192||n>8192-a)return -1;memcpy(b,nor+a,n);return 0;}
static int er(void*x,unsigned a){(void)x;assert(a==0||a==4096);erases++;memset(nor+a,255,4096);return 0;}
static int wr(void*x,unsigned a,const void*b,size_t n){(void)x;assert((a==0||a==4096)&&n==512);write_count++;size_t count=fail_at<0?n:(unsigned)fail_at;for(size_t i=0;i<count;i++)nor[a+i]&=((const u8*)b)[i];return fail_at<0?0:-1;}
int main(void){struct qwf_io io={NULL,rd,er,wr};u8 mac[6]={0x02,0x00,0x00,0x00,0x00,0x01},other[6]={0x18,0xef,0x3c,0,0,1};struct qc5_wifi_profile a={"synthetic-net","synthetic-password",0},b={"second-net","second-password",0},p;uint64_t gen;int bank;
 memset(nor,255,sizeof nor);assert(qwf_load(&io,mac,&p,&gen,&bank)==0);assert(!qwf_store(&io,mac,&a));assert(qwf_load(&io,mac,&p,&gen,&bank)==1&&gen==1&&bank==0&&qc5_wifi_equal(&a,&p));assert(!qwf_store(&io,mac,&a)&&erases==1&&write_count==1);
 unsigned char initial[8192];memcpy(initial,nor,8192);
 for(int n=0;n<512;n++){memcpy(nor,initial,8192);fail_at=n;assert(qwf_store(&io,mac,&b)<0);assert(qwf_load(&io,mac,&p,&gen,&bank)==1&&gen==1&&bank==0&&qc5_wifi_equal(&a,&p));cuts++;}
 fail_at=-1;memcpy(nor,initial,8192);assert(!qwf_store(&io,mac,&b));assert(qwf_load(&io,mac,&p,&gen,&bank)==1&&gen==2&&bank==1&&qc5_wifi_equal(&b,&p));assert(qwf_load(&io,other,&p,&gen,&bank)==0);nor[4096+80]^=1;assert(qwf_load(&io,mac,&p,&gen,&bank)==1&&gen==1&&bank==0);
 struct qc5_wifi_profile open={"open-test","",1};assert(!qwf_store(&io,mac,&open));assert(qwf_load(&io,mac,&p,&gen,&bank)==1&&p.open==1&&qc5_wifi_equal(&open,&p));
 printf("bank atomicity PASS: %u byte-boundary power cuts, unchanged-save wear prevention, MAC binding, corrupt-bank fallback, open network\n",cuts);return 0;}
