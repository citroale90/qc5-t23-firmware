#ifndef QC5_GC1084_CONTROLS_H
#define QC5_GC1084_CONTROLS_H
#include <stdint.h>
#include "gc1084-timing.h"
struct sensor_reg { uint16_t reg; uint8_t value; };
#include "gc1084-gain.private.h"

/* Native frame length is 900 lines. Use a conservative four-line margin.
 * Reject invalid values before creating any writes, unlike the OEM ioctl. */
static int sensor_control_plan(unsigned int lines, unsigned int gain, struct sensor_reg writes[10])
{
    if (lines < 4 || lines > QC5_GC1084_MAX_EXPOSURE_LINES || gain >= 25)
        return 0;
    const uint32_t *row = gc1084_gain[gain];
    const struct sensor_reg plan[10] = {
        {0x0d03, (lines >> 8) & 0x3f}, {0x0d04, lines & 0xff},
        {0x00d1, row[1]}, {0x00d0, row[2]}, {0x031d, 0x2e},
        {0x0dc1, row[3]}, {0x031d, 0x28}, {0x0155, row[4]},
        {0x00b8, row[5]}, {0x00b9, row[6]},
    };
    for (unsigned int i = 0; i < 10; ++i) writes[i] = plan[i];
    return 1;
}

/* v17 observed the preceding gain profile when reading immediately after
 * writes. The settled caller waits before these seven reads and requires
 * exact values. Access keys and banked 0xdc1 are still not read as ordinary
 * registers. All five observed gain bytes remain in the diagnostic record. */
#define QC5_CONTROL_REQUIRED_READBACK 0x7fu
static const unsigned int sensor_readback_indices[7] = {0, 1, 2, 3, 7, 8, 9};

static uint32_t sensor_readback_mask(const struct sensor_reg plan[10], const uint8_t actual[7])
{
    uint32_t mask = 0;
    for (unsigned int i = 0; i < 7; ++i)
        if (actual[i] == plan[sensor_readback_indices[i]].value) mask |= 1u << i;
    return mask;
}
#endif
