/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef QC5_SPEAKER_ABI_H
#define QC5_SPEAKER_ABI_H
#include <linux/types.h>
#include <linux/ioctl.h>
enum {QSM_OFF=0,QSM_ON=1};
#define QSM_SET _IOW('Q',0x74,__u32)
#endif
