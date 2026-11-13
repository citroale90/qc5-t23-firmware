#ifndef QC5_COLOR_PROFILE_H
#define QC5_COLOR_PROFILE_H

/* Preparatory colour load only: keep the established RAW bypass and DMA path. */
#define QC5_COLOR_MAP_BYTES 0x81000u
#define QC5_COLOR_MAGIC UINT32_C(0x37335043) /* CP37 */
struct color_row { uint32_t offset, value; };
#include "gc1084-color.private.h"

struct color_evidence {
    uint32_t magic, version, writes, write_hash, reads, read_hash;
    uint32_t csc[6]; /* Active matrix observed after frame processing. */
};
_Static_assert(sizeof(struct color_evidence) == 48, "Colour evidence fits stream preview");
static struct color_evidence active_color;

#ifdef QC5_FRAME_TEST
uint32_t color_read(volatile uint32_t *isp, unsigned int offset);
void color_write(volatile uint32_t *isp, unsigned int offset, uint32_t value);
#else
static uint32_t color_read(volatile uint32_t *isp, unsigned int offset) { return isp[offset / 4]; }
static void color_write(volatile uint32_t *isp, unsigned int offset, uint32_t value) { mmio(isp, offset, value); }
#endif

static uint32_t color_hash_pair(uint32_t hash, uint32_t offset, uint32_t value)
{
    for (unsigned int n = 0; n < 8; ++n) {
        hash = (hash ^ ((n < 4 ? offset : value) >> (8 * (n % 4)) & 255u)) * 16777619u;
    }
    return hash;
}

static unsigned int color_page(unsigned int offset)
{
    static const unsigned int pages[] = {0x1000, 0x4000, 0x5000, 0x6000, 0xc000,
                                          0x40000, 0x48000, 0x50000, 0x80000};
    for (unsigned int n = 0; n < 9; ++n) if ((offset & ~4095u) == pages[n]) return 1u << n;
    return 0;
}

static int color_lut(unsigned int offset)
{
    return (offset >= 0x40000 && offset < 0x40200) || (offset >= 0x48000 && offset < 0x48200) ||
           (offset >= 0x50000 && offset < 0x50200) || (offset >= 0x80000 && offset < 0x80180);
}

static int color_readback(unsigned int offset)
{
    return color_lut(offset);
}

static int color_allowed(unsigned int offset, uint32_t value)
{
    if (offset % 4 || offset >= QC5_COLOR_MAP_BYTES) return 0;
    if (color_lut(offset)) return 1;
    if (offset >= 0x4800 && offset <= 0x4a5c) return 1;
    if (offset >= 0x5000 && offset <= 0x5020) return offset != 0x5000 || value == 1;
    switch (offset) {
    case 0x1030: case 0x1034: case 0x1038: case 0x103c:
    case 0x1064: case 0x1068: case 0x106c: return 1;
    case 0x1060: return value == 0x100; /* Bit 0 writer remains off. */
    case 0x1070: return value == 1; /* Native GIB parameter latch, not a readback. */
    case 0x6000: return value == 0x1f;
    case 0x6004: case 0x6010: case 0x6014: case 0x6018: case 0x6020: case 0x6030:
    case 0xc000: case 0xc010: case 0xc040: case 0xc044: case 0xc050: case 0xc054: return 1;
    default: return 0;
    }
}

static int color_idle(volatile uint32_t *isp, struct qc5_preview *record)
{
    if (!(record->capture_flags & QC5_ISP_MAIN_CONFIGURED) ||
        (record->capture_flags & (QC5_CORE_TRIGGERED | QC5_CAPTURE_VARIANT_REFUSED)))
        return core_refuse(record, 0x10, color_read(isp, 0x10));
    if (frame_read_vic(0) || frame_read_vic(VIC_DMA_CONTROL) || frame_read_vic(VIC_DMA_ENABLE))
        return core_refuse(record, 0x10300, frame_read_vic(VIC_DMA_CONTROL));
    static const unsigned int stopped[] = {0x800, 0x804, 0xd040, 0xb000, 0xa04c,
                                            0xa84c, 0xb04c, 0x4490, 0x5b80, 0xb8b8};
    for (unsigned int n = 0; n < sizeof(stopped) / sizeof(stopped[0]); ++n)
        if (color_read(isp, stopped[n])) return core_refuse(record, stopped[n], color_read(isp, stopped[n]));
    if (color_read(isp, 0x08) != 1 || color_read(isp, 0x0c) != 0xb577fffd ||
        !isp_main_ready(color_read(isp, 0x10)) || (color_read(isp, 0x1060) & 1u) ||
        (color_read(isp, 0x7804) & 1u))
        return core_refuse(record, 0x0c, color_read(isp, 0x0c));
    return 1;
}

static int color_apply_plan(volatile uint32_t *isp, struct qc5_preview *record,
                             const struct color_row *plan, unsigned int count)
{
    memset(&active_color, 0, sizeof(active_color));
    if (!plan || count != QC5_COLOR_WRITES) return core_refuse(record, 0x60000, count);
    uint32_t hash = 2166136261u, pages = 0;
    for (unsigned int n = 0; n < count; ++n) {
        if (!color_allowed(plan[n].offset, plan[n].value)) return core_refuse(record, plan[n].offset, plan[n].value);
        hash = color_hash_pair(hash, plan[n].offset, plan[n].value);
        pages |= color_page(plan[n].offset);
    }
    /* Check the entire pinned plan before issuing any MMIO write. */
    if (hash != QC5_COLOR_WRITE_HASH || pages != 511u) return core_refuse(record, 0x60004, hash);
    if (!color_idle(isp, record)) return 0;
    for (unsigned int n = 0; n < count; ++n) color_write(isp, plan[n].offset, plan[n].value);
    hash = 2166136261u;
    for (unsigned int n = 0; n < count; ++n) {
        if (!color_readback(plan[n].offset)) continue;
        uint32_t value = color_read(isp, plan[n].offset);
        if (value != plan[n].value) return core_refuse(record, plan[n].offset, value);
        hash = color_hash_pair(hash, plan[n].offset, value);
        ++active_color.reads;
    }
    if (active_color.reads != QC5_COLOR_READS || hash != QC5_COLOR_READ_HASH)
        return core_refuse(record, 0x60008, hash);
    if (!color_idle(isp, record)) return 0;
    active_color = (struct color_evidence){.magic = QC5_COLOR_MAGIC, .version = 2,
        .writes = count, .write_hash = QC5_COLOR_WRITE_HASH, .reads = QC5_COLOR_READS, .read_hash = hash};
    return 1;
}

static int color_csc_mode(void)
{
    for (unsigned int mode = 0; mode < 2; ++mode) {
        unsigned int matched = 0;
        for (unsigned int n = 0; n < 6; ++n) matched += active_color.csc[n] == color_csc_reference[mode][n];
        if (matched == 6) return (int)mode;
    }
    return -1;
}

static int color_csc_valid(void)
{
    return active_color.csc[0] == 0 && active_color.csc[1] && active_color.csc[2] &&
        active_color.csc[3] && !((active_color.csc[1] | active_color.csc[2] |
        active_color.csc[3]) & 0xc0000000u) && !(active_color.csc[4] & 0xffff0000u) &&
        (active_color.csc[5] & 255u) <= ((active_color.csc[5] >> 8) & 255u) &&
        ((active_color.csc[5] >> 16) & 255u) <= (active_color.csc[5] >> 24);
}

static int color_finish(volatile uint32_t *isp, struct qc5_preview *record)
{
    if (active_color.magic != QC5_COLOR_MAGIC || active_color.version != 2)
        return core_refuse(record, 0x60000, active_color.magic);
    for (unsigned int n = 0; n < 6; ++n) active_color.csc[n] = color_read(isp, color_csc_offsets[n]);
    /* Record the active values after frames; do not claim they committed
     * merely because the writes were issued while the pipeline was idle. */
    if (!color_csc_valid()) return core_refuse(record, 0x6010, active_color.csc[1]);
    return 1;
}

static int color_pack(uint8_t *preview)
{
    if (active_color.magic != QC5_COLOR_MAGIC || active_color.version != 2 ||
        active_color.writes != QC5_COLOR_WRITES || active_color.write_hash != QC5_COLOR_WRITE_HASH ||
        active_color.reads != QC5_COLOR_READS || active_color.read_hash != QC5_COLOR_READ_HASH ||
        !color_csc_valid()) return 0;
    uint8_t reduced[12 * 8];
    for (unsigned int y = 0; y < 8; ++y)
        for (unsigned int x = 0; x < 12; ++x) reduced[y * 12 + x] = preview[(y * 9 / 8) * 16 + x * 16 / 12];
    memcpy(preview, &active_color, sizeof(active_color));
    memcpy(preview + sizeof(active_color), reduced, sizeof(reduced));
    return 1;
}
#endif
