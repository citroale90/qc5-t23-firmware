
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "qc5_wifi_profile.h"
#define QNT_IDLE 0
#define QC5_NET_CONNECTED 2
struct mode {struct {unsigned phase;}policy;};
struct qnt_transaction{unsigned phase;struct mode *mode;};
struct qnr_runtime{unsigned migrate_pending;uint64_t migrate_since;struct qc5_wifi_profile boot_profile;};
static int saved,flash_present,sd_present,flash_reads,sd_reads,save_ok=1;
static int qnr_persist(void*r,const struct qc5_wifi_profile*p){(void)r;assert(qc5_wifi_valid(p));saved++;return save_ok;}
static int qwf_linux_load(const char*i,struct qc5_wifi_profile*p){(void)i;flash_reads++;if(flash_present)*p=(struct qc5_wifi_profile){"internal","synthetic-password",0};return flash_present;}
static int qnr_sd_profile(const char*i,struct qc5_wifi_profile*p){(void)i;sd_reads++;if(sd_present)*p=(struct qc5_wifi_profile){"SD","synthetic-password",0};return sd_present;}
static int qnr_saved_profile(const char *sd,const char *iface,struct qc5_wifi_profile *p)
{
 int rc=qwf_linux_load(iface,p);
 if(rc==1){puts("QC5_WIFI_PROFILE source=flash SD_required=0");return 1;}
 rc=qnr_sd_profile(sd,p);
 if(rc==1)puts("QC5_WIFI_PROFILE source=SD migration_to_flash_after_connection=1");
 return rc;
}
static void qnr_migration_tick(struct qnr_runtime *r,const struct qnt_transaction *t,uint64_t now,unsigned leased)
{
 if(!r->migrate_pending||t->phase!=QNT_IDLE)return;
 if(t->mode->policy.phase!=QC5_NET_CONNECTED||!leased){r->migrate_since=0;return;}
 if(!r->migrate_since)r->migrate_since=now;
 if(now>=r->migrate_since&&now-r->migrate_since>=5000){
  if(qnr_persist(r,&r->boot_profile)){r->migrate_pending=0;memset(&r->boot_profile,0,sizeof r->boot_profile);}
  else r->migrate_since=now;
 }
}

int main(void){struct qc5_wifi_profile p;flash_present=1;sd_present=1;assert(qnr_saved_profile("card","wlan0",&p)==1&&!strcmp(p.ssid,"internal")&&!sd_reads);flash_present=0;assert(qnr_saved_profile("card","wlan0",&p)==1&&!strcmp(p.ssid,"SD"));sd_present=0;assert(!qnr_saved_profile("card","wlan0",&p));
 struct mode m={.policy={1}};struct qnt_transaction t={.mode=&m};struct qnr_runtime r={.migrate_pending=1,.boot_profile={"test","synthetic-password",0}};
 qnr_migration_tick(&r,&t,1000,1);assert(!saved&&!r.migrate_since);m.policy.phase=2;qnr_migration_tick(&r,&t,2000,0);assert(!saved&&!r.migrate_since);qnr_migration_tick(&r,&t,3000,1);qnr_migration_tick(&r,&t,7999,1);assert(!saved);save_ok=0;qnr_migration_tick(&r,&t,8000,1);assert(saved==1&&r.migrate_pending);save_ok=1;qnr_migration_tick(&r,&t,13000,1);assert(saved==2&&!r.migrate_pending&&!r.boot_profile.ssid[0]);qnr_migration_tick(&r,&t,20000,1);assert(saved==2);
 puts("migration PASS: flash priority, SD bootstrap fallback, no-card mode, stable lease before save, failed-write retry, one-time migration");return 0;}
