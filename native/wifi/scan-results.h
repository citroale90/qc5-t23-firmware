/* SPDX-License-Identifier: GPL-3.0-only
 * Bounded parsing of wpa_supplicant SCAN_RESULTS. SSIDs stay byte-exact in
 * JSON hex, never interpolated into HTML or commands as text.
 */
#ifndef QC5_NATIVE_SCAN_RESULTS_H
#define QC5_NATIVE_SCAN_RESULTS_H
#include <stdio.h>
#include <string.h>
static int qns_hex(unsigned char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
static int qns_ssid(const char *s,size_t n,char hex[65])
{
 size_t used=0;for(size_t i=0;i<n;i++){
  unsigned c=(unsigned char)s[i];
  if(c=='\\'){
   if(++i==n)return 0;c=(unsigned char)s[i];
   if(c=='x'){if(i+2>=n)return 0;int a=qns_hex((unsigned char)s[i+1]),b=qns_hex((unsigned char)s[i+2]);if(a<0||b<0)return 0;c=(unsigned)(a*16+b);i+=2;}
   else if(c=='e')c=27;else if(c=='n')c=10;else if(c=='r')c=13;else if(c=='t')c=9;else if(c!='\\'&&c!='"')return 0;
  }
  if(c<32||c==127||used>=32)return 0;
  snprintf(hex+used*2,3,"%02x",c);used++;
 }
 return used!=0;
}
static int qns_signal(const char *s,size_t n,int *out)
{
 size_t i=0;int negative=n&&s[0]=='-';if(negative)i++;
 if(i==n||n>4)return 0;int value=0;for(;i<n;i++){if(s[i]<'0'||s[i]>'9')return 0;value=value*10+s[i]-'0';}
 if(negative)value=-value;if(value>0||value< -150)return 0;
 value=2*(value+100);*out=value<0?0:value>100?100:value;return 1;
}
static int qns_json(const char *text,size_t size,char *json,size_t cap,unsigned *count)
{
 if(!text||!size||size>12288||memchr(text,0,size)||text[size-1]!='\n'||cap<40)return 0;
 const char *header=memchr(text,'\n',size);if(!header)return 0;
 static const char expected[]="bssid / frequency / signal level / flags / ssid";
 if((size_t)(header-text)!=sizeof expected-1||memcmp(text,expected,sizeof expected-1))return 0;
 size_t used=(size_t)snprintf(json,cap,"{\"cached\":true,\"networks\":[");unsigned found=0;
 for(size_t at=(size_t)(header-text)+1;at<size;){
  const char *line=text+at,*end=memchr(line,'\n',size-at);if(!end)return 0;at=(size_t)(end-text)+1;
  const char *tabs[4],*p=line;unsigned nt=0;
  while(nt<4&&(p=memchr(p,'\t',(size_t)(end-p))))tabs[nt++]=p++;
  if(nt!=4||tabs[0]-line!=17)continue;
  int valid=1;for(unsigned i=0;i<17;i++)if(i%3==2?line[i]!=':':qns_hex((unsigned char)line[i])<0)valid=0;
  for(p=tabs[0]+1;p<tabs[1];p++)if(*p<'0'||*p>'9')valid=0;
  int strength;char hex[65];if(!valid||!qns_signal(tabs[1]+1,(size_t)(tabs[2]-tabs[1]-1),&strength)||!qns_ssid(tabs[3]+1,(size_t)(end-tabs[3]-1),hex))continue;
  size_t flags=(size_t)(tabs[3]-tabs[2]-1);int secured=0;
  if(flags>256)continue;for(p=tabs[2]+1;p<tabs[3];p++)if(*p!='['&&*p!=']'&&(*p<'A'||*p>'Z')&&(*p<'0'||*p>'9')&&*p!='-'&&*p!='+'&&*p!='_')valid=0;
  if(!valid)continue;
  for(p=tabs[2]+1;p+3<=tabs[3];p++)if(!memcmp(p,"WPA",3)||!memcmp(p,"RSN",3)||!memcmp(p,"WEP",3)||!memcmp(p,"SAE",3))secured=1;
  char item[160];int bytes=snprintf(item,sizeof item,"%s{\"ssid_hex\":\"%s\",\"signal\":%d,\"secured\":%s}",found?",":"",hex,strength,secured?"true":"false");
  if(bytes<=0||(size_t)bytes>=sizeof item||used+(size_t)bytes+3>cap)break;
  memcpy(json+used,item,(size_t)bytes);used+=(size_t)bytes;found++;
 }
 memcpy(json+used,"]}",3);if(count)*count=found;return (int)(used+2);
}
#endif
