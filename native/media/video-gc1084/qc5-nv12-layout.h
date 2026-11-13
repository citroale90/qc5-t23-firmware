#ifndef QC5_NV12_LAYOUT_H
#define QC5_NV12_LAYOUT_H
#include <stdint.h>
#include "qc5-frame-layout.h"

/* Full native geometry only. Reuse existing RAW reservations for Y/UV. */
struct qc5_nv12_bank {
    uint32_t y, uv, end, stride, y_bytes, uv_bytes;
};

static int qc5_nv12_plan(unsigned int bank, unsigned int width, unsigned int height,
                          struct qc5_nv12_bank *result)
{
    if (!result || bank >= QC5_DMA_BANKS || width != QC5_SENSOR_WIDTH ||
        height != QC5_SENSOR_HEIGHT) return 0;
    uint32_t rows = (height + 15u) & ~15u;
    uint32_t y = QC5_DMA_BANK(bank), y_bytes = width * rows, uv_bytes = y_bytes / 2u;
    uint32_t uv = y + y_bytes, end = uv + uv_bytes;
    if ((y | uv) & 7u || uv < y || end < uv ||
        end > y + QC5_FRAME_BYTES || end > QC5_STATS_GUARD_BASE) return 0;
    *result = (struct qc5_nv12_bank){y, uv, end, width, y_bytes, uv_bytes};
    return 1;
}
#endif
