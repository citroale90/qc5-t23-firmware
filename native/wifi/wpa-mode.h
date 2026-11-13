/* SPDX-License-Identifier: GPL-3.0-only
 * One supplicant, preserved station profile, bounded AP/STA transitions.
 * The runtime supplies a private control socket and address/DHCP services.
 */
#ifndef QC5_NATIVE_WPA_MODE_H
#define QC5_NATIVE_WPA_MODE_H
#include <stdio.h>
#include <string.h>
#include "qc5_network_policy.h"
enum qnw_role {QNW_NONE,QNW_STATION,QNW_AP};
struct qnw_io {
 void *opaque;
 int (*command)(void *,const char *,char *,size_t);
 int (*role)(void *,enum qnw_role);
};
struct qnw_mode {
 struct qnw_io io;struct qc5_net_policy policy;
 int station_id,ap_id;unsigned desired,active,failed;
 char ap_ssid[11];
};
static int qnw_number(const char *s,size_t n,int *out)
{
 unsigned v=0;if(!s||!n||n>4)return 0;
 for(size_t i=0;i<n;i++){if(s[i]<'0'||s[i]>'9')return 0;v=v*10+(unsigned)(s[i]-'0');}
 if(v>4095)return 0;*out=(int)v;return 1;
}
static int qnw_ok(struct qnw_mode *s,const char *command)
{char reply[32];int n=s->io.command(s->io.opaque,command,reply,sizeof reply);return n==3&&!memcmp(reply,"OK\n",3);}
static int qnw_id_command(struct qnw_mode *s,const char *verb,int id)
{char command[64];int n=snprintf(command,sizeof command,"%s %d",verb,id);return n>0&&(size_t)n<sizeof command&&qnw_ok(s,command);}
static int qnw_set(struct qnw_mode *s,const char *field,const char *value)
{char command[128];int n=snprintf(command,sizeof command,"SET_NETWORK %d %s %s",s->ap_id,field,value);return n>0&&(size_t)n<sizeof command&&qnw_ok(s,command);}
static int qnw_prepare_ap(struct qnw_mode *s)
{
 if(s->ap_id>=0)return 1;
 char reply[32];int n=s->io.command(s->io.opaque,"ADD_NETWORK",reply,sizeof reply),id;
 if(n<2||n>(int)sizeof reply||reply[n-1]!='\n'||!qnw_number(reply,(size_t)n-1,&id)||id==s->station_id)return 0;
 s->ap_id=id;char hex[21];
 for(unsigned i=0;i<10;i++)snprintf(hex+i*2,3,"%02x",(unsigned char)s->ap_ssid[i]);
 if(qnw_set(s,"ssid",hex)&&qnw_set(s,"mode","2")&&qnw_set(s,"key_mgmt","NONE")&&
    qnw_set(s,"frequency","2412")&&qnw_id_command(s,"DISABLE_NETWORK",id))return 1;
 /* Delete only the unselected AP entry created by this invocation. */
 (void)qnw_id_command(s,"REMOVE_NETWORK",id);s->ap_id=-1;return 0;
}
static int qnw_init(struct qnw_mode *s,const struct qnw_io *io,int station_id,const char *suffix)
{
 if(!s||!io||!io->command||!io->role||!suffix||strlen(suffix)!=6||station_id< -1||station_id>4095)return 0;
 for(unsigned i=0;i<6;i++)if(!((suffix[i]>='0'&&suffix[i]<='9')||(suffix[i]>='a'&&suffix[i]<='f')))return 0;
 memset(s,0,sizeof *s);s->io=*io;s->station_id=station_id;s->ap_id=-1;
 memcpy(s->ap_ssid,"QC5-",4);memcpy(s->ap_ssid+4,suffix,7);return 1;
}
/* STATUS may say COMPLETED in AP mode too. Match its network ID and mode;
 * do not mistake a self-created hotspot for a leased station connection. */
static int qnw_status(const char *text,size_t n,int station_id,int ap_id,unsigned *role)
{
 unsigned seen=0,complete=0,ap=0;int id=-1;size_t at=0;
 if(!text||!n||n>4096||text[n-1]!='\n'||memchr(text,0,n))return 0;
 while(at<n){
  const char *line=text+at,*end=memchr(line,'\n',n-at);if(!end)return 0;
  size_t len=(size_t)(end-line);unsigned bit=0;
  if(len>=10&&!memcmp(line,"wpa_state=",10)){bit=1;complete=len==19&&!memcmp(line+10,"COMPLETED",9);}
  else if(len>=3&&!memcmp(line,"id=",3)){bit=2;if(!qnw_number(line+3,len-3,&id))return 0;}
  else if(len>=5&&!memcmp(line,"mode=",5)){bit=4;ap=len==7&&!memcmp(line+5,"AP",2);}
  if(bit&&seen&bit)return 0;seen|=bit;at+=(len+1);
 }
 *role=QNW_NONE;
 if(complete&&(seen&2)){
  if(ap&&ap_id>=0&&id==ap_id)*role=QNW_AP;
  else if(!ap&&station_id>=0&&id==station_id)*role=QNW_STATION;
 }
 return 1;
}
static int qnw_step(struct qnw_mode *s,uint64_t now,unsigned leased,unsigned portal_active,unsigned submitted,unsigned *events)
{
 if(!s||s->failed)return 0;
 char status[4096];unsigned radio;
 int n=s->io.command(s->io.opaque,"STATUS",status,sizeof status);
 if(n<=0||n>(int)sizeof status||!qnw_status(status,(size_t)n,s->station_id,s->ap_id,&radio))goto failed;
 /* DHCP/DNS starts only after the supplicant confirms the AP network. */
 if(s->desired==QNW_AP&&radio==QNW_AP&&s->active!=QNW_AP){
  if(!s->io.role(s->io.opaque,QNW_AP))goto failed;s->active=QNW_AP;
 }
 struct qc5_net_observation o={.now_ms=now,.has_saved_credentials=s->station_id>=0,
  .station_online=radio==QNW_STATION&&leased,.ap_clients=portal_active,.credentials_submitted=submitted};
 struct qc5_net_policy next=s->policy;unsigned action=qc5_net_step(&next,&o);
 if(action&QC5_NET_STOP_AP){
  if(s->ap_id<0||!qnw_id_command(s,"DISABLE_NETWORK",s->ap_id)||!s->io.role(s->io.opaque,QNW_NONE))goto failed;
  s->active=s->desired=QNW_NONE;
 }
 if(action&QC5_NET_TRY_STA){
  if(s->station_id<0||!qnw_id_command(s,"SELECT_NETWORK",s->station_id)||!s->io.role(s->io.opaque,QNW_STATION))goto failed;
  s->desired=s->active=QNW_STATION;
 }
 if(action&QC5_NET_START_AP){
  if(!qnw_prepare_ap(s)||!s->io.role(s->io.opaque,QNW_NONE)||!qnw_id_command(s,"SELECT_NETWORK",s->ap_id))goto failed;
  s->active=QNW_NONE;s->desired=QNW_AP;
 }
 if(events)*events=action;
 s->policy=next;return 1;
failed:s->failed=1;return 0;
}
static int qnw_tick(struct qnw_mode *s,uint64_t now,unsigned leased,unsigned portal_active)
{
 unsigned events=0;
 return qnw_step(s,now,leased,portal_active,0,&events)&&
  !(events&(QC5_NET_COMMIT_CREDENTIALS|QC5_NET_DISCARD_STAGED));
}
#endif
