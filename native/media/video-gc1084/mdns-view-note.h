/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QC5_MDNS_VIEW_NOTE_H
#define QC5_MDNS_VIEW_NOTE_H
#include <stdio.h>
#include <stdint.h>
#define QC5_MDNS_VIEW_LINE 384u
static unsigned qc5_mdns_view_line(char out[QC5_MDNS_VIEW_LINE],unsigned first,const uint32_t *values,unsigned count)
{
 if(!out||!values||first>=164||first%32||!count||count>32||count>164-first||count!=(164-first<32?164-first:32))return 0;
 int at=snprintf(out,QC5_MDNS_VIEW_LINE,"<6>QC5_MDNS_VIEW first=%u count=%u values=",first,count);
 if(at<=0||(unsigned)at>=QC5_MDNS_VIEW_LINE)return 0;
 for(unsigned n=0;n<count;n++){
  int k=snprintf(out+at,QC5_MDNS_VIEW_LINE-(unsigned)at,"%08x",values[n]);
  if(k!=8||(unsigned)at+8>=QC5_MDNS_VIEW_LINE)return 0;at+=8;
 }
 out[at++]='\n';out[at]=0;return (unsigned)at;
}
#endif
