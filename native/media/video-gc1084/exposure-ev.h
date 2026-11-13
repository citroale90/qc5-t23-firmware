/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef QC5_EXPOSURE_EV_H
#define QC5_EXPOSURE_EV_H
#include <stdint.h>
#include "gc1084-awb-ev.private.h"
static uint32_t qc5_exposure_ev_q10(unsigned lines,unsigned gain)
{
    if(lines<4||lines>896||gain>24)return 0;
    return (uint32_t)(((uint64_t)lines*qc5_awb_gain_linear[gain])>>6);
}
#endif
