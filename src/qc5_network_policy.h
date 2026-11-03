#ifndef QC5_NETWORK_POLICY_H
#define QC5_NETWORK_POLICY_H
#include <stdint.h>
/* Pure policy: hardware adapter must supply STA association + valid DHCP state.
 * Internet/cloud reachability is deliberately not an input. */
enum qc5_net_phase {QC5_NET_BOOT,QC5_NET_JOINING,QC5_NET_CONNECTED,QC5_NET_SETUP_AP};
enum qc5_net_action {
 QC5_NET_START_AP=1,QC5_NET_STOP_AP=2,QC5_NET_TRY_STA=4,QC5_NET_ENABLE_QR=8,
 QC5_NET_COMMIT_CREDENTIALS=16,QC5_NET_COMPLETE_LOCAL_SETUP=32,QC5_NET_DISCARD_STAGED=64
};
struct qc5_net_policy {
 enum qc5_net_phase phase;
 uint64_t deadline,stable_since,lost_since,retry_after;
 unsigned ap_up,staged,checking_stable,checking_lost;
};
struct qc5_net_observation {
 uint64_t now_ms;
 unsigned has_saved_credentials,station_online,credentials_submitted,ap_clients;
 unsigned saved_network_visible,concurrent_ap_sta;
};
unsigned qc5_net_step(struct qc5_net_policy*,const struct qc5_net_observation*);
#endif
