/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef QC5_PERIPHERAL_ABI_H
#define QC5_PERIPHERAL_ABI_H
#include <linux/types.h>
#include <linux/ioctl.h>
#define QCM_PROGRESS_VERSION 3u
enum {QCM_INITIALIZE=1,QCM_JOG=2,QCM_STOP=3,QCM_FILTER=4,QCM_COUNTERS=5,QCM_STATUS=6,QCM_MOVE=7,QCM_HOME=8,QCM_HOME_CENTER=9};
struct qcm_request {__u32 command,axis,direction,value;};
struct qcm_status {__u32 initialized,busy,phase,last_error,sequence,ir_energized,x_counter,y_counter;};
#define QCM_SUBMIT _IOW('Q',0x71,struct qcm_request)
#define QCM_READ_STATUS _IOR('Q',0x72,struct qcm_status)
struct qcm_progress {__u32 version,generation,axis,requested,steps,reason,valid,active;};
#define QCM_READ_PROGRESS _IOR('Q',0x73,struct qcm_progress)
#endif
