/* SPDX-License-Identifier: GPL-3.0-only
 * First finite hardware validation of production image-control lifetimes.
 * The compatibility trial remains bounded; no watchdog refresh or flash.
 */
#define QC5_IMAGE_CONTINUOUS
#ifndef QC5_LIVE_RECORD_VERSION
#define QC5_LIVE_RECORD_VERSION 18u
#endif
#include "capture-helix-sd-bcsh-dynamic.c"
