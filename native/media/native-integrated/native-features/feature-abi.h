/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef QC5_FEATURE_ABI_H
#define QC5_FEATURE_ABI_H
#include <stdint.h>
#define QFS_MAGIC 0x37334651u
enum {QFS_STATUS=1,QFS_CONTINUOUS=2,QFS_MOTION=3,QFS_STOP=4,QFS_SENSITIVITY=5};
struct qfs_request {uint32_t magic,serial,operation,value;};
struct qfs_status {
 uint32_t magic,serial,ok,phase,continuous,motion,inhibit,moving,sensitivity,failed;
 uint64_t frames,samples,clips,errors;
};
_Static_assert(sizeof(struct qfs_request)==16,"Native local request size");
_Static_assert(sizeof(struct qfs_status)==72,"Native local status size");
#endif
