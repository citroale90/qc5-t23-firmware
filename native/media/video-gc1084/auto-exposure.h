#ifndef QC5_AUTO_EXPOSURE_H
#define QC5_AUTO_EXPOSURE_H
#include <stdint.h>
#include "gc1084-timing.h"
#ifdef QC5_IMAGE_CONTINUOUS
#include "image-continuous.h"
#endif

/* A small controller over the exact native GC1084 gain table. No invented
 * sensor register values, floating point, frame copies, or heap allocation. */
struct qc5_ae_setting { unsigned int lines, gain; };

static int qc5_ae_next_target(struct qc5_ae_setting current, unsigned int mean,
                       unsigned int target,struct qc5_ae_setting *next)
{
    if (!next || current.lines < 4 || current.lines > QC5_AE_MAX_EXPOSURE_LINES || current.gain > 24 || mean > 255||target<32||target>224)
        return -1;
    *next = current;
    if (mean >= target-12 && mean <= target+12) return 0;
    if (mean < target-12) {
        if (current.lines < QC5_AE_MAX_EXPOSURE_LINES) {
            unsigned int value = current.lines * target / (mean ? mean : 1u);
            if (value > current.lines*2u) value = current.lines*2u;
            if (value > QC5_AE_MAX_EXPOSURE_LINES) value = QC5_AE_MAX_EXPOSURE_LINES;
            if (value <= current.lines) value = current.lines+1u;
            next->lines = value;
        } else if (current.gain < 24) ++next->gain;
    } else if (current.gain) {
        --next->gain;
    } else {
        unsigned int value = current.lines * target / mean;
        if (value < current.lines/2u) value = current.lines/2u;
        if (value < 4) value = 4;
        if (value >= current.lines && current.lines > 4) value = current.lines-1u;
        next->lines = value;
    }
    return next->lines != current.lines || next->gain != current.gain;
}
static int qc5_ae_next(struct qc5_ae_setting current,unsigned int mean,struct qc5_ae_setting *next)
{return qc5_ae_next_target(current,mean,96,next);}

/* Call only with exclusive CPU ownership of a completed, not-requeued plane. */
static int qc5_ae_meter(const volatile uint8_t *y, unsigned int bytes,
                        unsigned int *mean)
{
    if (!y || !mean || bytes != 1280u*720u) return 0;
    unsigned int sum = 0, count = 0;
    for (unsigned int row = 8; row < 720; row += 16)
        for (unsigned int col = 8; col < 1280; col += 16) {
            sum += y[row*1280u+col];
            ++count;
        }
    if (count != 3600) return 0;
    *mean = sum/count;
    return 1;
}

struct qc5_ae_controller {
    struct qc5_ae_setting selected;
    unsigned int samples, updates, pending;
    unsigned int target; /* 0 keeps the legacy diagnostic target; production can configure it. */
    uint64_t due_us;
};
typedef int (*qc5_ae_callback)(void *,struct qc5_ae_setting);

/* Nonblocking settle: frames keep flowing during the native 250ms delay.
 * A failed write/read aborts; no new request can hide an unconfirmed one. */
static int qc5_ae_tick(struct qc5_ae_controller *ae, unsigned int mean,
                       uint64_t now_us, int allow_update, qc5_ae_callback submit,
                       qc5_ae_callback verify, void *opaque)
{
    if (!ae || !submit || !verify || mean>255 ||
#ifndef QC5_IMAGE_CONTINUOUS
        ae->samples>=120 ||
#endif
        ae->selected.lines<4 || ae->selected.lines>QC5_AE_MAX_EXPOSURE_LINES || ae->selected.gain>24 ||
        (ae->pending && (!ae->due_us || ae->due_us<250000))) return 0;
#ifdef QC5_IMAGE_CONTINUOUS
    ae->samples=qc5_image_inc(ae->samples);
#else
    ++ae->samples;
#endif
    if (ae->pending) {
        if (now_us<ae->due_us) return 1;
        if (!verify(opaque,ae->selected)) return 0;
        ae->pending=0;
    }
    struct qc5_ae_setting next;
    int change=qc5_ae_next_target(ae->selected,mean,ae->target?ae->target:96u,&next);
    if (change<0) return 0;
    if (allow_update && change) {
        if (now_us>UINT64_MAX-250000 || !submit(opaque,next)) return 0;
        ae->selected=next;ae->due_us=now_us+250000;ae->pending=1;
#ifdef QC5_IMAGE_CONTINUOUS
        ae->updates=qc5_image_inc(ae->updates);
#else
        ++ae->updates;
#endif
    }
    return 1;
}
#endif
