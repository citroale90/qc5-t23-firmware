/* SPDX-License-Identifier: GPL-2.0-or-later
 * One encoder owner, one pending picture. Publish references only on success.
 */
#ifndef QC5_HELIX_SEQUENCE_H
#define QC5_HELIX_SEQUENCE_H
#include <string.h>
#include "helix-plan.h"
struct qc5_helix_sequence {
    uint32_t gop,qp,have_reference,reference_index,next_frame,since_idr,next_idr,pending;
    struct qc5_helix_picture issued;
};
static int qc5_helix_sequence_next(struct qc5_helix_sequence *s,unsigned int bank,struct qc5_helix_picture *p)
{
    if(!s||!p||!s->gop||s->gop>1024||s->qp>51||s->pending||bank>1||s->have_reference>1||
        s->reference_index>1||s->next_frame>1024||s->next_idr>1||s->since_idr>1024)return 0;
    unsigned int idr=!s->have_reference||s->since_idr>=s->gop||s->next_frame>=1024;
    *p=(struct qc5_helix_picture){idr,idr?0:s->next_frame,s->next_idr,s->qp,bank,
        s->have_reference?(s->reference_index^1u):0};
    s->issued=*p;s->pending=1;return 1;
}
static int qc5_helix_sequence_finish(struct qc5_helix_sequence *s,const struct qc5_helix_picture *p,int valid)
{
    if(!s||!p||s->pending!=1||(valid!=0&&valid!=1)||memcmp(p,&s->issued,sizeof(*p)))return 0;
    s->pending=0;
    if(!valid){s->have_reference=0;s->reference_index=0;s->next_frame=0;s->since_idr=0;return 1;}
    s->have_reference=1;s->reference_index=p->output_reference;s->next_frame=p->frame_number+1;
    if(p->idr){s->since_idr=1;s->next_idr=p->idr_id^1u;}else ++s->since_idr;
    return 1;
}
#endif
