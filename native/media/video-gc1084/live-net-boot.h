/* SPDX-License-Identifier: GPL-2.0-or-later
 * Compact boot evidence; never contains SSID, password, keys or packet data.
 */
#ifndef QC5_LIVE_NET_BOOT_H
#define QC5_LIVE_NET_BOOT_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include "memory-proof.h"
enum qc5_netboot_field {
    NB_MAGIC,NB_VERSION,NB_STAGES,NB_FAILED,NB_IP,NB_MAC0,NB_MAC1,NB_AVAILABLE_KB,
    NB_EVENTS,NB_ADDR_ERROR,NB_IFINDEX,NB_UNUSED0,NB_UNUSED1,NB_UNUSED2,NB_UNUSED3,NB_HASH
};
struct qc5_netboot {uint32_t words[16];};
#define QC5_NETBOOT_MAGIC UINT32_C(0x3733424e) /* NB37 */
static uint32_t qc5_netboot_hash(const void *data,unsigned int bytes)
{
    const unsigned char *p=data;uint32_t hash=2166136261u;
    for(unsigned int n=0;n<bytes;++n)hash=(hash^p[n])*16777619u;
    return hash;
}
static int qc5_netboot_valid(const struct qc5_netboot *p)
{
    if(!p)return 0;const uint32_t *m=p->words;
    struct qc5_memory_note memory={m[NB_UNUSED0],m[NB_UNUSED1],m[NB_UNUSED2],m[NB_UNUSED3]};
    return m[NB_MAGIC]==QC5_NETBOOT_MAGIC&&(m[NB_VERSION]==1||m[NB_VERSION]==2)&&!(m[NB_STAGES]&~4095u)&&
        m[NB_FAILED]<=15&&m[NB_EVENTS]<=32&&m[NB_MAC1]<=65535&&m[NB_AVAILABLE_KB]<=32768&&
        (m[NB_VERSION]==1?(!m[NB_UNUSED0]&&!m[NB_UNUSED1]&&!m[NB_UNUSED2]&&!m[NB_UNUSED3]):qc5_memory_note_valid(&memory))&&
        m[NB_HASH]==qc5_netboot_hash(p,60);
}
static int qc5_netboot_read(struct qc5_netboot *p,FILE *file)
{
    if(!p||!file)return 0;memset(p,0,sizeof(*p));uint32_t *m=p->words;
    m[NB_MAGIC]=QC5_NETBOOT_MAGIC;m[NB_VERSION]=1;
    char line[32];
    while(fgets(line,sizeof(line),file)){
        char *end;errno=0;long stage=strtol(line,&end,10);
        int malformed=errno||end==line;
        while(*end&&isspace((unsigned char)*end))++end;
        if(m[NB_EVENTS]>=32||malformed||*end||stage==0||stage<-12||stage>12){m[NB_FAILED]=15;break;}
        ++m[NB_EVENTS];
        if(stage>0)m[NB_STAGES]|=1u<<(stage-1);
        else if(!m[NB_FAILED])m[NB_FAILED]=(uint32_t)-stage;
    }
    if(ferror(file)||!m[NB_EVENTS])m[NB_FAILED]=15;
    m[NB_HASH]=qc5_netboot_hash(p,60);return qc5_netboot_valid(p);
}
#ifdef QC5_NETBOOT_PLATFORM
#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
static void qc5_netboot_sample(struct qc5_netboot *p)
{
    uint32_t *m=p->words;FILE *file=fopen("/run/live-net-stages","r");
    if(!file||!qc5_netboot_read(p,file)){
        memset(p,0,sizeof(*p));m[NB_MAGIC]=QC5_NETBOOT_MAGIC;m[NB_VERSION]=1;m[NB_FAILED]=15;
    }
    if(file)fclose(file);
    file=fopen("/proc/meminfo","r");char line[160];
    if(file){while(fgets(line,sizeof(line),file)){
        unsigned int kb;if(sscanf(line,"MemAvailable: %u kB",&kb)==1){m[NB_AVAILABLE_KB]=kb;break;}
    }fclose(file);}
    char name[IFNAMSIZ]={0};file=fopen("/run/live-net-iface","r");
    if(file){if(fscanf(file,"%15s",name)!=1)name[0]=0;fclose(file);}
    if(!strncmp(name,"wlan",4)&&strlen(name)<IFNAMSIZ){
        int fd=socket(AF_INET,SOCK_DGRAM,0);
        if(fd>=0){
            struct ifreq req={0};memcpy(req.ifr_name,name,strlen(name)+1);
            m[NB_IFINDEX]=if_nametoindex(name);
            if(!ioctl(fd,SIOCGIFADDR,&req))m[NB_IP]=ntohl(((struct sockaddr_in *)&req.ifr_addr)->sin_addr.s_addr);
            else m[NB_ADDR_ERROR]=(uint32_t)errno;
            if(!ioctl(fd,SIOCGIFHWADDR,&req)){
                const unsigned char *mac=(const unsigned char *)req.ifr_hwaddr.sa_data;
                m[NB_MAC0]=(uint32_t)mac[0]|(uint32_t)mac[1]<<8|(uint32_t)mac[2]<<16|(uint32_t)mac[3]<<24;
                m[NB_MAC1]=(uint32_t)mac[4]|(uint32_t)mac[5]<<8;
            }
            close(fd);
        }else m[NB_ADDR_ERROR]=(uint32_t)errno;
    }
    m[NB_HASH]=qc5_netboot_hash(p,60);
}
#endif
#endif
