/* SPDX-License-Identifier: GPL-3.0-only */
#define QC5_MEMORY_PROOF
#ifndef QC5_LIVE_RECORD_VERSION
#define QC5_LIVE_RECORD_VERSION 13u
#endif
#define QC5_DYNAMIC_CCM
#define QC5_EXPECT_CCM_HASH (qc5_ccm_profiles[0].checksum)
#include "capture-helix-awb.c"
