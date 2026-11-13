/* One-frame GC1084 raw capture for the QC5 RAM diagnostic. */
#define _POSIX_C_SOURCE 200809L
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "qc5-frame-layout.h"
#include "gc1084-controls.h"

enum capture_status {
    CAPTURE_OK, CAPTURE_SENSOR, CAPTURE_MAP, CAPTURE_CLOCK, CAPTURE_CSI,
    CAPTURE_SENSOR_MODE, CAPTURE_VIC, CAPTURE_TIMEOUT, CAPTURE_FRAME,
};

enum {
    SENTINEL = 0xa5a5, FRAME_WORDS = QC5_FRAME_BYTES / 2,
    GUARD_WORDS = 4096 / 2, VIC_ERRORS = 0xde00,
    VIC_IRQ_STATUS = 0x1e0, VIC_IRQ_MASK = 0x1e8, VIC_IRQ_CLEAR = 0x1f0,
    VIC_DMA_STATUS = 0x1e4, VIC_DMA_CONTROL = 0x300, VIC_DMA_ENABLE = 0x308, VIC_DMA_CURRENT = 0x380,
    VIC_INPUT_DONE = 1, VIC_INPUT_START = 0x10, VIC_INPUT_EVENTS = 0x11,
};

_Static_assert(QC5_DMA_BANK(QC5_DMA_BANKS) <= QC5_PREVIEW_PHYS,
               "All frame banks and guards must fit in reserved RAM");
_Static_assert(QC5_BANK_SPACING % 4096 == 0, "Bank addresses must be page aligned");

static int frame_bank(uint32_t address)
{
    if (address < QC5_FRAME_PHYS || address >= QC5_DMA_BANK(QC5_DMA_BANKS))
        return -1;
    uint32_t offset = address - QC5_FRAME_PHYS;
    if (offset % QC5_BANK_SPACING >= QC5_FRAME_BYTES)
        return -1;
    return offset / QC5_BANK_SPACING;
}

static int sample_preview(const volatile uint16_t *pixels, unsigned int packing,
                           uint8_t *output, unsigned int width, unsigned int height)
{
    for (unsigned int y = 0; y < height; ++y) {
        for (unsigned int x = 0; x < width; ++x) {
            unsigned int row = (2 * y + 1) * QC5_SENSOR_HEIGHT / (2 * height);
            unsigned int col = (2 * x + 1) * QC5_SENSOR_WIDTH / (2 * width);
            unsigned int sum = 0;
            row &= ~1u;
            col &= ~1u;
            for (unsigned int j = 0; j < 2; ++j)
                for (unsigned int i = 0; i < 2; ++i) {
                    unsigned int value = pixels[(row + j) * QC5_SENSOR_WIDTH + col + i];
                    if (value == SENTINEL) return 0;
                    sum += packing == 2 ? value >> 6 : value;
                }
            output[y * width + x] = sum >> 4;
        }
    }
    return 1;
}

static int frame_preview(const volatile uint16_t *pixels, struct qc5_preview *record)
{
    unsigned int changed = 0, high_bits = 0, low_bits = 0;

    for (unsigned int i = 0; i < GUARD_WORDS; ++i)
        if (pixels[-(int)i - 1] != SENTINEL || pixels[FRAME_WORDS + i] != SENTINEL)
            return 0;

    for (unsigned int row = 0; row < QC5_SENSOR_HEIGHT; ++row) {
        unsigned int touched = 0;
        for (unsigned int col = 0; col < QC5_SENSOR_WIDTH; ++col) {
            uint16_t v = pixels[row * QC5_SENSOR_WIDTH + col];
            if (v == SENTINEL)
                continue;
            ++touched;
            high_bits |= v & 0xfc00u;
            low_bits |= v & 0x003fu;
        }
        if (touched < QC5_SENSOR_WIDTH * 15 / 16)
            return 0;
        changed += touched;
    }
    if (high_bits && low_bits)
        return 0;

    record->packing = high_bits ? 2 : 1;
    record->changed_words = changed;
    record->frame_hash = qc5_frame_hash((const volatile uint8_t *)pixels, QC5_FRAME_BYTES);
    return sample_preview(pixels, record->packing, record->pixels, QC5_PREVIEW_WIDTH, QC5_PREVIEW_HEIGHT);
}

static uint32_t dma_control_for(unsigned int frames)
{
    return frames && frames <= QC5_DMA_BANKS ? 0x80000020u | (frames << 16) : 0;
}

static int sequence_complete(uint32_t control, unsigned int events)
{
    return control == 0x80000020u && events == QC5_SEQUENCE_FRAMES;
}

static int validate_banks(const volatile uint16_t *pixels, struct qc5_preview *record,
                          struct qc5_sequence_entry entries[QC5_DMA_BANKS], uint8_t preview_pixels[16 * 9])
{
    for (unsigned int bank = 0; bank < QC5_SEQUENCE_FRAMES; ++bank) {
        const volatile uint16_t *buffer = pixels + bank * (QC5_BANK_SPACING / 2);
        struct qc5_preview temporary = {0};
        if (!frame_preview(buffer, &temporary) || temporary.changed_words != FRAME_WORDS)
            return 0;
        uint32_t minimum = 1023, maximum = 0, sum = 0;
        for (unsigned int i = 0; i < FRAME_WORDS; ++i) {
            uint32_t sample = temporary.packing == 2 ? buffer[i] >> 6 : buffer[i];
            if (sample < minimum) minimum = sample;
            if (sample > maximum) maximum = sample;
            sum += sample; /* 921600 * 1023 fits in uint32_t. */
        }
        entries[bank] = (struct qc5_sequence_entry){
            QC5_DMA_BANK(bank), temporary.frame_hash, temporary.changed_words,
            temporary.packing, minimum, maximum, sum / FRAME_WORDS, bank,
        };
        
        if (bank == QC5_SEQUENCE_FRAMES - 1) {
            record->frame_hash = temporary.frame_hash;
            record->changed_words = temporary.changed_words;
            record->packing = temporary.packing;
            record->selected_frame_phys = QC5_DMA_BANK(bank);
            if (!sample_preview(buffer, temporary.packing, preview_pixels, 16, 9)) return 0;
        }
    }
    return 1;
}

/* Called only after stopping core/VIC/MDMA; the five buffers stay reserved. */
static int pack_sequence(const volatile uint16_t *pixels, struct qc5_preview *record,
                         struct qc5_frame_sequence *sequence)
{
    if (!sequence_complete(sequence->control_at_end, sequence->dma_events) ||
        sequence->observed_banks != (1u << QC5_SEQUENCE_FRAMES) - 1 ||
        sequence->remaining || sequence->stop_mask != 15 ||
        !sequence->first_dma_poll || sequence->first_dma_poll > sequence->last_dma_poll ||
        sequence->last_dma_poll > sequence->polls || sequence->polls > 2000)
        return 0;
    sequence->magic = QC5_SEQUENCE_MAGIC;
    sequence->version = 2;
    sequence->target = QC5_SEQUENCE_FRAMES;
    sequence->validated = 0;
    sequence->preview_width = 16;
    sequence->preview_height = 9;
    if (!validate_banks(pixels, record, sequence->frames, sequence->pixels)) return 0;
    sequence->validated = QC5_SEQUENCE_FRAMES;
    memcpy(record->pixels, sequence, sizeof(*sequence));
    return 1;
}

/* Compact proof per gain: mean[0:9], gain[10:14], readback[15:21],
 * validated-frame count[22:24], observed stop controls[25:28]. */
static uint32_t gain_stage_proof(const struct qc5_frame_sequence *sequence,
                                  unsigned int gain, uint32_t readback)
{
    if (sequence->validated != 5 || sequence->stop_mask != 15 || gain >= 25 ||
        (readback & QC5_CONTROL_REQUIRED_READBACK) != QC5_CONTROL_REQUIRED_READBACK || (readback & ~127u))
        return 0;
    uint32_t mean = 0;
    for (unsigned int n = 0; n < 5; ++n) {
        if (sequence->frames[n].changed_words != FRAME_WORDS || sequence->frames[n].mean > 1023)
            return 0;
        mean += sequence->frames[n].mean;
    }
    return mean / 5 | gain << 10 | readback << 15 | 5u << 22 | 15u << 25;
}

static void pack_gain_readbacks(struct qc5_preview *record, const uint8_t reads[3][5])
{
    uint32_t packed[4] = {0, 0, 0, 0x47000000u};
    for (unsigned int stage = 0; stage < 3; ++stage)
        for (unsigned int field = 0; field < 5; ++field) {
            unsigned int index = stage * 5 + field;
            packed[index / 4] |= (uint32_t)reads[stage][field] << (8 * (index % 4));
        }
    record->phy_state_seen = packed[0];
    record->phy_state_transitions = packed[1];
    record->phy_timing = packed[2];
    record->isp_route_blocker = packed[3];
}

static void finish_record(struct qc5_preview *record, unsigned int status)
{
    record->magic = QC5_PREVIEW_MAGIC;
    record->version = QC5_PREVIEW_VERSION;
    record->width = QC5_SENSOR_WIDTH;
    record->height = QC5_SENSOR_HEIGHT;
    record->frame_bytes = QC5_FRAME_BYTES;
    record->status = status;
    record->record_hash = qc5_frame_hash((const uint8_t *)record,
                                         offsetof(struct qc5_preview, record_hash));
}

#ifdef QC5_FRAME_TEST
#define QC5_SENSOR_TEST
#endif
#define QC5_SENSOR_NO_MAIN
#include "gc1084-probe.c"

enum {
    ISP_DIV = 0x80, ISP_GATE = BIT(23), CSI_GATE = BIT(25),
    CLKGR1 = 0x28, VPU_DIV = 0x30, VPU_GATE = BIT(0), IVDC_GATE = BIT(3),
    SRBC = 0xc4, ISP_STOP_REQUEST = BIT(21), ISP_STOP_ACK = BIT(20), ISP_RESET = BIT(22),
};

static int media_divider(unsigned int reg, unsigned int divider)
{
    const uint32_t mask = UINT32_C(0xf800000f);
    uint32_t requested = BIT(30) | (divider - 1u);
    if (!wait_bits(CPM, reg, CIM_BUSY, 0))
        return 0;
    uint32_t current = reg_read(CPM, reg);
    if ((current & mask) == requested)
        return 1;
    uint32_t value = (current & ~mask) | requested;
    reg_write(CPM, reg, value | CIM_LATCH);
    if (!wait_bits(CPM, reg, CIM_BUSY, 0))
        return 0;
    reg_write(CPM, reg, value);
    return (reg_read(CPM, reg) & mask) == requested;
}

static int video_clocks(struct qc5_preview *record)
{
    record->vpu_clock_before = reg_read(CPM, VPU_DIV);
    uint32_t gate1 = reg_read(CPM, CLKGR1);
    if (!(gate1 & VPU_GATE))
        record->capture_flags |= QC5_VPU_GATE_OPEN_BEFORE;
    if (!(gate1 & IVDC_GATE))
        record->capture_flags |= QC5_IVDC_GATE_OPEN_BEFORE;
    /* The stock QC5 drives ISP processing at 200 MHz and ISP AXI at 600 MHz. */
    int ok = pll_rate(reg_read(CPM, MPLL)) == 1200000000u &&
             media_divider(VPU_DIV, 2) && media_divider(ISP_DIV, 6);
    if (ok) {
        /* Match both stock video gates. Do not program/start the IVDC engine. */
        if (gate1 & (VPU_GATE | IVDC_GATE))
            reg_write(CPM, CLKGR1, gate1 & ~(VPU_GATE | IVDC_GATE));
        reg_write(CPM, CLKGR, reg_read(CPM, CLKGR) & ~(ISP_GATE | CSI_GATE));
    }
    record->vpu_clock_after = reg_read(CPM, VPU_DIV);
    record->isp_clock = reg_read(CPM, ISP_DIV);
    if (!(reg_read(CPM, CLKGR1) & VPU_GATE))
        record->capture_flags |= QC5_VPU_GATE_OPEN_AFTER;
    if (!(reg_read(CPM, CLKGR1) & IVDC_GATE))
        record->capture_flags |= QC5_IVDC_GATE_OPEN_AFTER;
    return ok && (record->capture_flags & QC5_VPU_GATE_OPEN_AFTER) &&
           (record->capture_flags & QC5_IVDC_GATE_OPEN_AFTER) &&
           !(reg_read(CPM, CLKGR) & (ISP_GATE | CSI_GATE));
}

static int reset_isp(struct qc5_preview *record)
{
    /* Exact T23 OEM private_reset_tx_isp_module(0) sequence. The bus must
     * acknowledge its stop request before pulsing reset; no DMA is armed. */
    uint32_t before = reg_read(CPM, SRBC);
    record->capture_flags |= ((before >> 20) & 7u) << QC5_SRBC_BEFORE_SHIFT;
    record->capture_flags |= QC5_ISP_RESET_REQUESTED;
    reg_write(CPM, SRBC, before | ISP_STOP_REQUEST);
    for (unsigned int i = 0; i < 500; ++i) {
        uint32_t current = reg_read(CPM, SRBC);
        if (current & ISP_STOP_ACK) {
            record->capture_flags |= QC5_ISP_RESET_ACK_SEEN;
            reg_write(CPM, SRBC, (current & ~ISP_STOP_REQUEST) | ISP_RESET);
            reg_write(CPM, SRBC, reg_read(CPM, SRBC) & ~ISP_RESET);
            record->capture_flags |= QC5_ISP_RESET_PULSED;
            uint32_t after = reg_read(CPM, SRBC);
            record->capture_flags |= ((after >> 20) & 7u) << QC5_SRBC_AFTER_SHIFT;
            if ((after & (ISP_STOP_REQUEST | ISP_RESET)) == 0 &&
                (before & ~(ISP_STOP_REQUEST | ISP_STOP_ACK | ISP_RESET)) ==
                (after & ~(ISP_STOP_REQUEST | ISP_STOP_ACK | ISP_RESET))) {
                record->capture_flags |= QC5_ISP_RESET_VERIFIED;
                return 1;
            }
            return 0;
        }
        delay_us(2000);
    }
    /* Like the OEM routine, leave the stop request asserted on timeout.
     * Do not start CSI/VIC/DMA; the independent watchdog still returns stock. */
    record->capture_flags |= QC5_ISP_RESET_TIMEOUT;
    record->capture_flags |= ((reg_read(CPM, SRBC) >> 20) & 7u) << QC5_SRBC_AFTER_SHIFT;
    return 0;
}

static void mmio(volatile uint32_t *base, unsigned int offset, uint32_t value)
{
    base[offset / 4] = value;
#ifndef QC5_FRAME_TEST
    __asm__ volatile("sync" ::: "memory");
#endif
}

static unsigned int isp_input_route(volatile uint32_t *core, struct qc5_preview *record)
{
    static const struct { unsigned int offset; uint32_t mask; } guard[] = {
        {0x800, UINT32_MAX}, {0xd040, UINT32_MAX}, {0x7804, 1}, {0x1060, 1},
    };
    for (unsigned int i = 0; i < sizeof(guard) / sizeof(guard[0]); ++i) {
        uint32_t value = core[guard[i].offset / 4];
        if (value & guard[i].mask) {
            record->isp_route_blocker = guard[i].offset;
            record->dma_addr_before = value;
            return QC5_ISP_ROUTE_REFUSED;
        }
    }

    /* T23 core statistics shutdown order. The main core, MSCA and MDNS
     * remain disabled. Only write the statistics stop registers. */
    static const unsigned int statistics[] = {
        0xb000, 0xa04c, 0xa84c, 0xb04c, 0x4490, 0x5b80, 0xb8b8,
    };
    for (unsigned int i = 0; i < sizeof(statistics) / sizeof(statistics[0]); ++i) {
        unsigned int reg = statistics[i];
        if (core[reg / 4] != 0) {
            mmio(core, reg, 0);
            record->capture_flags |= QC5_ISP_STATS_STOP_WRITTEN;
        }
    }
    for (unsigned int i = 0; i < sizeof(statistics) / sizeof(statistics[0]); ++i) {
        unsigned int reg = statistics[i];
        uint32_t value = core[reg / 4];
        if (value != 0) {
            record->isp_route_blocker = reg;
            record->dma_addr_before = value; /* Schema 6 carries the refused control value. */
            return QC5_ISP_ROUTE_REFUSED;
        }
    }

    const uint32_t geometry = (QC5_SENSOR_WIDTH << 16) | QC5_SENSOR_HEIGHT;
    uint32_t top = core[0x0c / 4];
    mmio(core, 0x04, geometry);
    mmio(core, 0x08, 1); /* GC1084 Bayer order measured with the stock ISP. */
    mmio(core, 0x2c, 0x00400040);
    mmio(core, 0x0c, top | BIT(31)); /* OEM tisp_top_sel(), without starting the ISP core. */
    if (core[0x04 / 4] != geometry || core[0x08 / 4] != 1 ||
        core[0x2c / 4] != 0x00400040 || !(core[0x0c / 4] & BIT(31)))
        return QC5_ISP_ROUTE_READBACK_FAILED;
    return QC5_ISP_ROUTE_APPLIED;
}

/* v12 hardware: the native 0xf3 write reads back as 0xc0 after top/ring
 * preparation without another software write to 0x10. Accept these exact
 * measured states only, not arbitrary bit subsets or an unconfigured zero. */
static int isp_main_ready(uint32_t value)
{
    return value == 0xf3 || value == 0xc0;
}

static int isp_main_configure(volatile uint32_t *core, struct qc5_preview *record)
{
    /* The exact OEM tisp_init prefix writes 0xf3 for GC1084 Bayer type 1
     * before tuning/DMA allocation. Keep the IPC trigger and all writers off. */
    static const struct { unsigned int offset; uint32_t mask; } guard[] = {
        {0x800, UINT32_MAX}, {0x804, UINT32_MAX}, {0xd040, UINT32_MAX},
        {0x7804, 1}, {0x1060, 1}, {0xb000, UINT32_MAX},
        {0xa04c, UINT32_MAX}, {0xa84c, UINT32_MAX}, {0xb04c, UINT32_MAX},
        {0x4490, UINT32_MAX}, {0x5b80, UINT32_MAX}, {0xb8b8, UINT32_MAX},
    };
    for (unsigned int i = 0; i < sizeof(guard) / sizeof(guard[0]); ++i) {
        uint32_t value = core[guard[i].offset / 4];
        if (value & guard[i].mask) {
            record->capture_flags |= QC5_CAPTURE_VARIANT_REFUSED;
            record->isp_route_blocker = guard[i].offset;
            record->dma_addr_before = value;
            return 0;
        }
    }
    mmio(core, 0x10, 0xf3);
    if (!isp_main_ready(core[0x10 / 4])) {
        record->capture_flags |= QC5_CAPTURE_VARIANT_REFUSED;
        record->isp_route_blocker = 0x10;
        record->dma_addr_before = core[0x10 / 4];
        return 0;
    }
    record->capture_flags |= QC5_ISP_MAIN_CONFIGURED;
    return 1;
}

static const unsigned int core_stat_controls[] = {
    0xb000, 0xa04c, 0xa84c, 0xb04c, 0x4490, 0x5b80, 0xb8b8,
};
static const struct { unsigned int base, entries, size, enable; } core_stat_rings[] = {
    {0xa02c, 8, 0x6000, 0x33}, {0xa82c, 8, 0x6000, 0}, {0xb03c, 4, 0x4000, 3},
    {0x4494, 4, 0x4000, 3}, {0x5b84, 4, 0x4000, 3}, {0xb8a8, 4, 0x4000, 3},
};
/* AE1 belongs to the second exposure. Its eight pointers and control read zero
 * on this exact stock GC1084 non-WDR camera. Never arm it or require its pointer
 * registers to retain writes. Keep its reserved span so other bases stay fixed. */
static const unsigned int core_stat_offsets[] = {0, 0x1000, 0x2000, 0x3000, 0x4000, 0x4800, 0x5000, 0x5800};

static int core_refuse(struct qc5_preview *record, unsigned int offset, uint32_t value)
{
    record->capture_flags |= QC5_CAPTURE_VARIANT_REFUSED;
    record->isp_route_blocker = offset;
    record->dma_addr_before = value;
    return 0;
}

static int core_ring_addresses(volatile uint32_t *core, struct qc5_preview *record, int program)
{
    uint32_t physical = QC5_STATS_BASE;
    for (unsigned int ring = 0; ring < 6; ++ring) {
        for (unsigned int n = 0; core_stat_rings[ring].enable && n < core_stat_rings[ring].entries; ++n) {
            unsigned int offset = core_stat_rings[ring].base + n * 4;
            uint32_t address = physical + core_stat_offsets[n];
            if (address < physical || address >= physical + core_stat_rings[ring].size ||
                physical + core_stat_rings[ring].size > QC5_STATS_END)
                return core_refuse(record, offset, address);
            if (program)
                mmio(core, offset, address);
            if (core[offset / 4] != address)
                return core_refuse(record, offset, core[offset / 4]);
        }
        physical += core_stat_rings[ring].size;
    }
    return physical == QC5_STATS_END;
}

static int core_writers_off(volatile uint32_t *core, struct qc5_preview *record)
{
    if (core[0xd040 / 4])
        return core_refuse(record, 0xd040, core[0xd040 / 4]);
    for (unsigned int i = 0; i < sizeof(core_stat_controls) / sizeof(core_stat_controls[0]); ++i) {
        unsigned int at = core_stat_controls[i];
        if (core[at / 4])
            return core_refuse(record, at, core[at / 4]);
    }
    return 1;
}

static int core_prepare(volatile uint32_t *core, volatile uint8_t *guard_memory, struct qc5_preview *record)
{
    if (!isp_main_configure(core, record) || !core_writers_off(core, record))
        return 0;
    for (unsigned int i = 0; i < QC5_STATS_BYTES + 8192u; ++i)
        guard_memory[i] = 0xa5;
    /* Five active native ring layouts are redirected inside the existing no-map
     * reservation. AE1 stays off; all writer controls are still zero here. */
    if (!core_ring_addresses(core, record, 1))
        return 0;
    mmio(core, 0x0c, 0xb577fffd); /* Exact OEM non-WDR mask with tunable blocks bypassed. */
    mmio(core, 0x1c, 0);
    mmio(core, 0x30, UINT32_MAX);
    if (core[0x0c / 4] != 0xb577fffd || core[0x1c / 4] != 0)
        return core_refuse(record, 0x0c, core[0x0c / 4]);
    return core_writers_off(core, record) && core_ring_addresses(core, record, 0);
}

static int core_stop(volatile uint32_t *core, struct qc5_preview *record);

static int core_trigger(volatile uint32_t *core, struct qc5_preview *record, uint32_t soc_mask)
{
    const uint32_t irq_mask = BIT(29) | BIT(30);
    if ((soc_mask & irq_mask) != irq_mask)
        return core_refuse(record, 0x10001004, soc_mask);
    for (unsigned int offset = 0x800; offset <= 0x804; offset += 4)
        if (core[offset / 4])
            return core_refuse(record, offset, core[offset / 4]);
    if (!isp_main_ready(core[0x10 / 4]))
        return core_refuse(record, 0x10, core[0x10 / 4]);
    if (core[0x0c / 4] != 0xb577fffd)
        return core_refuse(record, 0x0c, core[0x0c / 4]);
    if (!core_writers_off(core, record) || !core_ring_addresses(core, record, 0))
        return 0;
    /* Arm only the five active non-WDR ring controls after every used pointer
     * has been checked. AE1, MSCA and AWB global enable remain off. */
    for (unsigned int i = 0; i < 6; ++i) {
        unsigned int control = core_stat_controls[i + 1];
        uint32_t value = core_stat_rings[i].enable;
        if (value)
            mmio(core, control, value);
        if (core[control / 4] != value) {
            uint32_t actual = core[control / 4];
            (void)core_stop(core, record);
            return core_refuse(record, control, actual);
        }
    }
    mmio(core, 0x804, 0x1c); /* Exact OEM IPC command for the GC1084 input type. */
    if (core[0x804 / 4] != 0x1c) {
        (void)core_stop(core, record);
        return core_refuse(record, 0x804, core[0x804 / 4]);
    }
    mmio(core, 0x800, 1); /* Pulse may self-clear: do not require it to remain one. */
    record->capture_flags |= QC5_CORE_TRIGGERED;
    return 1;
}

static int core_stop(volatile uint32_t *core, struct qc5_preview *record)
{
    mmio(core, 0x800, 0);
    for (unsigned int i = 0; i < sizeof(core_stat_controls) / sizeof(core_stat_controls[0]); ++i)
        mmio(core, core_stat_controls[i], 0);
    return core_writers_off(core, record);
}

static int core_pool_guard(const volatile uint8_t *memory, struct qc5_preview *record)
{
    for (unsigned int i = 0; i < 4096; ++i) {
        if (memory[i] != 0xa5 || memory[4096 + QC5_STATS_BYTES + i] != 0xa5)
            return core_refuse(record, 0x20000, i);
    }
    for (unsigned int i = 0; i < core_stat_rings[1].size; ++i)
        if (memory[4096 + core_stat_rings[0].size + i] != 0xa5)
            return core_refuse(record, 0x21000, i); /* Disabled AE1 span was written. */
    if (!(record->capture_flags & QC5_CAPTURE_VARIANT_REFUSED))
        record->isp_route_blocker = qc5_frame_hash(memory + 4096, QC5_STATS_BYTES);
    return 1;
}

static struct qc5_preview retry_record(const struct qc5_preview *record)
{
    return (struct qc5_preview){
        .vpu_clock_before = record->vpu_clock_before,
        .vpu_clock_after = record->vpu_clock_after,
        .isp_clock = record->isp_clock,
        .capture_flags = (record->capture_flags & ~(QC5_INPUT_SYNC | QC5_BANKS_VERIFIED |
                          QC5_DMA_ENABLE_PULSE | QC5_CORE_TRIGGERED | QC5_VIC_EVENT_ACK_WRITTEN |
                          QC5_VIC_EVENT_ACK_VERIFIED | QC5_VIC_STARTED_AFTER_DMA)) |
                         QC5_VIC_EVENT_POLL,
    };
}

#ifdef QC5_FRAME_TEST
uint32_t frame_read_vic(unsigned int offset);
void frame_write_vic(unsigned int offset, uint32_t value);
uint32_t frame_read_register(uint32_t address);
#else
static volatile uint32_t *csi, *phy, *vic, *intc, *core;

static uint32_t frame_read_vic(unsigned int offset)
{
    return vic[offset / 4];
}

static void frame_write_vic(unsigned int offset, uint32_t value)
{
    mmio(vic, offset, value);
}

static uint32_t frame_read_register(uint32_t address)
{
    if ((address & 0xfffff000u) == 0x10000000)
        return reg_read(CPM, address & 0xfffu);
    if ((address & 0xfffff000u) == 0x10023000)
        return csi[(address & 0xfffu) / 4];
    if ((address & 0xfffff000u) == 0x10022000)
        return phy[(address & 0xfffu) / 4];
    if ((address & 0xfffff000u) == 0x133e0000)
        return vic[(address & 0xfffu) / 4];
    return core[(address & 0xffffu) / 4];
}
#endif

static int core_mode_for_restart(volatile uint32_t *isp, struct qc5_preview *record)
{
    if (frame_read_vic(VIC_DMA_CONTROL) || frame_read_vic(VIC_DMA_ENABLE) || frame_read_vic(0))
        return core_refuse(record, 0x10000, frame_read_vic(0));
    if (!core_writers_off(isp, record)) return 0;
    if (isp[0x800 / 4]) return core_refuse(record, 0x800, isp[0x800 / 4]);
    if (isp[0x804 / 4] != 0 && isp[0x804 / 4] != 0x1c)
        return core_refuse(record, 0x804, isp[0x804 / 4]);
    if (isp[0x804 / 4]) mmio(isp, 0x804, 0);
    return isp[0x804 / 4] == 0 || core_refuse(record, 0x804, isp[0x804 / 4]);
}

static int vic_start_after_dma(struct qc5_preview *record, int banks_ok, unsigned int frames)
{
    /* Do not replace a core/pointer refusal with a generic VIC error. */
    if (!banks_ok)
        return 0;
    if (!(record->capture_flags & QC5_VIC_START_DEFERRED))
        return 1;
    if (!(record->capture_flags & QC5_BANKS_VERIFIED) ||
        frame_read_vic(0) != 0 || !dma_control_for(frames) || frame_read_vic(VIC_DMA_CONTROL) != dma_control_for(frames)) {
        record->capture_flags |= QC5_CAPTURE_VARIANT_REFUSED;
        record->isp_route_blocker = 0x10000;
        return 0;
    }
    frame_write_vic(0, 1);
    if (frame_read_vic(0) != 1) {
        record->capture_flags |= QC5_CAPTURE_VARIANT_REFUSED;
        record->isp_route_blocker = 0x10000;
        return 0;
    }
    record->capture_flags |= QC5_VIC_STARTED_AFTER_DMA;
    return 1;
}

static uint32_t poll_vic_events(struct qc5_preview *record, struct qc5_capture_snapshot *snapshot,
                                 uint32_t previous)
{
    uint32_t seen = frame_read_vic(VIC_IRQ_STATUS);
    uint32_t edges = seen & ~previous;
    ++snapshot->polls;
    snapshot->input_done_events += !!(edges & VIC_INPUT_DONE);
    snapshot->input_start_events += !!(edges & VIC_INPUT_START);
    record->vic_status |= seen;
    if (seen & VIC_INPUT_DONE)
        record->capture_flags |= QC5_INPUT_SYNC;
    if (record->capture_flags & QC5_VIC_EVENT_POLL) {
        /* Match the OEM IRQ's W1C acknowledgement for unmasked input events.
         * Keep SoC IRQ30 masked; the diagnostic polls and needs no IRQ handler. */
        uint32_t pending = seen & ~frame_read_vic(VIC_IRQ_MASK) & VIC_INPUT_EVENTS;
        if (pending) {
            frame_write_vic(VIC_IRQ_CLEAR, pending);
            record->capture_flags |= QC5_VIC_EVENT_ACK_WRITTEN;
            uint32_t after = frame_read_vic(VIC_IRQ_STATUS);
            record->vic_status |= after;
            uint32_t cleared = pending & ~after;
            snapshot->input_done_ack_verified += !!(cleared & VIC_INPUT_DONE);
            snapshot->input_start_ack_verified += !!(cleared & VIC_INPUT_START);
            if (cleared)
                record->capture_flags |= QC5_VIC_EVENT_ACK_VERIFIED;
            return after & VIC_INPUT_EVENTS;
        }
    }
    return seen & VIC_INPUT_EVENTS;
}

static void collect_snapshot(struct qc5_capture_snapshot *snapshot)
{
    snapshot->magic = QC5_SNAPSHOT_MAGIC;
    snapshot->version = 1;
    snapshot->register_count = 44;
    for (unsigned int i = 0; i < snapshot->register_count; ++i) {
        snapshot->registers[i].address = qc5_snapshot_addresses[i];
        snapshot->registers[i].value = frame_read_register(qc5_snapshot_addresses[i]);
    }
}

static int capture_failure(struct qc5_preview *record, const struct qc5_capture_snapshot *snapshot,
                            int status)
{
    memcpy(record->pixels, snapshot, sizeof(*snapshot));
    return status;
}

#ifndef QC5_FRAME_TEST
#include "gc1084-mode.private.h"
static uint32_t sensor_apb_rate, active_gain, active_readback;
static uint8_t active_gain_readbacks[5];
#ifdef QC5_SENSOR_ORIENTATION
#include "sensor-orientation.h"
static struct qc5_orientation_note active_orientation;
static uint32_t orientation_last_reg,orientation_last_value;
#endif

static void csi_start(void)
{
    mmio(csi, 0x04, 0); /* one lane */
    mmio(csi, 0x08, csi[0x08 / 4] & ~1u);
    mmio(csi, 0x0c, 0);
    delay_us(1000);
    mmio(csi, 0x10, csi[0x10 / 4] & ~1u);
    delay_us(1000);
    mmio(csi, 0x0c, 1);
    delay_us(1000);
    mmio(csi, 0x100, 3);
    mmio(vic, 0x0c, 1);
    mmio(phy, 0x00, 0x7d);
    mmio(phy, 0x128, 0x3f);
    mmio(csi, 0x10, 1);
    delay_us(10000);
}

static int sensor_write(uint16_t address, uint8_t value)
{
    (void)reg_read(I2C0, I2C_CLR_INTR);
    const uint32_t words[] = {address >> 8, address & 0xff, I2C_STOP | value};
    for (unsigned int i = 0; i < 3; ++i) {
        if (!wait_bits(I2C0, I2C_STATUS, I2C_TFNF, I2C_TFNF))
            return 0;
        reg_write(I2C0, I2C_DATA_CMD, words[i]);
    }
    for (unsigned int i = 0; i < 30; ++i) {
        uint32_t flags = reg_read(I2C0, I2C_RAW_INTR);
        if (flags & I2C_TX_ABRT)
            return 0;
        if (flags & I2C_STOP_DET)
            return 1;
        delay_us(1000);
    }
    return 0;
}

#ifdef QC5_SENSOR_ORIENTATION
static int orientation_read(void *unused,uint16_t reg,uint8_t *value)
{
    (void)unused;enum result result=read_sensor(reg,value);orientation_last_reg=reg;
    orientation_last_value=result==SENSOR_ID_OK?*value:0x100u+(unsigned)result;
    return result==SENSOR_ID_OK;
}
static int orientation_write(void *unused,uint16_t reg,uint8_t value)
{
    (void)unused;int ok=sensor_write(reg,value);orientation_last_reg=reg;
    orientation_last_value=ok?value:0x200u;return ok;
}
static void orientation_pause(void *unused,unsigned us){(void)unused;delay_us(us);}
static int sensor_orientation_finish(struct qc5_preview *record)
{
    uint32_t old_gate=reg_read(CPM,CLKGR);
    reg_write(CPM,CLKGR,old_gate&~I2C_GATE);
    int ok=configure_i2c(sensor_apb_rate);
    const uint16_t regs[2]={0x0015,0x0d15};
    for(unsigned n=0;ok&&n<2;++n){
        uint8_t v=0;ok=orientation_read(NULL,regs[n],&v)&&v==active_orientation.after[n];
        if(ok)active_orientation.after[n]=v;
    }
    ok=i2c_enable(0)&&ok;
    reg_write(CPM,CLKGR,(reg_read(CPM,CLKGR)&~I2C_GATE)|(old_gate&I2C_GATE));
    active_orientation.bayer=core[8/4];
    return ok&&qc5_orientation_note_valid(&active_orientation)?1:core_refuse(record,0x30015,0x102);
}
#endif

static int sensor_start(struct qc5_preview *record)
{
    (void)record;
    uint32_t apb_rate;
    /* The ID probe disabled I2C. Reopen it without resetting the sensor. */
    if (!configure_clock(&apb_rate))
        return 0;
    uint32_t old_gate = reg_read(CPM, CLKGR);
    reg_write(CPM, CLKGR, old_gate & ~I2C_GATE);
    sensor_apb_rate = apb_rate;
    int ok = configure_i2c(apb_rate);
    for (unsigned int i = 0; ok && i < sizeof(gc1084_mode) / sizeof(gc1084_mode[0]); ++i)
        ok = sensor_write(gc1084_mode[i].reg, gc1084_mode[i].value);
#ifdef QC5_SENSOR_ORIENTATION
#ifndef QC5_ORIENTATION_SETTLE_US
#define QC5_ORIENTATION_SETTLE_US 0u
#endif
    const struct qc5_orientation_io orientation_io={NULL,orientation_read,orientation_write,orientation_pause,QC5_ORIENTATION_SETTLE_US};
    if(ok&&QC5_ORIENTATION_SETTLE_US)delay_us(QC5_ORIENTATION_SETTLE_US);
    if(ok)ok=qc5_orientation_apply(&orientation_io,QC5_SENSOR_ORIENTATION,&active_orientation);
    if(!ok&&record)core_refuse(record,0x30000u+orientation_last_reg,orientation_last_value);
#endif
    ok = i2c_enable(0) && ok;
    reg_write(CPM, CLKGR, old_gate);
    return ok;
}

static int sensor_apply_controls(unsigned int gain, struct qc5_preview *record)
{
    struct sensor_reg plan[10];
    uint8_t actual[7] = {0};
    active_readback = 0;
    if (!sensor_control_plan(720, gain, plan) || sensor_apb_rate < 1000000 || sensor_apb_rate > 200000000)
        return core_refuse(record, 0x3ffff, gain);
    /* Caller has confirmed stopped VIC/MDMA/core writers. Do not retune MCLK. */
    uint32_t old_gate = reg_read(CPM, CLKGR);
    reg_write(CPM, CLKGR, old_gate & ~I2C_GATE);
    int ok = configure_i2c(sensor_apb_rate);
    unsigned int failed = 0xffff;
    uint32_t value = 0;
    for (unsigned int n = 0; ok && n < 10; ++n) {
        ok = sensor_write(plan[n].reg, plan[n].value);
        if (!ok) { failed = plan[n].reg; value = 0x100; }
    }
    if (ok) delay_us(250000); /* Let the sensor cross several frame boundaries before readback. */
    for (unsigned int n = 0; ok && n < 7; ++n) {
        struct sensor_reg expected = plan[sensor_readback_indices[n]];
        ok = read_sensor(expected.reg, &actual[n]) == SENSOR_ID_OK;
        if (!ok || actual[n] != expected.value) {
            failed = expected.reg; value = ok ? actual[n] : 0x101; ok = 0;
        }
    }
    active_readback = sensor_readback_mask(plan, actual);
    ok = i2c_enable(0) && ok;
    reg_write(CPM, CLKGR, (reg_read(CPM, CLKGR) & ~I2C_GATE) | (old_gate & I2C_GATE));
    if (!ok || (active_readback & QC5_CONTROL_REQUIRED_READBACK) != QC5_CONTROL_REQUIRED_READBACK)
        return core_refuse(record, 0x30000u + failed, value);
    active_gain = gain;
    for (unsigned int n = 0; n < 5; ++n) active_gain_readbacks[n] = actual[n + 2];
    return 1;
}

static int vic_start(int deferred)
{
    const uint32_t geometry = (QC5_SENSOR_WIDTH << 16) | QC5_SENSOR_HEIGHT;
    const struct { unsigned int reg; uint32_t value; } sequence[] = {
        {0x1e8, UINT32_MAX}, {0x1ec, UINT32_MAX},
        {0x1a4, 0x000a000a}, {0x100, (QC5_SENSOR_WIDTH * 10 + 31) / 32},
        {0x0c, 2}, {0x14, 1}, {0x04, geometry}, {0x10c, 0x2b000},
        {0x110, QC5_SENSOR_WIDTH << 16}, {0x114, 0}, {0x118, 0}, {0x11c, 0},
        {0x1ac, 0x4440}, {0x1a8, 0x4440}, {0x1b0, 0x10},
        {0x00, 2}, {0x00, 4}, {0x1a0, 0},
    };
    for (unsigned int i = 0; i < sizeof(sequence) / sizeof(sequence[0]); ++i)
        mmio(vic, sequence[i].reg, sequence[i].value);
    for (unsigned int i = 0; i < 200; ++i) {
        if (!vic[0]) {
            mmio(vic, 0x104, 0);
            mmio(vic, 0x108, 0);
            if (!deferred)
                mmio(vic, 0x00, 1);
            return 1;
        }
        delay_us(1000);
    }
    return 0;
}

static int vic_capture(volatile uint16_t *pixels, struct qc5_preview *record, int trigger_core)
{
    const unsigned int target = trigger_core ? QC5_SEQUENCE_FRAMES : 1;
    struct qc5_frame_sequence sequence = {0};
    const uint32_t geometry = (QC5_SENSOR_WIDTH << 16) | QC5_SENSOR_HEIGHT;
    const uint32_t stride = QC5_SENSOR_WIDTH * 2;
    const int words = (QC5_DMA_BANK(QC5_DMA_BANKS) - QC5_FRAME_PHYS) / 2;
    for (int i = -GUARD_WORDS; i < words; ++i)
        pixels[i] = SENTINEL;
    mmio(vic, VIC_DMA_CONTROL, 0);
    mmio(vic, 0x1f0, vic[VIC_IRQ_STATUS / 4]);
    mmio(vic, 0x1f4, vic[VIC_DMA_STATUS / 4]);
    record->intc_mask = intc[0x04 / 4];
    /* The VIC can expose completion while the SoC keeps IRQ30 masked. */
    if (record->intc_mask & BIT(30)) {
        mmio(vic, 0x1e8, ~(record->capture_flags & QC5_VIC_EVENT_POLL ? VIC_INPUT_EVENTS : VIC_INPUT_DONE));
        mmio(vic, 0x1ec, ~UINT32_C(3));
    }
    for (unsigned int i = 0; !(record->capture_flags & QC5_VIC_START_DEFERRED) && i < 300; ++i) {
        if (vic[VIC_IRQ_STATUS / 4] & BIT(0)) {
            record->capture_flags |= QC5_INPUT_SYNC;
            break;
        }
        delay_us(1000);
    }
    mmio(vic, 0x1f0, vic[VIC_IRQ_STATUS / 4]);
    mmio(vic, VIC_DMA_ENABLE, 1);
    if (vic[VIC_DMA_ENABLE / 4] & BIT(0))
        record->capture_flags |= QC5_DMA_ENABLE_PULSE;
    mmio(vic, 0x304, geometry);
    mmio(vic, 0x310, stride);
    mmio(vic, 0x314, stride);
    for (unsigned int i = 0; i < QC5_DMA_BANKS; ++i) {
        uint32_t address = QC5_DMA_BANK(i);
        mmio(vic, 0x318 + 4 * i, address);
        mmio(vic, 0x32c + 4 * i, address);
        mmio(vic, 0x340 + 4 * i, address);
    }
    record->dma_shape = vic[0x304 / 4];
    record->dma_stride = vic[0x310 / 4];
    record->dma_bank0 = vic[0x318 / 4];
    int banks_ok = record->dma_shape == geometry && record->dma_stride == stride && vic[0x314 / 4] == stride;
    for (unsigned int i = 0; i < QC5_DMA_BANKS; ++i) {
        uint32_t address = QC5_DMA_BANK(i);
        banks_ok &= vic[0x318 / 4 + i] == address && vic[0x32c / 4 + i] == address &&
                    vic[0x340 / 4 + i] == address;
    }
    record->dma_addr_before = vic[VIC_DMA_CURRENT / 4];
    if (banks_ok) {
        record->capture_flags |= QC5_BANKS_VERIFIED;
        mmio(vic, VIC_DMA_CONTROL, dma_control_for(target));
    }
    if (trigger_core)
        banks_ok = banks_ok && core_trigger(core, record, record->intc_mask);
    banks_ok = vic_start_after_dma(record, banks_ok, target) && banks_ok;
    uint32_t previous_phy = csi[0x14 / 4];
    uint32_t previous_vic_events = 0;
    struct qc5_capture_snapshot snapshot = {0};
    for (unsigned int i = 0; banks_ok && i < 2000; ++i) {
        uint32_t current_phy = csi[0x14 / 4];
        record->phy_state_seen |= current_phy;
        record->phy_state_transitions |= current_phy ^ previous_phy;
        previous_phy = current_phy;
        previous_vic_events = poll_vic_events(record, &snapshot, previous_vic_events);
        uint32_t dma_status = vic[VIC_DMA_STATUS / 4];
        record->vic_dma_status |= dma_status;
        if (target > 1) {
            sequence.polls = i + 1;
            sequence.control_at_end = vic[VIC_DMA_CONTROL / 4];
            sequence.remaining = (sequence.control_at_end >> 16) & 15u;
            int bank = frame_bank(vic[VIC_DMA_CURRENT / 4]);
            if (bank >= 0) sequence.observed_banks |= BIT(bank);
            if (dma_status & 1) {
                if (!sequence.dma_events) sequence.first_dma_poll = i + 1;
                sequence.last_dma_poll = i + 1;
                ++sequence.dma_events;
            }
            if (dma_status & 3) mmio(vic, 0x1f4, dma_status & 3);
            if (sequence_complete(sequence.control_at_end, sequence.dma_events))
                break;
        } else if (record->vic_dma_status & 3u) {
            break;
        }
        if (record->vic_status & VIC_ERRORS)
            break;
        delay_us(1000);
    }
    record->dma_addr_after = vic[VIC_DMA_CURRENT / 4];
    record->dma_control = vic[VIC_DMA_CONTROL / 4];
    collect_snapshot(&snapshot); /* Capture live registers before stopping VIC/MDMA. */
    if (record->capture_flags & QC5_CORE_TRIGGERED)
        (void)core_stop(core, record);
    mmio(vic, VIC_DMA_CONTROL, 0);
    mmio(vic, VIC_DMA_ENABLE, 0);
    mmio(vic, 0x1e8, UINT32_MAX);
    mmio(vic, 0x1ec, UINT32_MAX);
    (void)vic[VIC_DMA_ENABLE / 4];
    mmio(vic, 0x00, 4);
    record->csi_err1 = csi[0x20 / 4];
    record->csi_err2 = csi[0x24 / 4];
    record->phy_timing = phy[0x160 / 4];
    record->isp_clock = reg_read(CPM, ISP_DIV);
    delay_us(10000);
    if (trigger_core) {
        const volatile uint8_t *pool = (const volatile uint8_t *)pixels + QC5_STATS_GUARD_BASE - QC5_FRAME_PHYS;
        if (!core_pool_guard(pool, record) || (record->capture_flags & QC5_CAPTURE_VARIANT_REFUSED))
            banks_ok = 0;
    }
    for (unsigned int bank = 0; bank < QC5_DMA_BANKS; ++bank) {
        const volatile uint16_t *buffer = pixels + bank * (QC5_BANK_SPACING / 2);
        unsigned int changed = 0;
        for (unsigned int i = 0; i < FRAME_WORDS; i += 64)
            changed += buffer[i] != SENTINEL;
        if (changed)
            record->sampled_bank_mask |= BIT(bank);
        if (changed > record->sampled_words)
            record->sampled_words = changed;
    }
    if (!banks_ok || (record->vic_status & VIC_ERRORS))
        return capture_failure(record, &snapshot, CAPTURE_FRAME);
    if (!(record->vic_dma_status & 3u))
        return capture_failure(record, &snapshot, CAPTURE_TIMEOUT);
    if (target > 1) {
        sequence.stop_mask = (core_writers_off(core, record) ? 1u : 0u) |
                             (vic[VIC_DMA_CONTROL / 4] == 0 ? 2u : 0u) |
                             (vic[VIC_DMA_ENABLE / 4] == 0 ? 4u : 0u) |
                             (vic[0] == 0 ? 8u : 0u);
        sequence.configured_exposure_lines = 720;
        sequence.configured_frame_lines = 900;
        sequence.gain_index = active_gain;
        if (!sequence_complete(sequence.control_at_end, sequence.dma_events))
            return capture_failure(record, &snapshot, CAPTURE_TIMEOUT);
        return pack_sequence(pixels, record, &sequence) ? CAPTURE_OK :
               capture_failure(record, &snapshot, CAPTURE_FRAME);
    }
    int bank = frame_bank(record->dma_addr_after);
    for (unsigned int i = 0; i < QC5_DMA_BANKS; ++i) {
        /* Prefer the bank named by the hardware, then scan the other banks. */
        unsigned int candidate = ((bank < 0 ? 0u : (unsigned int)bank) + i) % QC5_DMA_BANKS;
        if (!(record->sampled_bank_mask & BIT(candidate)))
            continue;
        const volatile uint16_t *buffer = pixels + candidate * (QC5_BANK_SPACING / 2);
        if (frame_preview(buffer, record)) {
            record->selected_frame_phys = QC5_DMA_BANK(candidate);
            return CAPTURE_OK;
        }
    }
    return capture_failure(record, &snapshot, CAPTURE_FRAME);
}

static void *map_phys(int fd, uint32_t address, size_t length)
{
    return mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, address);
}

static int can_retry_capture(struct qc5_preview *record, int status)
{
    if (status != CAPTURE_TIMEOUT || !(record->vic_status & BIT(0)) ||
        record->dma_addr_after || record->sampled_bank_mask ||
        (record->capture_flags & QC5_CAPTURE_VARIANT_REFUSED))
        return 0;
    if (vic[VIC_DMA_CONTROL / 4] || vic[VIC_DMA_ENABLE / 4] || vic[0]) {
        record->capture_flags |= QC5_CAPTURE_VARIANT_REFUSED;
        record->isp_route_blocker = 0x10000u;
        return 0;
    }
    return 1;
}

static int pre_capture_failure(struct qc5_preview *record)
{
    struct qc5_capture_snapshot snapshot = {0};
    collect_snapshot(&snapshot);
    return capture_failure(record, &snapshot, CAPTURE_FRAME);
}

static int compare_isp_route(volatile uint32_t *core, volatile uint16_t *pixels,
                             struct qc5_preview *record)
{
    int status = vic_capture(pixels, record, 0);
    if (!can_retry_capture(record, status))
        return status;
    record->capture_flags |= QC5_BASELINE_TIMEOUT;
    /* Keep the entire region reserved until the watchdog reset. In particular,
     * do not restart a DMA pass if its previous stop did not stick. */
    if (vic[VIC_DMA_CONTROL / 4] || vic[VIC_DMA_ENABLE / 4] || vic[0]) {
        record->capture_flags |= QC5_ISP_ROUTE_REFUSED;
        record->isp_route_blocker = 0x10000u;
        return status;
    }
    unsigned int route = isp_input_route(core, record);
    record->capture_flags |= route;
    if (route != QC5_ISP_ROUTE_APPLIED)
        return status;
    if (!vic_start(0)) {
        record->capture_flags |= QC5_ISP_ROUTE_VIC_START_FAILED;
        return CAPTURE_FRAME;
    }

    struct qc5_preview retry = retry_record(record);
    status = vic_capture(pixels, &retry, 0);
    *record = retry;
    if (!can_retry_capture(record, status) || !isp_main_configure(core, record))
        return status;

    if (!vic_start(0)) {
        record->capture_flags |= QC5_CAPTURE_VARIANT_REFUSED;
        return CAPTURE_FRAME;
    }
    retry = retry_record(record);
    status = vic_capture(pixels, &retry, 0);
    *record = retry;
    if (!can_retry_capture(record, status))
        return status;

    if (!vic_start(1)) {
        record->capture_flags |= QC5_CAPTURE_VARIANT_REFUSED;
        return CAPTURE_FRAME;
    }
    record->capture_flags |= QC5_VIC_START_DEFERRED;
    retry = retry_record(record);
    status = vic_capture(pixels, &retry, 0);
    *record = retry;
    if (!can_retry_capture(record, status))
        return status;

    volatile uint8_t *pool = (volatile uint8_t *)pixels + QC5_STATS_GUARD_BASE - QC5_FRAME_PHYS;
    static const unsigned int gains[3] = {0, 8, 16};
    uint32_t proofs[3] = {0};
    uint8_t gain_reads[3][5] = {{0}};
    for (unsigned int stage = 0; stage < 3; ++stage) {
        retry = retry_record(record);
        *record = retry;
        if (!core_mode_for_restart(core, record) || !sensor_apply_controls(gains[stage], record) ||
            !core_prepare(core, pool, record))
            return pre_capture_failure(record);
        if (!vic_start(1)) {
            record->capture_flags |= QC5_CAPTURE_VARIANT_REFUSED;
            return pre_capture_failure(record);
        }
        retry = retry_record(record);
        status = vic_capture(pixels, &retry, 1);
        *record = retry;
        if (status != CAPTURE_OK) return status;
        if (record->csi_err1 || record->csi_err2) {
            (void)core_refuse(record, 0x50000u + stage, record->csi_err1 | record->csi_err2);
            return pre_capture_failure(record);
        }
        struct qc5_frame_sequence sequence;
        memcpy(&sequence, record->pixels, sizeof(sequence));
        proofs[stage] = gain_stage_proof(&sequence, gains[stage], active_readback);
        memcpy(gain_reads[stage], active_gain_readbacks, sizeof(gain_reads[stage]));
        if (!proofs[stage]) {
            (void)core_refuse(record, 0x40000, stage);
            return pre_capture_failure(record);
        }
    }
    struct qc5_frame_sequence sequence;
    memcpy(&sequence, record->pixels, sizeof(sequence));
    sequence.gain_proof0 = proofs[0];
    sequence.gain_proof8 = proofs[1];
    sequence.gain_proof16 = proofs[2];
    memcpy(record->pixels, &sequence, sizeof(sequence));
    /* Schema 17 retains all five actual gain-register reads per group.
     * Full-frame validation, CSI checks, and pool/AE1 guards precede this. */
    pack_gain_readbacks(record, gain_reads);
    return status;
}

static void publish_record(volatile uint8_t *ram, struct qc5_preview *record, unsigned int status)
{
    finish_record(record, status);
    volatile uint8_t *payload = ram + QC5_PREVIEW_PHYS - QC5_CAPTURE_RAM_BASE;
    for (unsigned int i = 0; i < sizeof(*record); ++i)
        payload[i] = ((const uint8_t *)record)[i];
    __asm__ volatile("sync" ::: "memory");
}

#ifndef QC5_FRAME_LIBRARY
int main(void)
{
    int fd = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);
    if (fd < 0 || map_sensor_registers(fd) != SENSOR_ID_OK)
        return CAPTURE_MAP;
    if (probe_gc1084() != SENSOR_ID_OK)
        return CAPTURE_SENSOR;

    csi = map_phys(fd, 0x10023000, 4096);
    phy = map_phys(fd, 0x10022000, 4096);
    vic = map_phys(fd, 0x133e0000, 4096);
    intc = map_phys(fd, 0x10001000, 4096);
    core = map_phys(fd, 0x13300000, 0x10000);
    volatile uint8_t *ram = map_phys(fd, QC5_CAPTURE_RAM_BASE,
                                      QC5_CAPTURE_RAM_END - QC5_CAPTURE_RAM_BASE);
    close(fd);
    if (csi == MAP_FAILED || phy == MAP_FAILED || vic == MAP_FAILED ||
        intc == MAP_FAILED || core == MAP_FAILED || ram == MAP_FAILED)
        return CAPTURE_MAP;
    struct qc5_preview record = {0};
    if (!video_clocks(&record) || !reset_isp(&record)) {
        publish_record(ram, &record, CAPTURE_CLOCK);
        return CAPTURE_CLOCK;
    }
    if (csi[0] != 0x3130322a)
        return CAPTURE_CSI;

    csi_start();
    int status;
    if (!sensor_start(&record)) {
        status = CAPTURE_SENSOR_MODE;
    } else if (!vic_start(0)) {
        status = CAPTURE_VIC;
    } else {
        volatile uint16_t *pixels = (volatile uint16_t *)(ram + QC5_FRAME_PHYS - QC5_CAPTURE_RAM_BASE);
        status = compare_isp_route(core, pixels, &record);
    }
    mmio(csi, 0x08, 0);
    mmio(csi, 0x0c, 0);
    mmio(csi, 0x10, 0);
    if (status == CAPTURE_OK || status == CAPTURE_TIMEOUT || status == CAPTURE_FRAME)
        publish_record(ram, &record, status);
    printf("QC5_FRAME_RESULT %d vic=%08x dma=%08x csi=%08x/%08x words=%u\n",
           status, record.vic_status, record.vic_dma_status, record.csi_err1, record.csi_err2,
           record.changed_words);
    return status;
}
#endif /* QC5_FRAME_LIBRARY */
#endif
