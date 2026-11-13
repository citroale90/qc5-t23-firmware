/* SPDX-License-Identifier: GPL-2.0-or-later
 * Diagnostic caps stay unchanged unless the continuous filter is selected.
 * Lifetime counters never wrap or reset coefficient/reference history.
 */
#ifndef QC5_MDNS_COUNTS_H
#define QC5_MDNS_COUNTS_H
#include <stdint.h>
#include <inttypes.h>
#include "mdns-offsets.h"
#ifdef QC5_MDNS_CONTINUOUS
typedef uint64_t qc5_mdns_count;
#define QC5_MDNS_COUNT_MAX UINT64_MAX
#define QC5_MDNS_COUNT_FMT PRIu64
#else
typedef uint32_t qc5_mdns_count;
#define QC5_MDNS_COUNT_MAX UINT32_MAX
#define QC5_MDNS_COUNT_FMT PRIu32
#endif
static int qc5_mdns_commit_available(qc5_mdns_count arms)
{
#ifndef QC5_MDNS_CONTINUOUS
 if(arms>=122)return 0;
#endif
 return arms<QC5_MDNS_COUNT_MAX;
}
static int qc5_mdns_update_available(qc5_mdns_count updates,qc5_mdns_count writes,unsigned changed)
{
#ifndef QC5_MDNS_CONTINUOUS
 if(updates>=121)return 0;
#endif
 return changed<=1&&updates<QC5_MDNS_COUNT_MAX&&(!changed||writes<=QC5_MDNS_COUNT_MAX-QC5_MDNS_PATCH);
}
static int qc5_mdns_next_frame(qc5_mdns_count previous,qc5_mdns_count completed)
{
#ifndef QC5_MDNS_CONTINUOUS
 if(completed>248)return 0;
#endif
 return completed&&previous<QC5_MDNS_COUNT_MAX&&completed==previous+1;
}
static int qc5_mdns_write_count_matches(qc5_mdns_count arms,qc5_mdns_count writes)
{
 return arms<=(QC5_MDNS_COUNT_MAX-QC5_MDNS_INITIAL)/QC5_MDNS_PATCH&&
  writes==QC5_MDNS_INITIAL+QC5_MDNS_PATCH*arms;
}
#endif
