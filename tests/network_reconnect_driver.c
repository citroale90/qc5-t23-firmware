#include <assert.h>
#include <stdio.h>
#include "qc5_network_policy.h"
int main(void){
 struct qc5_net_policy s={.phase=QC5_NET_CONNECTED};
 struct qc5_net_observation o={.now_ms=1,.has_saved_credentials=1};
 unsigned a=qc5_net_step(&s,&o);assert(!(a&QC5_NET_TRY_STA));
#ifdef QC5_EXPECT_STUCK
 o.now_ms=60001;qc5_net_step(&s,&o);assert(s.phase==QC5_NET_SETUP_AP);
 o.now_ms=600001;a=qc5_net_step(&s,&o);assert(s.phase==QC5_NET_SETUP_AP&&!(a&QC5_NET_TRY_STA));
 puts("{\"original_stuck_hotspot_without_visibility_reproduced\":true}");
#else
 /* A boot with saved credentials and no radio link must open setup after
  * the bounded join attempt, without discarding the saved profile. */
 struct qc5_net_policy boot={0};
 struct qc5_net_observation first={.has_saved_credentials=1};
 a=qc5_net_step(&boot,&first);assert(boot.phase==QC5_NET_JOINING&&(a&QC5_NET_TRY_STA));
 first.now_ms=44999;assert(!(qc5_net_step(&boot,&first)&QC5_NET_START_AP));
 first.now_ms=45000;a=qc5_net_step(&boot,&first);
 assert(boot.phase==QC5_NET_SETUP_AP&&(a&QC5_NET_START_AP)&&!(a&QC5_NET_DISCARD_STAGED));
 assert(first.has_saved_credentials);
 o.now_ms=10001;a=qc5_net_step(&s,&o);assert(s.phase==QC5_NET_JOINING&&(a&QC5_NET_TRY_STA));
 o.now_ms=55001;a=qc5_net_step(&s,&o);assert(s.phase==QC5_NET_SETUP_AP&&(a&QC5_NET_START_AP));
 o.now_ms=115001;a=qc5_net_step(&s,&o);assert(s.phase==QC5_NET_JOINING&&(a&QC5_NET_TRY_STA)&&(a&QC5_NET_STOP_AP));
 o.now_ms=116001;o.station_online=1;qc5_net_step(&s,&o);
 o.now_ms=121001;a=qc5_net_step(&s,&o);assert(s.phase==QC5_NET_CONNECTED&&(a&QC5_NET_COMPLETE_LOCAL_SETUP)&&!(a&QC5_NET_COMMIT_CREDENTIALS));
 /* Do not reset a working association after a brief radio interruption. */
 o.station_online=0;o.now_ms=130001;assert(!(qc5_net_step(&s,&o)&QC5_NET_TRY_STA));
 o.station_online=1;o.now_ms=132001;assert(!(qc5_net_step(&s,&o)&QC5_NET_TRY_STA)&&s.phase==QC5_NET_CONNECTED);
 /* Active setup page defers a retry for the full quiet interval. */
 s=(struct qc5_net_policy){.phase=QC5_NET_SETUP_AP,.ap_up=1,.retry_after=60000};
 o=(struct qc5_net_observation){.has_saved_credentials=1,.ap_clients=1};
 for(unsigned n=1;n<=10;n++){o.now_ms=n*60000;assert(!(qc5_net_step(&s,&o)&QC5_NET_TRY_STA));}
 o.ap_clients=0;o.now_ms=659999;assert(!(qc5_net_step(&s,&o)&QC5_NET_TRY_STA));
 o.now_ms=660000;assert(qc5_net_step(&s,&o)&QC5_NET_TRY_STA);
 /* A known-negative scan differs from a native AP which cannot scan. */
 s=(struct qc5_net_policy){.phase=QC5_NET_SETUP_AP,.ap_up=1,.retry_after=1};
 o=(struct qc5_net_observation){.now_ms=1000000,.has_saved_credentials=1,.network_visibility_known=1};
 assert(!(qc5_net_step(&s,&o)&QC5_NET_TRY_STA));
 o.saved_network_visible=1;assert(qc5_net_step(&s,&o)&QC5_NET_TRY_STA);
 s=(struct qc5_net_policy){0};o=(struct qc5_net_observation){.now_ms=1};
 assert(qc5_net_step(&s,&o)&QC5_NET_START_AP);o.now_ms=1000000;assert(!(qc5_net_step(&s,&o)&QC5_NET_TRY_STA));
 puts("{\"missing_saved_wifi_at_boot_opens_hotspot_after45s\":true,\"saved_wifi_retried_after_link_loss\":true,\"idle_hotspot_retries_without_scan_support\":true,\"active_portal_preserved\":true,\"brief_radio_loss_does_not_reset_wifi\":true,\"unknown_and_absent_network_differ\":true,\"credentials_not_recommitted_on_reconnect\":true,\"no_reboot_or_cloud_probe\":true}");
#endif
 return 0;
}
