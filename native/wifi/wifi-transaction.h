/* SPDX-License-Identifier: GPL-3.0-only
 * Credentials become durable only after the exact candidate network obtains
 * a lease and remains stable. The old supplicant profile is retained until
 * the atomic storage callback succeeds; failed joins never change storage.
 */
#ifndef QC5_NATIVE_WIFI_TRANSACTION_H
#define QC5_NATIVE_WIFI_TRANSACTION_H
#include "wpa-mode.h"
#include "qc5_wifi_profile.h"
enum qnt_phase {QNT_IDLE,QNT_JOINING,QNT_SAVE_PENDING,QNT_SAVED,QNT_JOIN_FAILED,QNT_STORAGE_FAILED,QNT_CONTROL_FAILED};
struct qnt_transaction {
 struct qnw_mode *mode;struct qc5_wifi_profile profile;
 int old_id,pending_id,cleanup_id;
 unsigned phase,submitted;uint64_t retry_at,cleanup_at;
 void *opaque;int (*persist)(void *,const struct qc5_wifi_profile*);
};
static int qnt_set(struct qnw_mode*s,int id,const char*field,const char*value)
{
 char command[256];int n=snprintf(command,sizeof command,"SET_NETWORK %d %s %s",id,field,value);
 return n>0&&(size_t)n<sizeof command&&qnw_ok(s,command);
}
static int qnt_init(struct qnt_transaction*t,struct qnw_mode*mode,void*opaque,int(*persist)(void*,const struct qc5_wifi_profile*))
{
 if(!t||!mode||!persist)return 0;memset(t,0,sizeof *t);t->mode=mode;t->opaque=opaque;t->persist=persist;
 t->old_id=t->pending_id=t->cleanup_id=-1;return 1;
}
static int qnt_stage(struct qnt_transaction*t,const struct qc5_wifi_profile*p)
{
 if(!t||!p||!qc5_wifi_valid(p)||t->mode->failed||t->pending_id>=0||t->cleanup_id>=0)return 0;
 struct qnw_mode*s=t->mode;char reply[32];int n=s->io.command(s->io.opaque,"ADD_NETWORK",reply,sizeof reply),id;
 if(n<2||n>(int)sizeof reply||reply[n-1]!='\n'||!qnw_number(reply,(size_t)n-1,&id)||id==s->station_id||id==s->ap_id)return 0;
 char hex[65],psk[67];
 for(unsigned i=0;i<strlen(p->ssid);i++)snprintf(hex+i*2,3,"%02x",(unsigned char)p->ssid[i]);
 /* SET_NETWORK passes the value straight to wpa_config_parse_psk. Unlike
  * config-file lines, this path has no comment stripping. Its quoted PSK
  * parser uses the last quote and retains literal quote/backslash bytes. */
 int good=qnt_set(s,id,"ssid",hex)&&qnt_set(s,id,"mode","0")&&qnt_set(s,id,"scan_ssid","1")&&
  qnt_set(s,id,"key_mgmt",p->open?"NONE":"WPA-PSK");
 if(good&&!p->open){
  n=snprintf(psk,sizeof psk,"\"%s\"",p->password);
  good=n>0&&(size_t)n<sizeof psk&&qnt_set(s,id,"proto","RSN")&&qnt_set(s,id,"psk",psk);
 }
 memset(psk,0,sizeof psk);
 if(!good){if(!qnw_id_command(s,"REMOVE_NETWORK",id)){t->cleanup_id=id;t->cleanup_at=0;}return 0;}
 t->old_id=s->station_id;t->pending_id=id;t->profile=*p;t->phase=QNT_JOINING;t->submitted=1;
 s->station_id=id;return 1;
}
/* A validated profile read from durable storage is already saved. Keep it
 * through a failed boot association so idle AP recovery can retry it. */
static int qnt_load_saved(struct qnt_transaction*t,const struct qc5_wifi_profile*p)
{
 if(!t||!t->mode||t->mode->station_id>=0||t->mode->policy.phase!=QC5_NET_BOOT||!qnt_stage(t,p))return 0;
 t->old_id=t->pending_id=-1;t->phase=QNT_IDLE;t->submitted=0;memset(&t->profile,0,sizeof t->profile);return 1;
}
static int qnt_tick(struct qnt_transaction*t,uint64_t now,unsigned leased,unsigned portal_active)
{
 if(!t||!t->mode)return 0;
 unsigned events=0;
 if(!qnw_step(t->mode,now,leased,portal_active,t->submitted,&events)){
  t->phase=QNT_CONTROL_FAILED;return 0;
 }
 t->submitted=0;
 if(events&QC5_NET_DISCARD_STAGED){
  if(t->pending_id<0)return 0;
  int rejected=t->pending_id;t->mode->station_id=t->old_id;
  t->pending_id=t->old_id=-1;t->phase=QNT_JOIN_FAILED;memset(&t->profile,0,sizeof t->profile);
  if(!qnw_id_command(t->mode,"REMOVE_NETWORK",rejected)){t->cleanup_id=rejected;t->cleanup_at=now>UINT64_MAX-5000?UINT64_MAX:now+5000;}
 }
 if(events&QC5_NET_COMMIT_CREDENTIALS){if(t->pending_id<0)return 0;t->phase=QNT_SAVE_PENDING;t->retry_at=now;}
 if((t->phase==QNT_SAVE_PENDING||t->phase==QNT_STORAGE_FAILED)&&now>=t->retry_at){
  /* A storage error must not force a radio reset or claim a durable save. */
  if(t->persist(t->opaque,&t->profile)){
   t->phase=QNT_SAVED;t->cleanup_id=t->old_id;t->cleanup_at=now;t->old_id=t->pending_id=-1;
   memset(&t->profile,0,sizeof t->profile);
  }else{t->phase=QNT_STORAGE_FAILED;t->retry_at=now>UINT64_MAX-5000?UINT64_MAX:now+5000;}
 }
 if(t->cleanup_id>=0&&now>=t->cleanup_at&&t->cleanup_id!=t->mode->station_id&&t->cleanup_id!=t->mode->ap_id){
  if(qnw_id_command(t->mode,"REMOVE_NETWORK",t->cleanup_id))t->cleanup_id=-1;
  else t->cleanup_at=now>UINT64_MAX-5000?UINT64_MAX:now+5000;
 }
 return 1;
}
#endif
