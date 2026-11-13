#ifndef QC5_MSCA_RECORD_H
#define QC5_MSCA_RECORD_H
#include <stdint.h>
#define QC5_NV12_MAGIC UINT32_C(0x3733564e) /* NV37 */
#define QC5_NV12_FOURCC UINT32_C(0x3231564e)
#ifndef QC5_NV12_TARGET
#define QC5_NV12_TARGET 2u
#endif
#ifdef QC5_STREAM_RUNTIME
_Static_assert(QC5_NV12_TARGET==632u,"Only the bounded continuous-producer trial");
#define QC5_NV12_POLLS 35000u
#else
_Static_assert(QC5_NV12_TARGET == 2u || QC5_NV12_TARGET == 60u || QC5_NV12_TARGET == 120u || QC5_NV12_TARGET == 128u || QC5_NV12_TARGET == 248u,
               "Bounded pair/ring/auto-exposure trial only");
#ifdef QC5_NV12_AE
#define QC5_NV12_POLLS 15000u
#else
#define QC5_NV12_POLLS 5000u
#endif
#endif
#ifdef QC5_MDNS_GATE
#define QC5_NV12_BYPASS UINT32_C(0xb57468c9) /* This path also selects BCSH (bit 16); load its native coefficients before enabling it. */
#define QC5_NV12_RGB_HEIGHT 5u
#else
#ifdef QC5_NV12_WB
#ifdef QC5_NV12_SPATIAL
#ifdef QC5_NV12_DETAIL
#define QC5_NV12_BYPASS UINT32_C(0xb57568c9) /* Also enable initialized GIB, DPC and YDNS. */
#else
#define QC5_NV12_BYPASS UINT32_C(0xb57768f9) /* AWB and initialized SDNS (bit 15 clear). */
#endif
#else
#define QC5_NV12_BYPASS UINT32_C(0xb577e8f9) /* Also enable AWB gain stage, bit 2. */
#endif
#define QC5_NV12_RGB_HEIGHT 5u
#else
#define QC5_NV12_BYPASS UINT32_C(0xb577e8fd) /* Enable DMSC/CCM/Gamma/CSC. */
#define QC5_NV12_RGB_HEIGHT 6u
#endif
#endif
enum msca_field {
    M_MAGIC, M_VERSION, M_FORMAT, M_TARGET, M_COMPLETED, M_POLLS, M_FIRST, M_LAST,
    M_STOP, M_GUARDS, M_Y_BYTES, M_UV_BYTES, M_STRIDE, M_FRAME_BYTES, M_DESC_BYTES,
    M_PLAN_HASH, M_COLOR_WRITES, M_LUT_READS, M_LUT_HASH, M_CSC_MATCH, M_BYPASS,
    M_ISP_IRQ, M_VIC_IRQ, M_STAGE, M_FAULT_ADDRESS, M_FAULT_VALUE, M_LATCH_POLLS,
    M_SHAPE_MASK, M_Y_DONE, M_UV_DONE, M_GAIN, M_EXPOSURE, M_SENSOR_MASK,
    M_INITIAL_LUT_BANK, M_PLAN_WRITES, M_POOL_HASH, M_INTC_MASK, M_CORE_RUN,
    M_OUTPUT_ENABLE, M_VIC_RUN, M_FIELD_COUNT,
};
/* Payload v4 reuses fields redundant with the pinned plan/full RAM guard. */
#define M_AE_INITIAL_GAIN M_INITIAL_LUT_BANK
#define M_AE_UPDATES M_PLAN_WRITES
#define M_AE_SAMPLES M_POOL_HASH
struct msca_frame {
    uint32_t y, uv, end, y_hash, uv_hash, y_min, y_max, y_mean, u_mean, v_mean;
};
struct msca_record {
    uint32_t meta[M_FIELD_COUNT];
    union {
        struct {
            struct msca_frame frames[2];
#ifdef QC5_NV12_WB
            struct { uint32_t values[4],plan_hash,read_count; } wb;
#endif
            uint8_t rgb[8 * QC5_NV12_RGB_HEIGHT * 3];
        } success;
        struct { uint32_t address, value; } failure[28];
    } data;
};
static const unsigned int msca_snapshot_offsets[28] = {4,8,12,16,28,48,0xb0,0xb4,0x33c,0x800,0x804,
    0xd010,0xd040,0xd04c,0xd050,0xd090,0xd094,0xd100,0xd104,0xd108,0xd10c,
    0xd110,0xd114,0xd128,0xd140,0xd160,0xe100,0xe00c};
_Static_assert(M_FIELD_COUNT == 40, "MSCA metadata count");
_Static_assert(sizeof(struct msca_record) == 384, "NV12 evidence fits checkpoint payload");
#endif
