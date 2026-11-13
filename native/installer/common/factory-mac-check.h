/* SPDX-License-Identifier: (GPL-2.0-only OR GPL-3.0-only)
 * Read-only PCAM identity decoder and driver MAC policy. No unit constants,
 * serial/IP lookup, hardware accesses, allocation or output of PARAM data.
 */
#ifndef QC5_FACTORY_MAC_H
#define QC5_FACTORY_MAC_H
#define QCM_BYTES 0x40000u
#define QCM_BANK 0x20000u
#define QCM_APP_BYTES 18848u
#define QCM_MAC_OFFSET 0x9b0u
enum { QCM_OK=1,QCM_BAD_BANK=-1,QCM_BAD_LAYOUT=-2,QCM_CONFLICT=-3,QCM_BAD_MAC=-4,QCM_MISSING=-5 };
struct qcm_meta {unsigned bank_mask,copy_mask;};
static inline unsigned qcm_le(const unsigned char *p)
{return (unsigned)p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
static inline unsigned qcm_crc(const unsigned char *p,unsigned n)
{unsigned c=0;for(unsigned i=0;i<n;i++){c^=p[i];for(unsigned j=0;j<8;j++)c=(c>>1)^(0xedb88320u&-(c&1u));}return c;}
static inline int qcm_equal(const void *aa,const void *bb,unsigned n)
{const unsigned char *a=aa,*b=bb;for(unsigned i=0;i<n;i++)if(a[i]!=b[i])return 0;return 1;}
static inline int qcm_valid(const unsigned char mac[6])
{unsigned any=0;for(unsigned i=0;i<6;i++)any|=mac[i];return any&&!(mac[0]&1u);}
static inline int qcm_hex(unsigned char c)
{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
static inline int qcm_parse(const char text[18],unsigned char mac[6])
{
 if(text[17])return 0;
 unsigned char out[6];for(unsigned i=0;i<6;i++){
  int a=qcm_hex((unsigned char)text[3*i]),b=qcm_hex((unsigned char)text[3*i+1]);
  if(a<0||b<0||(i<5&&text[3*i+2]!=':'))return 0;out[i]=(unsigned char)(a*16+b);
 }
 if(!qcm_valid(out))return 0;for(unsigned i=0;i<6;i++)mac[i]=out[i];return 1;
}
static inline void qcm_format(const unsigned char mac[6],char text[18])
{for(unsigned i=0;i<6;i++){text[3*i]="0123456789abcdef"[mac[i]>>4];text[3*i+1]="0123456789abcdef"[mac[i]&15];if(i<5)text[3*i+2]=':';}text[17]=0;}
/* Factory PARAM wins over efuse; empty efuse never produces a random MAC. */
static inline int qcm_driver_select(const char factory[18],const unsigned char efuse[6],unsigned char out[6])
{
 unsigned char selected[6];int source;
 if(factory[0]){if(!qcm_parse(factory,selected))return 0;source=1;}
 else{if(!qcm_valid(efuse))return 0;for(unsigned i=0;i<6;i++)selected[i]=efuse[i];source=2;}
 for(unsigned i=0;i<6;i++)out[i]=selected[i];return source;
}
static inline int qcm_from_param(const unsigned char *p,unsigned bytes,unsigned char out[6],struct qcm_meta *meta)
{
 if(!p||bytes!=QCM_BYTES||!out||!meta)return QCM_BAD_LAYOUT;
 meta->bank_mask=meta->copy_mask=0;unsigned found=0;unsigned char selected[6]={0};
 for(unsigned bank=0;bank<2;bank++){
  unsigned base=bank*QCM_BANK;const unsigned char *head=p+base;
  if(!qcm_equal(head,"PCAM",4)||qcm_le(head+4)!=QCM_BANK||qcm_crc(head,0x8fc)!=qcm_le(head+0x8fc))continue;
  unsigned geometry=1;
  for(unsigned slot=0;slot<143;slot++){
   const unsigned char *d=head+12+16*slot;unsigned a=qcm_le(d),b=qcm_le(d+4),n=qcm_le(d+8),c=qcm_le(d+12);
   if((!(a|b|n|c))||((a&b&n&c)==0xffffffffu))continue;
   if(a<0x900||a>=QCM_BANK||b<QCM_BANK+0x900||b>=QCM_BYTES||!n||n>QCM_BANK-a||n>QCM_BYTES-b){geometry=0;break;}
  }
  if(!geometry)continue;meta->bank_mask|=1u<<bank;
  unsigned seen=0;
  for(unsigned slot=0;slot<143;slot++){
   const unsigned char *d=head+12+16*slot;unsigned a=qcm_le(d),b=qcm_le(d+4),n=qcm_le(d+8),crc=qcm_le(d+12);
   if(!n||n==0xffffffffu)continue;
   const unsigned char *record=p+(bank?b:a);unsigned key=0,which=0;
   if(n>=10&&qcm_equal(record,"app_param\0",10)){key=10;which=1;}
   else if(n>=15&&qcm_equal(record,"app_param_back\0",15)){key=15;which=2;}
   else continue;
   if(seen&which)return QCM_BAD_LAYOUT;seen|=which;
   if(n!=key+QCM_APP_BYTES)return QCM_BAD_LAYOUT;
   if(qcm_crc(record,n)!=crc)continue;
   const unsigned char *field=record+key+QCM_MAC_OFFSET;unsigned empty=1;
   for(unsigned i=0;i<18;i++)if(field[i])empty=0;
   if(empty)continue;
   unsigned char mac[6];if(!qcm_parse((const char *)field,mac))return QCM_BAD_MAC;
   if(found&&!qcm_equal(selected,mac,6))return QCM_CONFLICT;
   for(unsigned i=0;i<6;i++)selected[i]=mac[i];found=1;
   meta->copy_mask|=1u<<(bank*2+(which==2));
  }
 }
 if(!meta->bank_mask)return QCM_BAD_BANK;if(!found)return QCM_MISSING;
 for(unsigned i=0;i<6;i++)out[i]=selected[i];return QCM_OK;
}
#endif
