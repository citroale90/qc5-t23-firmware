#ifndef QC5_FRAME_LAYOUT_H
#define QC5_FRAME_LAYOUT_H

#include <stdint.h>

#define QC5_CAPTURE_RAM_BASE 0x01400000u
#define QC5_CAPTURE_RAM_END  0x01d00000u
#define QC5_PREVIEW_PHYS     (QC5_CAPTURE_RAM_END - 4096u)
#define QC5_FRAME_PHYS       (QC5_CAPTURE_RAM_BASE + 4096u)
#define QC5_PREVIEW_MAGIC    UINT32_C(0x37334652) /* RF37 */
#ifndef QC5_PREVIEW_VERSION
#define QC5_PREVIEW_VERSION  18u
#endif
#define QC5_PREVIEW_WIDTH    24u
#define QC5_PREVIEW_HEIGHT   16u
#define QC5_SENSOR_WIDTH     1280u
#define QC5_SENSOR_HEIGHT    720u
#define QC5_FRAME_BYTES      (2u * QC5_SENSOR_WIDTH * QC5_SENSOR_HEIGHT)
#define QC5_DMA_BANKS        5u
#define QC5_BANK_SPACING     (QC5_FRAME_BYTES + 4096u)
#define QC5_DMA_BANK(n)      (QC5_FRAME_PHYS + (n) * QC5_BANK_SPACING)

enum qc5_capture_flags {
    QC5_INPUT_SYNC = 1u << 0,
    QC5_BANKS_VERIFIED = 1u << 1,
    QC5_VPU_GATE_OPEN_BEFORE = 1u << 2,
    QC5_VPU_GATE_OPEN_AFTER = 1u << 3,
    QC5_DMA_ENABLE_PULSE = 1u << 4,
    QC5_BASELINE_TIMEOUT = 1u << 5,
    QC5_ISP_ROUTE_APPLIED = 1u << 6,
    QC5_ISP_ROUTE_REFUSED = 1u << 7,
    QC5_ISP_ROUTE_READBACK_FAILED = 1u << 8,
    QC5_ISP_ROUTE_VIC_START_FAILED = 1u << 9,
    QC5_ISP_STATS_STOP_WRITTEN = 1u << 10,
    QC5_ISP_RESET_REQUESTED = 1u << 11,
    QC5_ISP_RESET_ACK_SEEN = 1u << 12,
    QC5_ISP_RESET_PULSED = 1u << 13,
    QC5_ISP_RESET_VERIFIED = 1u << 14,
    QC5_ISP_RESET_TIMEOUT = 1u << 15,
    QC5_VIC_EVENT_POLL = 1u << 22,
    QC5_VIC_EVENT_ACK_WRITTEN = 1u << 23,
    QC5_VIC_EVENT_ACK_VERIFIED = 1u << 24,
    QC5_IVDC_GATE_OPEN_BEFORE = 1u << 25,
    QC5_IVDC_GATE_OPEN_AFTER = 1u << 26,
    QC5_ISP_MAIN_CONFIGURED = 1u << 27,
    QC5_VIC_START_DEFERRED = 1u << 28,
    QC5_VIC_STARTED_AFTER_DMA = 1u << 29,
    QC5_CAPTURE_VARIANT_REFUSED = 1u << 30,
    QC5_CORE_TRIGGERED = 1u << 31,
};

#define QC5_STATS_GUARD_BASE QC5_DMA_BANK(QC5_DMA_BANKS)
#define QC5_STATS_BASE (QC5_STATS_GUARD_BASE + 4096u)
#define QC5_STATS_BYTES 0x1c000u
#define QC5_STATS_END (QC5_STATS_BASE + QC5_STATS_BYTES)
_Static_assert(QC5_STATS_END + 4096u <= QC5_PREVIEW_PHYS, "Statistics rings fit below the SD record");

/* Schema 7 packs SRBC bits 20..22 into otherwise unused flag bits. */
#define QC5_SRBC_BEFORE_SHIFT 16u
#define QC5_SRBC_AFTER_SHIFT  19u

#define QC5_SNAPSHOT_MAGIC UINT32_C(0x37334752) /* RG37, only for failed captures */
static const uint32_t qc5_snapshot_addresses[] = {
    0x10000000, 0x10000020, 0x10000024, 0x10000028, 0x10000030, 0x10000080, 0x100000c4,
    0x10023004, 0x10023008, 0x1002300c, 0x10023010, 0x10023014, 0x10023020, 0x10023024,
    0x133e0000, 0x133e0004, 0x133e000c, 0x133e0010, 0x133e0014, 0x133e0018,
    0x133e0100, 0x133e010c, 0x133e0110, 0x133e01a0, 0x133e01a4, 0x133e01a8,
    0x133e01ac, 0x133e01b0, 0x133e01e8, 0x133e01ec, 0x133e0300, 0x133e0308, 0x133e0380,
    0x13300004, 0x13300008, 0x1330000c, 0x13300010, 0x1330001c, 0x1330002c,
    0x13300030, 0x13300800, 0x13300804, 0x1330d040, 0x10022160,
};

struct qc5_capture_snapshot {
    uint32_t magic, version, register_count, polls;
    uint32_t input_done_events, input_done_ack_verified, input_start_events, input_start_ack_verified;
    struct { uint32_t address, value; } registers[44];
};
_Static_assert(sizeof(qc5_snapshot_addresses) / sizeof(uint32_t) == 44, "Snapshot register count");
_Static_assert(sizeof(struct qc5_capture_snapshot) == 384, "Failure snapshot replaces only the preview");

#define QC5_SEQUENCE_MAGIC UINT32_C(0x37335153) /* SQ37 */
#define QC5_SEQUENCE_FRAMES 5u
struct qc5_sequence_entry {
    uint32_t physical, frame_hash, changed_words, packing;
    uint32_t minimum, maximum, mean, bank;
};
struct qc5_frame_sequence {
    uint32_t magic, version, target, validated;
    uint32_t polls, dma_events, observed_banks, remaining;
    uint32_t first_dma_poll, last_dma_poll, control_at_end, stop_mask;
    uint32_t configured_exposure_lines, configured_frame_lines, gain_index, gain_proof0;
    uint32_t preview_width, preview_height, gain_proof8, gain_proof16;
    struct qc5_sequence_entry frames[QC5_SEQUENCE_FRAMES];
    uint8_t pixels[16 * 9];
};
_Static_assert(sizeof(struct qc5_frame_sequence) == 384, "Burst evidence fits preview payload");
_Static_assert(QC5_SEQUENCE_FRAMES == QC5_DMA_BANKS, "One frame per reserved bank, no ring wrap");

#define QC5_STREAM_MAGIC UINT32_C(0x37335453) /* ST37 */
#define QC5_STREAM_TARGET 60u
#define QC5_STREAM_POLLS 8000u
struct qc5_stream_record {
    uint32_t magic, version, target, completed;
    uint32_t polls, pointer_errors, ring_wraps, frame_lines;
    uint32_t elapsed_us, first_dma_poll, last_dma_poll, control_at_end;
    uint32_t stop_mask, exposure_lines, gain_index, readback_mask;
    uint32_t gain_reads0, gain_reads1, direct_setup, validated_final;
    struct qc5_sequence_entry frames[QC5_DMA_BANKS];
    uint8_t pixels[16 * 9];
};
_Static_assert(sizeof(struct qc5_stream_record) == 384, "Stream evidence fits preview payload");

struct qc5_preview {
    uint32_t magic, version, width, height, frame_bytes, frame_hash;
    uint32_t vic_status, vic_dma_status, csi_err1, csi_err2;
    uint32_t changed_words, packing;
    uint8_t pixels[QC5_PREVIEW_WIDTH * QC5_PREVIEW_HEIGHT];
    uint32_t status, phy_state_seen, phy_state_transitions, phy_timing;
    uint32_t dma_control, isp_route_blocker, intc_mask, isp_clock, sampled_words;
    uint32_t dma_addr_before, dma_addr_after, dma_bank0, vpu_clock_before;
    uint32_t dma_shape, dma_stride, vpu_clock_after, capture_flags;
    uint32_t sampled_bank_mask, selected_frame_phys;
    uint32_t record_hash;
};

_Static_assert(sizeof(struct qc5_preview) == 512, "Preview occupies one sector");

static uint32_t qc5_frame_hash(const volatile uint8_t *data, unsigned int count)
{
    uint32_t hash = UINT32_C(2166136261);
    for (unsigned int i = 0; i < count; ++i)
        hash = (hash ^ data[i]) * UINT32_C(16777619);
    return hash;
}

#endif
