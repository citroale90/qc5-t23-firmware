/* SPDX-License-Identifier: GPL-2.0-only
 * Date the first sample from the validated DMA cursor/backlog, not the
 * time a queued period happened to be copied. No new buffer or clock epoch.
 */
#ifndef QC5_SAMPLE_CLOCK_H
#define QC5_SAMPLE_CLOCK_H
#include <stdint.h>
static int qas_sample_stamp(uint64_t completed,uint64_t serial,unsigned phase_bytes,uint64_t observed,uint64_t *out)
{
 if(!serial||!observed||!out||completed<serial||completed-serial>=30||phase_bytes>=2000||phase_bytes%16)return 0;
 uint64_t age=((completed-serial+1)*2000+phase_bytes)*1000000u/32000u;
 if(age>=observed)return 0;
 *out=observed-age;return 1;
}
#endif
