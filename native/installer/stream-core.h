/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef QC5_STREAM_CORE_H
#define QC5_STREAM_CORE_H
#include "full-recovery.h"
struct qsr_source {void *context;int (*read)(void *,u32,u32,u8 *);};
/* Two caller-owned 32 KiB buffers. Source packages are never loaded whole. */
int qsr_verify(const struct qsr_source *,const struct qsr_source *,const struct qfr_io *,u8 *,u8 *);
int qsr_install(const struct qsr_source *,const struct qsr_source *,const struct qfr_io *,u8 *,u8 *);
int qsr_restore(const struct qsr_source *,const struct qfr_io *,u8 *,u8 *);
int qsr_native_ready(const struct qfr_io *,u8 *);
#endif
