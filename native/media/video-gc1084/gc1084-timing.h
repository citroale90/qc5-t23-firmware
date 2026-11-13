/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef QC5_GC1084_TIMING_H
#define QC5_GC1084_TIMING_H

/* Exact 1280x720 mode: OEM fps ioctl uses 0x02f34f60 Hz and reads
 * HTS from 0x0d05/06. The pinned mode sets HTS=0x0898, VTS=0x0384.
 * Keep the four-line integration margin independent of any AE ceiling. */
#define QC5_GC1084_PIXEL_CLOCK_HZ 49500000u
#define QC5_GC1084_LINE_CLOCKS 2200u
#define QC5_GC1084_FRAME_LINES 900u
#define QC5_GC1084_MAX_EXPOSURE_LINES 896u

/* Experimental build policy; the normal firmware retains its original cap.
 * 450 lines = 20 ms (1/50 s), while frame timing remains 25 fps. */
#ifndef QC5_AE_MAX_EXPOSURE_LINES
#define QC5_AE_MAX_EXPOSURE_LINES QC5_GC1084_MAX_EXPOSURE_LINES
#endif
#if QC5_AE_MAX_EXPOSURE_LINES < 4 || QC5_AE_MAX_EXPOSURE_LINES > QC5_GC1084_MAX_EXPOSURE_LINES
#error "AE exposure ceiling must fit the verified sensor integration range"
#endif
#define QC5_AE_INITIAL_EXPOSURE_LINES \
    (QC5_AE_MAX_EXPOSURE_LINES < 720u ? QC5_AE_MAX_EXPOSURE_LINES : 720u)
#endif
