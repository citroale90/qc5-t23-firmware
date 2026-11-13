#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "owned-wifi-reset.h"
static unsigned commands,entries,fail_reply;
static int cmd(void*p,const char*s,char*out,size_t n){(void)p;assert(!strcmp(s,"REMOVE_NETWORK all")&&n>=5);commands++;if(fail_reply){memcpy(out,"FAIL\n",5);return 5;}entries=0;memcpy(out,"OK\n",3);return 3;}
static int role(void*p,enum qnw_role r){(void)p;(void)r;assert(0);return 0;}
int main(void){struct qnw_io io={0,cmd,role};struct qnw_mode m;
 assert(!qwr_reset_owned(NULL));assert(qnw_init(&m,&io,-1,"3662fa"));entries=2;assert(qwr_reset_owned(&m)&&!entries&&commands==1);
 for(unsigned i=0;i<1000;i++){entries=1;assert(qwr_reset_owned(&m)&&!entries);}unsigned old=commands;m.station_id=0;assert(!qwr_reset_owned(&m)&&commands==old);m.station_id=-1;m.ap_id=0;assert(!qwr_reset_owned(&m)&&commands==old);m.ap_id=-1;m.active=1;assert(!qwr_reset_owned(&m));m.active=0;m.desired=1;assert(!qwr_reset_owned(&m));m.desired=0;m.failed=1;assert(!qwr_reset_owned(&m));m.failed=0;fail_reply=1;assert(!qwr_reset_owned(&m));
 puts("owned_wifi_reset_passed cold_and_restart=1 stale_profile_growth_bounded=1 numeric_and_AP_owned_IDs_refused=1 command_failure_closed=1 no_flash_or_driver_IO=1");return 0;}
