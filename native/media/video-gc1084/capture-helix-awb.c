/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE 1
#define QC5_NV12_SPATIAL
#define QC5_LIVE_BOOTLOG
#define QC5_NETBOOT_PLATFORM
#define QC5_LIVE_WB_NOTE
#define QC5_SENSOR_ORIENTATION 3u
#define QC5_ORIENTATION_SETTLE_US 250000u
#define QC5_AE_TARGET 128u
#define QC5_DYNAMIC_AWB
#ifndef QC5_LIVE_RECORD_VERSION
#define QC5_LIVE_RECORD_VERSION 10u
#endif
#ifdef QC5_NV12_DETAIL
#ifdef QC5_AF_STATS
#define QC5_EXPECT_SPATIAL_HASH (qc5_spatial_pair_digest(qc5_spatial_pair_digest(gc1084_spatial.checksum,gc1084_detail.checksum),qc5_af_profile_hash()))
#else
#define QC5_EXPECT_SPATIAL_HASH (qc5_spatial_pair_digest(gc1084_spatial.checksum,gc1084_detail.checksum))
#endif
#else
#define QC5_EXPECT_SPATIAL_HASH (gc1084_spatial.checksum)
#endif
#define QC5_EXPECT_WB_HASH (qc5_awb_profiles[0].checksum)
#include "capture-helix-live.c"
