#include "qc5_network_policy.h"
#define JOIN_TIMEOUT_MS 45000
#define LOST_TIMEOUT_MS 10000
#define CONNECT_STABLE_MS 5000
#define AP_RETRY_MS 60000
static unsigned enter_ap(struct qc5_net_policy*s,uint64_t now){
 unsigned actions=s->ap_up?0:QC5_NET_START_AP;
 if(s->staged)actions|=QC5_NET_DISCARD_STAGED;
 s->phase=QC5_NET_SETUP_AP;s->ap_up=1;s->staged=0;
 s->checking_stable=s->checking_lost=0;s->retry_after=now+AP_RETRY_MS;
 return actions;
}
static unsigned begin_join(struct qc5_net_policy*s,const struct qc5_net_observation*o){
 unsigned actions=QC5_NET_TRY_STA;
 if(s->ap_up&&!o->concurrent_ap_sta){actions|=QC5_NET_STOP_AP;s->ap_up=0;}
 s->phase=QC5_NET_JOINING;s->deadline=o->now_ms+JOIN_TIMEOUT_MS;
 s->checking_stable=s->checking_lost=0;
 return actions;
}
unsigned qc5_net_step(struct qc5_net_policy*s,const struct qc5_net_observation*o){
 unsigned actions=QC5_NET_ENABLE_QR;
 if(o->credentials_submitted){
  s->staged=1;return actions|begin_join(s,o);
 }
 if(s->phase==QC5_NET_BOOT){
  if(!o->has_saved_credentials&&!o->station_online)return actions|enter_ap(s,o->now_ms);
  if(o->station_online){
   /* Observe the working link; do not restart association at every boot. */
   s->phase=QC5_NET_JOINING;s->deadline=o->now_ms+JOIN_TIMEOUT_MS;
   s->checking_stable=1;s->stable_since=o->now_ms;
  }else actions|=begin_join(s,o);
 }
 if(s->phase==QC5_NET_JOINING){
  if(o->station_online){
   if(!s->checking_stable){s->checking_stable=1;s->stable_since=o->now_ms;}
   if(o->now_ms-s->stable_since>=CONNECT_STABLE_MS){
    s->phase=QC5_NET_CONNECTED;s->checking_lost=0;
    if(s->ap_up){actions|=QC5_NET_STOP_AP;s->ap_up=0;}
    if(s->staged){actions|=QC5_NET_COMMIT_CREDENTIALS;s->staged=0;}
    return actions|QC5_NET_COMPLETE_LOCAL_SETUP;
   }
  }else{
   s->checking_stable=0;
   if(o->now_ms>=s->deadline)return actions|enter_ap(s,o->now_ms);
  }
 }else if(s->phase==QC5_NET_CONNECTED){
  if(o->station_online)s->checking_lost=0;
  else{
   if(!s->checking_lost){s->checking_lost=1;s->lost_since=o->now_ms;}
   if(o->now_ms-s->lost_since>=LOST_TIMEOUT_MS)return actions|begin_join(s,o);
  }
 }else if(s->phase==QC5_NET_SETUP_AP){
  if(o->station_online){
   s->phase=QC5_NET_JOINING;s->checking_stable=1;s->stable_since=o->now_ms;
   s->deadline=o->now_ms+JOIN_TIMEOUT_MS;
  }else if(o->ap_clients){
   /* Preserve a person configuring the hotspot; give them a full retry
    * interval after the last observed portal activity. */
   s->retry_after=o->now_ms+AP_RETRY_MS;
  }else if(o->has_saved_credentials&&(o->saved_network_visible||!o->network_visibility_known)&&o->now_ms>=s->retry_after){
   return actions|begin_join(s,o);
  }
 }
 return actions;
}
