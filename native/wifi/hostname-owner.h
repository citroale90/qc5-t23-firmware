/* SPDX-License-Identifier: GPL-3.0-only
 * The network owner alone persists a name or renews its DHCP child.
 * HTTP never reads the Wi-Fi record. Names are literal single DNS labels. */
#ifndef QC5_HOSTNAME_OWNER_H
#define QC5_HOSTNAME_OWNER_H
static int qhn_sd(const char *sd,char name[64])
{
 if(!qc5_sd_mounted(sd,0))return 0;
 int dir=open(sd,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(dir<0)return -1;
 int fd=openat(dir,"config.txt",O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC);close(dir);
 if(fd<0)return errno==ENOENT?0:-1;
 struct stat st;char text[8193];long n=-1;
 if(!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size>=0&&st.st_size<=8192)n=qc5_config_read_fd(fd,text,sizeof text);
 close(fd);int rc=n>=0?qc5_wifi_config_hostname(text,(unsigned long)n,name):-1;memset(text,0,sizeof text);return rc;
}
static int qhn_publish(struct qnr_runtime *r,const char *state)
{
 char current[66],custom[66];snprintf(current,sizeof current,"%s\n",r->hostname);snprintf(custom,sizeof custom,"%s\n",r->custom_hostname);
 return qnf_text(r->directory,"hostname-current",current)&&qnf_text(r->directory,"hostname-custom",custom)&&qnf_text(r->directory,"hostname-state",state);
}
static void qhn_boot(struct qnr_runtime *r,struct qc5_wifi_profile *saved)
{
 char custom[64]={0},from_sd[64];if(saved)memcpy(custom,saved->hostname,sizeof custom);
 /* The selected saved profile already came from flash, or from SD only
  * when no flash profile exists. Even an empty flash name is authoritative:
  * it may be the user's explicit reset to the automatic MAC-based name. */
 int rc=saved?0:qhn_sd(r->sd,from_sd);if(rc==1)memcpy(custom,from_sd,sizeof custom);
 memcpy(r->custom_hostname,custom,sizeof custom);strcpy(r->hostname,custom[0]?custom:r->default_hostname);
 if(saved)memcpy(saved->hostname,custom,sizeof custom);
 int ok=!sethostname(r->hostname,strlen(r->hostname));
 (void)qhn_publish(r,!ok?"apply_failed\n":rc<0?"invalid_SD_name\n":"applied\n");
}
static void qhn_process(struct qnr_runtime *r,const struct qnt_transaction *t)
{
 if(t->pending_id>=0||t->cleanup_id>=0||qnf_busy(r->directory))return;
 struct stat st;if(fstatat(r->directory,"hostname-request",&st,AT_SYMLINK_NOFOLLOW))return;
 char text[96],name[64],canonical[96];int n=qnf_read(r->directory,"hostname-request",text,sizeof text);
 int parsed=n>0?qc5_wifi_config_hostname(text,(unsigned long)n,name):-1;
 int expected=parsed==1?snprintf(canonical,sizeof canonical,"HOSTNAME=\"%s\"\n",name):-1;
 if(parsed!=1||expected!=n||memcmp(canonical,text,(size_t)n)){
  if(S_ISREG(st.st_mode))unlinkat(r->directory,"hostname-request",0);
  (void)qnf_text(r->directory,"hostname-state","invalid_request\n");return;
 }
 struct qc5_wifi_profile p,check;int found=qwf_linux_load(r->iface,&p);
 if(!found){memset(&p,0,sizeof p);return;} /* Keep queued until Wi-Fi is saved. */
 (void)qnf_text(r->directory,"hostname-state","applying\n");
 if(unlinkat(r->directory,"hostname-request",0)){memset(&p,0,sizeof p);return;}
 int ok=found==1;
 if(ok){memcpy(p.hostname,name,sizeof p.hostname);ok=!qwf_linux_store(r->iface,&p)&&qwf_linux_load(r->iface,&check)==1&&qc5_wifi_profile_equal(&p,&check);}
 memset(&check,0,sizeof check);
 if(ok){
  memcpy(r->custom_hostname,name,sizeof name);
  const char *applied=name[0]?name:r->default_hostname;
  ok=!sethostname(applied,strlen(applied));if(ok)strcpy(r->hostname,applied);
  r->migrate_pending=0;memset(&r->boot_profile,0,sizeof r->boot_profile);
  /* Flash is authoritative; absent/readonly/full SD never undoes a save. */
  if(qc5_sd_mounted(r->sd,1)&&qc5_wifi_store(r->sd,NULL,0,&p))
   fputs("QC5_HOSTNAME optional_SD_mirror_failed=1 flash_verified=1\n",stderr);
  if(!ok){memset(&p,0,sizeof p);(void)qhn_publish(r,"saved_apply_failed\n");return;}
  if(t->mode->active==QNW_STATION&&!qnr_dhcp_start(r)){
   memset(&p,0,sizeof p);(void)qhn_publish(r,"saved_dhcp_retry\n");return;
  }
 }
 memset(&p,0,sizeof p);(void)qhn_publish(r,ok?"applied\n":"save_failed\n");
}
#endif
