/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef QC5_OWNED_WIFI_RESET_H
#define QC5_OWNED_WIFI_RESET_H
#include "wpa-mode.h"
/* Called only with the exclusive controller lock and a private supplicant.
 * Cold AUTO and controller recovery start with unknown RAM network IDs.
 * Numeric station-ID and explicit AP invocations retain their semantics. */
static int qwr_reset_owned(struct qnw_mode *m)
{
 if(!m||m->station_id!=-1||m->ap_id!=-1||m->active||m->desired||m->failed)return 0;
 return qnw_ok(m,"REMOVE_NETWORK all");
}
#endif
