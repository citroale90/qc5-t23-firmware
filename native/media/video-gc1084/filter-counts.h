/* SPDX-License-Identifier: GPL-2.0-or-later
 * Filter history is not restarted when diagnostic counters reach their old
 * limits. Legacy finite builds retain their original32-bit representation.
 */
#ifndef QC5_FILTER_COUNTS_H
#define QC5_FILTER_COUNTS_H
#include <stdint.h>
#include <inttypes.h>
#ifdef QC5_IMAGE_CONTINUOUS
typedef uint64_t qc5_filter_count;
#define QC5_FILTER_COUNT_MAX UINT64_MAX
#define QC5_FILTER_COUNT_FMT PRIu64
#else
typedef uint32_t qc5_filter_count;
#define QC5_FILTER_COUNT_MAX UINT32_MAX
#define QC5_FILTER_COUNT_FMT PRIu32
#endif
static int qc5_filter_room(qc5_filter_count n,unsigned add)
{return add<=QC5_FILTER_COUNT_MAX-n;}
/* Legacy SD proof records have32-bit fields; refuse a lossy projection.
 * A permanent service exports the full64-bit counters through its own API. */
static int qc5_filter_u32_pair(qc5_filter_count a,qc5_filter_count b,uint32_t *x,uint32_t *y)
{if(!x||!y||a>UINT32_MAX||b>UINT32_MAX)return 0;*x=(uint32_t)a;*y=(uint32_t)b;return 1;}
#endif
