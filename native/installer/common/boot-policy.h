/* SPDX-License-Identifier: GPL-3.0-only
 * Prepare a per-camera ENV proposal in RAM/SD, never write a device here.
 */
#ifndef QC5_BOOT_POLICY_H
#define QC5_BOOT_POLICY_H
#include "boot-policy.generated.h"
static uint32_t qenv_crc(const u8 *p,size_t n){uint32_t c=~0u;while(n--){c^=*p++;for(unsigned i=0;i<8;i++)c=(c>>1)^(0xedb88320u&-(c&1));}return ~c;}
static int qenv_key(const char *key,const u8 *pair,size_t n){return strlen(key)==n&&!memcmp(pair,key,n);}
static int qenv_replaced(const u8 *name,size_t n)
{
 for(const struct qenv_pair *p=qenv_policy;p->name;p++)if(qenv_key(p->name,name,n))return 1;
 for(const char *const *p=qenv_legacy;*p;p++)if(qenv_key(*p,name,n))return 1;
 return 0;
}
static int qenv_add(u8 *out,size_t *at,const u8 *value,size_t n)
{if(n>=4092-*at)return 0;memcpy(out+4+*at,value,n);*at+=n;out[4+(*at)++]=0;return 1;}
static int qenv_pair_add(u8 *out,size_t *at,const struct qenv_pair *p)
{
 char text[1024];int n=snprintf(text,sizeof text,"%s=%s",p->name,p->value);
 return n>0&&n<(int)sizeof text&&qenv_add(out,at,(const u8 *)text,(size_t)n);
}
static const u8 *qenv_find(const u8 *record,const char *name)
{
 size_t kn=strlen(name);for(size_t pos=4;pos<16383&&record[pos];){const u8 *end=memchr(record+pos,0,16384-pos);if(!end)return 0;size_t n=end-(record+pos);if(n>kn&&record[pos+kn]=='='&&!memcmp(record+pos,name,kn))return record+pos+kn+1;pos+=n+1;}return 0;
}
static int qenv_propose(const u8 old[16384],u8 out[16384])
{
 int blank=1;for(size_t i=0;i<16384;i++)if(old[i]!=255){blank=0;break;}
 if(!blank&&qenv_crc(old+4,16380)!=((uint32_t)old[0]|(uint32_t)old[1]<<8|(uint32_t)old[2]<<16|(uint32_t)old[3]<<24))return 0;
 memcpy(out,old,16384);memset(out,0,4096);size_t at=0;
 if(blank){
  for(const struct qenv_pair *p=qenv_factory;p->name;p++)if(!qenv_replaced((const u8 *)p->name,strlen(p->name))&&!qenv_pair_add(out,&at,p))return 0;
 }else{
  size_t pos=4;int terminated=0;
  while(pos<16383){
   if(!old[pos]){terminated=1;break;}
   const u8 *end=memchr(old+pos,0,16384-pos);if(!end)return 0;size_t n=end-(old+pos);
   const u8 *eq=memchr(old+pos,'=',n);if(!eq||eq==old+pos)return 0;size_t kn=eq-(old+pos);
   for(size_t j=0;j<n;j++)if(old[pos+j]<32||old[pos+j]>126)return 0;
   for(size_t prev=4;prev<pos;){const u8 *pe=memchr(old+prev,0,pos-prev);if(!pe)return 0;const u8 *pk=memchr(old+prev,'=',pe-(old+prev));if(pk&&(size_t)(pk-(old+prev))==kn&&!memcmp(old+prev,old+pos,kn))return 0;prev=(size_t)(pe-old)+1;}
   if(!qenv_replaced(old+pos,kn)&&!qenv_add(out,&at,old+pos,n))return 0;pos+=n+1;
  }
  if(!terminated)return 0;
 }
 for(const struct qenv_pair *p=qenv_policy;p->name;p++)if(!qenv_pair_add(out,&at,p))return 0;
 if(at>=4091)return 0;out[4+at]=0;
 if(!blank){int compatible=1;for(const struct qenv_pair *p=qenv_policy;p->name;p++){const u8 *v=qenv_find(old,p->name);if(!v||strcmp((const char *)v,p->value))compatible=0;}for(const char *const *p=qenv_legacy;*p;p++)if(qenv_find(old,*p))compatible=0;if(compatible){memcpy(out,old,16384);return 1;}}
 uint32_t crc=qenv_crc(out+4,16380);out[0]=crc;out[1]=crc>>8;out[2]=crc>>16;out[3]=crc>>24;
 return 1;
}
#endif
