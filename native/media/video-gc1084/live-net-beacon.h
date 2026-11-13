/* SPDX-License-Identifier: GPL-2.0-or-later
 * Candidate-specific LAN discovery, no DNS and no external destination.
 */
#ifndef QC5_LIVE_NET_BEACON_H
#define QC5_LIVE_NET_BEACON_H
#include "live-net-boot.h"
#include "helix-live-net.h"
struct qc5_live_beacon {int fd;uint64_t due;unsigned char packet[84];};
static int qc5_beacon_open(struct qc5_live_beacon *b,const struct qc5_netboot *boot,const unsigned char nonce[16])
{
    if(!b)return 0;memset(b,0,sizeof(*b));b->fd=-1;
    if(!boot||!nonce||!qc5_netboot_valid(boot)||!boot->words[NB_IP])return 0;
    b->fd=socket(AF_INET,SOCK_DGRAM,0);if(b->fd<0)return 0;
    int one=1;struct sockaddr_in source={.sin_family=AF_INET,.sin_addr.s_addr=htonl(boot->words[NB_IP])};
    if(!qc5_net_flags(b->fd)||setsockopt(b->fd,SOL_SOCKET,SO_BROADCAST,&one,sizeof(one))||bind(b->fd,(struct sockaddr *)&source,sizeof(source))){close(b->fd);b->fd=-1;return 0;}
    memcpy(b->packet,"QL73",4);memcpy(b->packet+4,nonce,16);memcpy(b->packet+20,boot,64);return 1;
}
static void qc5_beacon_tick(struct qc5_live_beacon *b)
{
    if(!b||b->fd<0)return;uint64_t now=qc5_net_us();if(!now||now<b->due)return;
    b->due=now+250000;
    struct sockaddr_in destination={.sin_family=AF_INET,.sin_port=htons(18074),.sin_addr.s_addr=htonl(UINT32_C(0xc0a804ff))};
    (void)sendto(b->fd,b->packet,sizeof(b->packet),MSG_NOSIGNAL,(struct sockaddr *)&destination,sizeof(destination));
}
static void qc5_beacon_close(struct qc5_live_beacon *b)
{if(b&&b->fd>=0){close(b->fd);b->fd=-1;}}
#endif
