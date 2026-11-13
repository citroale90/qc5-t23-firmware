/* SPDX-License-Identifier: GPL-3.0-only
 * CT and exposure are independent event sources. Every accepted change
 * reaches both colour blocks; a partial failure makes the pair terminal.
 */
#ifndef QC5_IMAGE_COLOUR_EVENTS_H
#define QC5_IMAGE_COLOUR_EVENTS_H
#include "ccm-capture.h"
#include "bcsh-live-plan.h"
static int qc5_image_colour_event(struct qc5_ccm_capture *matrix,struct qbh_state *colour,uint32_t ct,uint32_t ev)
{
 if(!matrix||!colour||!matrix->ready||matrix->failed||!colour->ready||colour->failed||
    colour->finished||colour->events.failed)return 0;
 if(!qc5_ccm_tick(matrix,ct,ev)){colour->failed=1;return 0;}
 if(!qbh_tick(colour,ct,ev)){matrix->failed=1;return 0;}
 return 1;
}
#endif
