/* SPDX-License-Identifier: GPL-3.0-only
 * One bounded validation of the persistent producer on the native adapter.
 * 632 captures /512 encoded frames; fixed60s watchdog and no flash writes.
 */
#define QC5_STREAM_RUNTIME
#define QC5_MDNS_CONTINUOUS
#define QC5_LIVE_FRAMES 512u
#define QC5_NV12_TARGET 632u
#define QC5_LIVE_RECORD_VERSION 19u
#include "capture-helix-sd-adapt.c"
