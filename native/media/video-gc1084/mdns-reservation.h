/* SPDX-License-Identifier: GPL-2.0-or-later
 * Validate kernel-provided DT and iomem authority before mapping MDNS RAM.
 * No allocation, mapping, MMIO or device access is performed by this parser.
 */
#ifndef QC5_MDNS_RESERVATION_H
#define QC5_MDNS_RESERVATION_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "mdns-layout.h"
static uint32_t qc5_mdns_be32(const uint8_t *p)
{return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
/* reg, parent address/size cells, /memory@0/reg: exact H33 authority. */
static int qc5_mdns_dt_authority(const uint8_t *data,size_t bytes,int no_map_present,size_t no_map_bytes)
{
 static const uint32_t expected[]={QC5_MDNS_RESERVE_START,QC5_MDNS_RESERVE_END-QC5_MDNS_RESERVE_START,1,1,0,
  QC5_MDNS_RESERVE_START,QC5_CAPTURE_RAM_END,33554432u-QC5_CAPTURE_RAM_END};
 if(!data||bytes!=sizeof expected||!no_map_present||no_map_bytes)return 0;
 for(unsigned n=0;n<sizeof expected/sizeof expected[0];n++)if(qc5_mdns_be32(data+4*n)!=expected[n])return 0;
 return 1;
}
static int qc5_mdns_hex32(const char **cursor,const char *end,uint32_t *value)
{
 const char *p=*cursor;unsigned digits=0;uint32_t result=0;
 while(p<end){
  unsigned char c=(unsigned char)*p;unsigned v;
  if(c>='0'&&c<='9')v=c-'0';else if(c>='a'&&c<='f')v=c-'a'+10;else if(c>='A'&&c<='F')v=c-'A'+10;else break;
  if(++digits>16||result>UINT32_MAX/16u)return 0;
  result=result*16u+v;++p;
 }
 if(!digits)return 0;*cursor=p;*value=result;return 1;
}
static int qc5_mdns_iomem_authority(const char *data,size_t bytes)
{
 if(!data||!bytes||bytes>8192||data[bytes-1]!='\n'||memchr(data,0,bytes))return 0;
 const char *p=data,*limit=data+bytes;unsigned banks=0;
 while(p<limit){
  const char *end=memchr(p,'\n',(size_t)(limit-p));if(!end||end-p>255)return 0;
  while(p<end&&(*p==' '||*p=='\t'))++p;
  uint32_t start,last;
  if(!qc5_mdns_hex32(&p,end,&start)||p==end||*p++!='-'||!qc5_mdns_hex32(&p,end,&last)||start>last)return 0;
  while(p<end&&*p==' ')++p;if(p==end||*p++!=':')return 0;
  while(p<end&&*p==' ')++p;
  const char *label=p,*trim=end;while(trim>label&&trim[-1]==' ')--trim;
  if(trim==label)return 0;
  if(trim-label==10&&!memcmp(label,"System RAM",10)){
   unsigned bit;
   if(start==0&&last==QC5_MDNS_RESERVE_START-1)bit=1;
   else if(start==QC5_CAPTURE_RAM_END&&last==33554431u)bit=2;
   else return 0;
   if(banks&bit)return 0;banks|=bit;
  }
  p=end+1;
 }
 return banks==3;
}
#endif
