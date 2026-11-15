/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "network-files.h"
struct qnr_runtime {int directory;const char *sd;char iface[16],hostname[64],custom_hostname[64],default_hostname[64];unsigned migrate_pending;struct qc5_wifi_profile boot_profile;};
enum {QNW_STATION=1,QNW_AP=2};
struct qnt_transaction {int pending_id,cleanup_id;struct {unsigned active;} *mode;};
static struct qc5_wifi_profile flash;static int present=1,fail_save,fail_apply,fail_dhcp,saves,renewals;static char kernel_name[64];
static int qwf_linux_load(const char *iface,struct qc5_wifi_profile *p){(void)iface;memset(p,0,sizeof *p);if(present==1)*p=flash;return present;}
static int qwf_linux_store(const char *iface,const struct qc5_wifi_profile *p){(void)iface;saves++;if(fail_save)return -1;flash=*p;return 0;}
static int qnr_dhcp_start(struct qnr_runtime *r){(void)r;renewals++;return !fail_dhcp;}
static int test_hostname(const char *name,size_t n){if(fail_apply)return -1;if(fail_apply)return -1;assert(n<sizeof kernel_name);memcpy(kernel_name,name,n);kernel_name[n]=0;return 0;}
#define sethostname test_hostname
#include "hostname-owner.h"
#undef sethostname
static void state(int dir,const char *wanted){char b[128];assert(qnf_read(dir,"hostname-state",b,sizeof b)>0&&!strcmp(b,wanted));}
static void submit(int dir,const char *value){char b[96];snprintf(b,sizeof b,"HOSTNAME=\"%s\"\n",value);assert(qnf_text(dir,"hostname-request",b));}
int main(void){
 char folder[]="/tmp/qc5-hostname-owner-XXXXXX";assert(mkdtemp(folder));char sd[160];snprintf(sd,sizeof sd,"%s/sd",folder);assert(!mkdir(sd,0700));
 int dir=qnf_directory(folder);assert(dir>=0);struct qnr_runtime r={.directory=dir,.sd=sd,.iface="wlan0",.default_hostname="qc5-123456"};
 struct {unsigned active;} mode={QNW_STATION};struct qnt_transaction t={.pending_id=-1,.cleanup_id=-1,.mode=(void*)&mode};
 flash=(struct qc5_wifi_profile){"synthetic-wifi","synthetic-password",0};
 qhn_boot(&r,&flash);assert(!strcmp(kernel_name,"qc5-123456"));state(dir,"applied\n");
 submit(dir,"camera-sala");qhn_process(&r,&t);assert(saves==1&&renewals==1&&!strcmp(flash.hostname,"camera-sala")&&!strcmp(flash.password,"synthetic-password"));state(dir,"applied\n");
 struct qc5_wifi_profile saved=flash;memset(r.hostname,0,sizeof r.hostname);qhn_boot(&r,&saved);assert(!strcmp(kernel_name,"camera-sala"));
 fail_save=1;submit(dir,"camera-other");qhn_process(&r,&t);assert(!strcmp(kernel_name,"camera-sala")&&!strcmp(flash.hostname,"camera-sala"));state(dir,"save_failed\n");fail_save=0;
 present=0;submit(dir,"camera-queued");qhn_process(&r,&t);assert(!faccessat(dir,"hostname-request",F_OK,0));present=1;qhn_process(&r,&t);assert(!strcmp(flash.hostname,"camera-queued"));
 int old=saves;submit(dir,"-bad-name");qhn_process(&r,&t);assert(saves==old);state(dir,"invalid_request\n");
 submit(dir,"");qhn_process(&r,&t);assert(!flash.hostname[0]&&!strcmp(kernel_name,"qc5-123456"));
 assert(!setenv("QC5_TEST_SD_ROOT",sd,1));char config[192];snprintf(config,sizeof config,"%s/config.txt",sd);FILE *f=fopen(config,"w");assert(f);fputs("HOSTNAME=\"Camera-From-SD\"\n",f);assert(!fclose(f));saved=flash;qhn_boot(&r,&saved);assert(!strcmp(kernel_name,"qc5-123456")&&!saved.hostname[0]);qhn_boot(&r,NULL);assert(!strcmp(kernel_name,"camera-from-sd"));
 assert(!setenv("QC5_TEST_SD_READONLY","1",1));submit(dir,"camera-flash");qhn_process(&r,&t);assert(!strcmp(flash.hostname,"camera-flash"));state(dir,"applied\n");
 unsetenv("QC5_TEST_SD_READONLY");submit(dir,"camera-both");qhn_process(&r,&t);state(dir,"applied\n");char check[8193];int fd=open(config,O_RDONLY);long n=qc5_config_read_fd(fd,check,sizeof check);close(fd);struct qc5_wifi_profile p;assert(qc5_wifi_config_profile(check,n,&p)==1&&qc5_wifi_profile_equal(&p,&flash));
 fail_apply=1;submit(dir,"camera-apply");qhn_process(&r,&t);assert(!strcmp(flash.hostname,"camera-apply")&&!strcmp(r.hostname,"camera-both"));state(dir,"saved_apply_failed\n");fail_apply=0;
 fail_dhcp=1;submit(dir,"camera-dhcp");qhn_process(&r,&t);assert(!strcmp(flash.hostname,"camera-dhcp")&&!strcmp(r.hostname,"camera-dhcp"));state(dir,"saved_dhcp_retry\n");fail_dhcp=0;
 puts("hostname owner PASS: flash persistence and precedence, first-setup SD import/mirror, no-SD default, no password loss, failure reporting, queued first setup, DHCP renewal, automatic reset");
 for(const char **name=(const char*[]){"hostname-current","hostname-custom","hostname-state","hostname-request",NULL};*name;name++)unlinkat(dir,*name,0);close(dir);
 unlink(config);char prev[192];snprintf(prev,sizeof prev,"%s/.qc5-config.previous",sd);unlink(prev);snprintf(prev,sizeof prev,"%s/.qc5-config.lock",sd);unlink(prev);rmdir(sd);rmdir(folder);
}
