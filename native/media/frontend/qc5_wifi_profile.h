#ifndef QC5_WIFI_PROFILE_H
#define QC5_WIFI_PROFILE_H
#include <stdint.h>
#include <string.h>
struct qc5_wifi_profile {char ssid[33],password[64];unsigned open;char hostname[64];};
static inline unsigned qc5_wifi_length(const char*s,unsigned limit){unsigned n=0;while(n<limit&&s[n])n++;return n;}
/* A DHCP hostname is a single DNS label. Empty selects the MAC-based name.
 * The caller normalizes before storage; validation never changes input. */
static inline int qc5_hostname_normalize(char out[64],const char *value){
 unsigned n=qc5_wifi_length(value,64);
 if(n>63||(n&&(value[0]=='-'||value[n-1]=='-'))){out[0]=0;return 0;}
 for(unsigned i=0;i<n;i++){
  unsigned c=(unsigned char)value[i];if(c>='A'&&c<='Z')c+='a'-'A';
  if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-')){out[0]=0;return 0;}
  out[i]=(char)c;
 }
 for(unsigned i=n;i<64;i++)out[i]=0;return 1;
}
static inline int qc5_hostname_valid(const char *value){
 char normal[64];return qc5_hostname_normalize(normal,value)&&!strcmp(normal,value);
}
static inline int qc5_wifi_valid(const struct qc5_wifi_profile*p){
 unsigned a=qc5_wifi_length(p->ssid,sizeof(p->ssid)),b=qc5_wifi_length(p->password,sizeof(p->password));
 if(!qc5_hostname_valid(p->hostname)||!a||a>32||b>63||p->open>1||(!b&&!p->open)||(b&&b<8)||(b&&p->open))return 0;
 for(unsigned i=0;i<a;i++)if((unsigned char)p->ssid[i]<32||p->ssid[i]==127)return 0;
 for(unsigned i=0;i<b;i++)if((unsigned char)p->password[i]<32||p->password[i]==127)return 0;
 return 1;
}
static inline int qc5_wifi_equal(const struct qc5_wifi_profile*a,const struct qc5_wifi_profile*b){
 if(a->open!=b->open)return 0;
 for(unsigned i=0;i<sizeof(a->ssid);i++){if(a->ssid[i]!=b->ssid[i])return 0;if(!a->ssid[i])break;}
 for(unsigned i=0;i<sizeof(a->password);i++){if(a->password[i]!=b->password[i])return 0;if(!a->password[i])break;}
 return 1;
}
/* Wi-Fi matching and request tags intentionally ignore the independent name.
 * Flash deduplication must compare both credentials and stored hostname. */
static inline int qc5_wifi_profile_equal(const struct qc5_wifi_profile *a,const struct qc5_wifi_profile *b){
 return qc5_wifi_equal(a,b)&&!strcmp(a->hostname,b->hostname);
}
/* Change identifier only, never used for authentication or exposed by HTTP. */
static inline uint64_t qc5_wifi_tag(const struct qc5_wifi_profile*p){
 uint64_t h=UINT64_C(14695981039346656037);
 for(unsigned i=0;p->ssid[i];i++){h^=(unsigned char)p->ssid[i];h*=UINT64_C(1099511628211);}
 h^=0;h*=UINT64_C(1099511628211);
 for(unsigned i=0;p->password[i];i++){h^=(unsigned char)p->password[i];h*=UINT64_C(1099511628211);}
 h^=p->open;return h*UINT64_C(1099511628211);
}
#endif
