/* One bounded NV12 output trial, no encoder and no flash access. */
#ifndef QC5_PREVIEW_VERSION
#define QC5_PREVIEW_VERSION 20u
#endif
#define QC5_FRAME_LIBRARY
#include "capture-frame.c"
#include "color-profile.h"
#include "qc5-nv12-layout.h"
#include "msca-record.h"
#ifdef QC5_NV12_AUDIT
#include "nv12-audit.h"
#endif
#include "gc1084-msca.private.h"
#ifndef QC5_NV12_INITIAL_GAIN
#define QC5_NV12_INITIAL_GAIN 0u
#endif
#ifndef QC5_AE_TARGET
#define QC5_AE_TARGET 96u
#endif
#ifdef QC5_NV12_AE
#include "auto-exposure.h"
#ifdef QC5_NV12_WB
#include "white-balance.h"
#ifdef QC5_NV12_SPATIAL
#define QC5_NV12_PAYLOAD_VERSION 6u
#else
#define QC5_NV12_PAYLOAD_VERSION 5u
#endif
#else
#define QC5_NV12_PAYLOAD_VERSION 4u
#endif
#else
#define QC5_NV12_PAYLOAD_VERSION (QC5_NV12_TARGET == 2u ? 2u : 3u)
#endif

#include "msca-state.h"
#ifdef QC5_NV12_ENCODER_HOOK
static int encoder_prepare(volatile uint8_t *,struct qc5_preview *);
#ifndef QC5_STREAM_RUNTIME
static int encoder_completed(volatile uint8_t *,struct qc5_preview *,unsigned int);
#endif
static int encoder_owns_address(uint32_t);
#ifdef QC5_CAPTURE_SERVICE
static int encoder_service_capture(volatile uint8_t *,struct qc5_preview *,struct msca_record *);
#endif
#ifdef QC5_STREAM_RUNTIME
static int encoder_runtime_frame(volatile uint8_t *,struct qc5_preview *,struct msca_record *,struct msca_state *);
static int encoder_runtime_stopped(struct qc5_preview *,unsigned);
#endif
#endif
#ifdef QC5_LIVE_PROGRESS
static void live_progress(const char *,uint64_t);
#define NV12_PROGRESS(stage) live_progress(stage,state.completed)
#else
#define NV12_PROGRESS(stage) ((void)0)
#endif
#ifdef QC5_FRAME_TEST
uint32_t msca_read(unsigned int offset);
void msca_write(unsigned int offset, uint32_t value);
void msca_vic_write(unsigned int offset, uint32_t value);
#else
static uint32_t msca_read(unsigned int offset) { return core[offset / 4]; }
static void msca_write(unsigned int offset, uint32_t value) { mmio(core, offset, value); }
static void msca_vic_write(unsigned int offset, uint32_t value) { mmio(vic, offset, value); }
#endif
#ifdef QC5_NV12_SPATIAL
#include "spatial-capture.h"
#endif
#ifdef QC5_DYNAMIC_AWB
#if !defined(QC5_NV12_AE) || !defined(QC5_NV12_WB)
#error Dynamic AWB requires the existing exposure and white-balance path
#endif
#include "awb-capture.h"
static struct qc5_awb_capture active_awb;
static int ae_time_us(struct qc5_preview *,uint64_t *);
static uint32_t awb_hw_read(void *opaque,uint32_t offset){(void)opaque;return msca_read(offset);}
static int awb_hw_write(void *opaque,uint32_t offset,uint32_t value){(void)opaque;msca_write(offset,value);return 1;}
static int awb_hw_time(void *opaque,uint64_t *us){return ae_time_us(opaque,us);}
static const uint32_t *wb_active_expected(void){return active_awb.expected;}
static uint32_t wb_active_hash(void){return active_awb.ready?active_awb.algorithm.profile->checksum:0;}
static int awb_failed(struct qc5_preview *r)
{return core_refuse(r,active_awb.fault_reg?active_awb.fault_reg:0x75014,active_awb.fault_value);}
#ifdef QC5_DYNAMIC_CCM
#include "ccm-capture.h"
static struct qc5_ccm_capture active_ccm;
static int ccm_failed(struct qc5_preview *r)
{return core_refuse(r,active_ccm.fault_reg?active_ccm.fault_reg:0x76008,active_ccm.fault_value);}
#if defined(QC5_IMAGE_CONTINUOUS) && defined(QC5_DYNAMIC_BCSH)
#include "image-colour-events.h"
static int image_colour_event(struct qc5_preview *r,uint32_t ct,uint32_t ev)
{
 if(!bcsh_loaded_for_output())return core_refuse(r,0x7b020,0);
 if(qc5_image_colour_event(&active_ccm,&active_bcsh_dynamic,ct,ev)){bcsh_dynamic_log(ct,ev);return 1;}
 if(active_bcsh_dynamic.fault_offset)return core_refuse(r,active_bcsh_dynamic.fault_offset,active_bcsh_dynamic.fault_value);
 return ccm_failed(r);
}
#endif
#endif
#endif

static struct msca_record msca_initial(void)
{
    struct msca_record data = {0};
    uint32_t *m = data.meta;
    m[M_MAGIC] = QC5_NV12_MAGIC; m[M_VERSION] = QC5_NV12_PAYLOAD_VERSION; m[M_FORMAT] = QC5_NV12_FOURCC;
    m[M_TARGET] = QC5_NV12_TARGET; m[M_Y_BYTES] = 921600; m[M_UV_BYTES] = 460800; m[M_STRIDE] = 1280;
    m[M_FRAME_BYTES] = 1382400; m[M_DESC_BYTES] = 24; m[M_PLAN_HASH] = QC5_MSCA_PLAN_HASH;
    m[M_BYPASS] = QC5_NV12_BYPASS; m[M_GAIN] = QC5_NV12_INITIAL_GAIN; m[M_EXPOSURE] = 720;
#ifdef QC5_NV12_AE
    m[M_AE_INITIAL_GAIN] = QC5_NV12_INITIAL_GAIN;
#endif
    return data;
}

static int msca_idle(struct qc5_preview *record)
{
    static const unsigned int off[] = {0x800, 0x804, 0xd040, 0xb000, 0xa04c, 0xa84c,
                                        0xb04c, 0x4490, 0x5b80, 0xb8b8};
    if (!(record->capture_flags & QC5_ISP_MAIN_CONFIGURED) ||
        (record->capture_flags & (QC5_CORE_TRIGGERED | QC5_CAPTURE_VARIANT_REFUSED)) ||
        (record->intc_mask & (BIT(29) | BIT(30))) != (BIT(29) | BIT(30)))
        return core_refuse(record, 0x10001004, record->intc_mask);
    for (unsigned int n = 0; n < sizeof(off) / sizeof(off[0]); ++n)
        if (msca_read(off[n])) return core_refuse(record, off[n], msca_read(off[n]));
    if (frame_read_vic(0) || frame_read_vic(VIC_DMA_CONTROL) || frame_read_vic(VIC_DMA_ENABLE))
        return core_refuse(record, 0x10000, frame_read_vic(0));
    if ((msca_read(0x1060) & 1) || (msca_read(0x7804) & 1) ||
        msca_read(8) != 1 || !isp_main_ready(msca_read(0x10)) || msca_read(0x0c) != 0xb577fffd)
        return core_refuse(record, 0x0c, msca_read(0x0c));
    return 1;
}

static int msca_allowed(unsigned int offset)
{
    static const unsigned int allowed[] = {0xd010,0xd040,0xd04c,0xd050,0xd090,0xd094,
        0xd100,0xd104,0xd108,0xd10c,0xd110,0xd114,0xd128,0xd140,0xd160,0xd170,0xd174,
        0xe000,0xe004,0xe100};
    for (unsigned int n = 0; n < sizeof(allowed) / sizeof(allowed[0]); ++n)
        if (offset == allowed[n]) return 1;
    return 0;
}

static int msca_trigger(struct qc5_preview *record, const struct msca_state *state);

/* Geometry registers can still report their reset values before frame input.
 * Never give the DMA an address until every active geometry word is correct. */
static const struct color_row msca_shape[] = {
    {0xd090,0},{0xd094,0x050002d0},{0xd100,0},{0xd104,0x050002d0},
    {0xd108,0},{0xd10c,0x050002d0},{0xd110,0x4000},{0xd114,0x4000},
    {0xd140,1280},{0xd160,1280}
};
#define MSCA_SHAPE_MASK ((1u << (sizeof(msca_shape)/sizeof(msca_shape[0]))) - 1u)

static unsigned int msca_shape_mask(void)
{
    unsigned int mask = 0;
    for (unsigned int n = 0; n < sizeof(msca_shape)/sizeof(msca_shape[0]); ++n)
        if (msca_read(msca_shape[n].offset) == msca_shape[n].value) mask |= BIT(n);
    return mask;
}

/* The exact OEM ISR tests done-FIFO bit 0: one is empty. Input queues
 * are cleared by the native four-write sequence, with no destinations given. */
static int msca_fifos_empty(struct qc5_preview *record)
{
    static const unsigned int status[] = {0xd13c,0xd15c};
    for (unsigned int n = 0; n < sizeof(status)/sizeof(status[0]); ++n)
        if (!(msca_read(status[n]) & 1))
            return core_refuse(record,status[n],msca_read(status[n]));
    return 1;
}

static int msca_program(struct qc5_preview *record, struct msca_record *data,
                         struct msca_state *state, const struct color_row *plan, unsigned int count)
{
    if (!msca_policy_valid(state)||!plan || count != QC5_MSCA_PLAN_COUNT || state->queued || state->completed||state->last_poll||(state->service&&state->target))
        return core_refuse(record, 0x70000, count);
    uint32_t hash = 2166136261u;
    for (unsigned int n = 0; n < count; ++n) {
        if (!msca_allowed(plan[n].offset)) return core_refuse(record, plan[n].offset, plan[n].value);
        hash = color_hash_pair(hash, plan[n].offset, plan[n].value);
    }
    if (hash != QC5_MSCA_PLAN_HASH) return core_refuse(record, 0x70004, hash);
    if (!msca_idle(record)) return 0;
    for (unsigned int n = 0; n < 2; ++n)
        if (!qc5_nv12_plan(n, 1280, 720, &state->planes[n])) return core_refuse(record, 0x70008, n);
    if (msca_read(0xe100)) return core_refuse(record,0xe100,msca_read(0xe100));
    if (msca_read(0xe00c)) return core_refuse(record,0xe00c,msca_read(0xe00c));
    /* Native code preserves the word and ORs in these three UV-enable bits.
     * Every initial value 0..7 therefore produces this plan's exact 7. */
    if (msca_read(0xd050)&~7u) return core_refuse(record,0xd050,msca_read(0xd050));
#ifndef QC5_NV12_AE
    data->meta[M_INITIAL_LUT_BANK] = msca_read(0xe100);
#endif
    /* Native clear procedure, only with all writers/input stopped. */
    msca_write(0xb0, 0);
    msca_write(0xd144, 1); msca_write(0xd164, 1);
    msca_write(0xd148, 1); msca_write(0xd168, 1);
    if (!msca_fifos_empty(record)) return 0;
    msca_write(0x33c, 0x20230219); /* Native tisp_msca_init. */
    data->meta[M_STAGE] = 3;
    /* Native tisp_init starts IPC before the later channel-start operation.
     * This diagnostic withholds ALL output addresses during configuration. */
    if (!msca_trigger(record,state)) return 0;
    for (unsigned int n = 0; n < count; ++n) msca_write(plan[n].offset, plan[n].value);
#ifndef QC5_NV12_AE
    data->meta[M_PLAN_WRITES] = count + 1;
#endif
    /* Native plan enables ch0; no destinations exist yet and VIC is stopped.
     * Disable it before providing input, then wait for live geometry. */
    msca_write(0xd040,0);
    if (msca_read(0xd040)) return core_refuse(record,0xd040,msca_read(0xd040));
    if (!msca_fifos_empty(record)) return 0;
    data->meta[M_STAGE] = 4;
    return 1;
}

static int msca_trigger(struct qc5_preview *record, const struct msca_state *state)
{
#ifdef QC5_BCSH
    if(!bcsh_loaded_for_output())return core_refuse(record,0x7b00c,0);
#endif
    if (state->queued || state->completed || msca_read(0xd040) != 0 ||
        msca_read(0x800) || msca_read(0x804) || frame_read_vic(0) ||
        frame_read_vic(VIC_DMA_CONTROL) || frame_read_vic(VIC_DMA_ENABLE) ||
        (record->capture_flags&(QC5_CORE_TRIGGERED|QC5_BANKS_VERIFIED|QC5_CAPTURE_VARIANT_REFUSED)))
        return core_refuse(record, 0xd040, msca_read(0xd040));
    if ((record->intc_mask & (BIT(29)|BIT(30))) != (BIT(29)|BIT(30)) ||
        msca_read(0x0c) != 0xb577fffd || !isp_main_ready(msca_read(0x10)))
        return core_refuse(record, 0x0c, msca_read(0x0c));
#ifdef QC5_NV12_SPATIAL
    if (!active_spatial.ready || active_spatial.bank ||
        active_spatial.gain_index != QC5_NV12_INITIAL_GAIN)
        return core_refuse(record, 0x7300c, active_spatial.gain_index);
#endif
#ifdef QC5_NV12_DETAIL
    if(!active_detail.ready||active_detail.bank||active_detail.gain!=QC5_NV12_INITIAL_GAIN||active_detail.writes!=67||
        active_color.magic!=QC5_COLOR_MAGIC||active_color.writes!=QC5_COLOR_WRITES)
        return core_refuse(record,0x7900c,active_detail.gain);
#endif
    for (unsigned int n = 0; n < 2; ++n) {
        struct qc5_nv12_bank canonical;
        if (!qc5_nv12_plan(n,1280,720,&canonical) ||
            memcmp(&canonical,&state->planes[n],sizeof(canonical)))
            return core_refuse(record,0x70010,n);
    }
    for (unsigned int n = 0; n < sizeof(core_stat_controls)/sizeof(core_stat_controls[0]); ++n)
        if (msca_read(core_stat_controls[n])) return core_refuse(record,core_stat_controls[n],msca_read(core_stat_controls[n]));
    if ((msca_read(0x1060)&1) || (msca_read(0x7804)&1)) return core_refuse(record,0x7804,msca_read(0x7804));
    msca_write(0x0c, QC5_NV12_BYPASS);
    if (msca_read(0x0c) != QC5_NV12_BYPASS) return core_refuse(record, 0x0c, msca_read(0x0c));
    msca_write(0xb8, msca_read(0xb4));
    msca_write(0xb0, UINT32_MAX); /* Poll ISP status; SoC IRQs stay masked. */
    msca_write(0x804, 0x1c);
    if (msca_read(0x804) != 0x1c) return core_refuse(record, 0x804, msca_read(0x804));
#ifdef QC5_MDNS_TEMPORAL
    if(!mdns_temporal_start(record))return 0;
#endif
    msca_write(0x800, 1);
    record->capture_flags |= QC5_CORE_TRIGGERED;
    return 1;
}

static int msca_begin_output(struct qc5_preview *record, struct msca_record *data,
                              struct msca_state *state, unsigned int poll)
{
    if (!msca_policy_valid(state)||!poll || poll >= QC5_NV12_POLLS || state->queued || state->completed ||
        data->meta[M_LATCH_POLLS] || data->meta[M_SHAPE_MASK] ||
        !(record->capture_flags&QC5_CORE_TRIGGERED) ||
        (record->capture_flags&(QC5_BANKS_VERIFIED|QC5_CAPTURE_VARIANT_REFUSED)) ||
        !(record->vic_status&1) || msca_read(0xd040) ||
        frame_read_vic(VIC_DMA_CONTROL) || frame_read_vic(VIC_DMA_ENABLE))
        return core_refuse(record,0x70024,poll);
    unsigned int mask = msca_shape_mask();
    if (mask != MSCA_SHAPE_MASK) return core_refuse(record,0x70028,mask);
    if (!msca_fifos_empty(record)) return 0;
    for (unsigned int n = 0; n < sizeof(core_stat_controls)/sizeof(core_stat_controls[0]); ++n)
        if (msca_read(core_stat_controls[n]))
            return core_refuse(record,core_stat_controls[n],msca_read(core_stat_controls[n]));
    if ((msca_read(0x1060)&1) || ((msca_read(0x7804)&1)
#ifdef QC5_MDNS_GATE
        && !mdns_temporal_output_ready()
#endif
        ))
        return core_refuse(record,0x7804,msca_read(0x7804));
    for (unsigned int n = 0; n < 2; ++n) {
        struct qc5_nv12_bank canonical;
        if (!qc5_nv12_plan(n,1280,720,&canonical) ||
            memcmp(&canonical,&state->planes[n],sizeof(canonical)))
            return core_refuse(record,0x7002c,n);
    }
    data->meta[M_LATCH_POLLS] = poll;
    data->meta[M_SHAPE_MASK] = mask;
    for (unsigned int n = 0; n < 2; ++n) {
        msca_write(0xd130,state->planes[n].y); msca_write(0xd150,state->planes[n].uv);
        ++state->queued;
    }
    record->capture_flags |= QC5_BANKS_VERIFIED;
    msca_write(0xd040,1); msca_write(0xd010,1);
    if (msca_read(0xd040) != 1) return core_refuse(record,0xd040,msca_read(0xd040));
#ifdef QC5_DYNAMIC_AWB
    if(!qc5_awb_capture_start(&active_awb))return awb_failed(record);
#endif
    state->completion_pending=0; /* Discard events observed before output enable. */
    data->meta[M_STAGE] = 6;
    return 1;
}

/* OEM ISR drains done FIFOs only after the MSCA frame-end IRQ (bit 0).
 * A zero Y status alone is not an authorization to pop a pair during startup.
 * Keep an observed event until its FIFO is empty, including coalesced pairs.
 * UV status is diagnostic only: the pinned OEM ISR gates the pair on Y.
 */
static int msca_completed_pair(struct msca_record *data,struct msca_state *state,
                               uint32_t *y,uint32_t *uv)
{
    if(!data||!state||!y||!uv||!state->completion_pending)return 0;
    data->meta[M_Y_DONE]=msca_read(0xd13c);
    data->meta[M_UV_DONE]=msca_read(0xd15c);
    if(data->meta[M_Y_DONE]&1){state->completion_pending=0;return 0;}
    *y=msca_read(0xd138);*uv=msca_read(0xd158);
    if(msca_read(0xd13c)&1)state->completion_pending=0;
    return 1;
}

static int msca_complete(struct qc5_preview *record, struct msca_record *data,
                          struct msca_state *state, uint32_t y, uint32_t uv, uint64_t poll)
{
    if(!record||!data||!state)return 0;
    y &= ~7u; uv &= ~7u;
    uint64_t limit=msca_limit(state);
    if (!msca_policy_valid(state)||state->queued < 2 || (limit&&state->queued > limit) ||
        state->completed >= state->queued || (limit&&state->completed >= limit) ||state->completed==UINT64_MAX||
        state->queued-state->completed > 2 ||
        !poll || (!state->service&&poll > QC5_NV12_POLLS) ||
        poll <= (state->service?state->last_poll:data->meta[M_LAST]) || !data->meta[M_LATCH_POLLS] ||
        poll <= data->meta[M_LATCH_POLLS] || data->meta[M_SHAPE_MASK] != MSCA_SHAPE_MASK)
        return core_refuse(record,0x70014,state->completed);
    unsigned int bank = state->completed % 2u;
    if (y != state->planes[bank].y) return core_refuse(record,0xd138,y);
    if (uv != state->planes[bank].uv) return core_refuse(record,0xd158,uv);
#ifdef QC5_MDNS_DEFERRED
    if(!mdns_temporal_frame_ready(record,state->completed+1))return 0;
#endif
#ifdef QC5_BCSH
    if(!bcsh_frame_ready(record,state->completed+1))return 0;
#endif
    if (!state->completed) data->meta[M_FIRST] = msca_note_count(poll);
    state->last_poll=poll;data->meta[M_LAST] = msca_note_count(poll);
    data->meta[M_COMPLETED] = msca_note_count(++state->completed);
    return 1;
}

/* Native direct-mode ISR returns the consumed Y/UV pair to the input FIFOs.
 * A consumed bank is CPU-owned until this function returns it to the DMA;
 * the optional AE meter reads that bank only, before requeue. */
static int msca_requeue(struct qc5_preview *record, struct msca_state *state,
                         uint32_t y, uint32_t uv)
{
    if(!record||!state)return 0;
    uint64_t limit=msca_limit(state);
    if (!msca_policy_valid(state)||!state->completed || (limit&&state->completed > limit) ||
        state->queued < 2 || state->queued < state->completed || (limit&&state->queued > limit) ||
        state->queued-state->completed > 1)
        return core_refuse(record,0x70034,state->queued);
    if (limit&&state->queued == limit) return 1; /* Drain the final pair. */
    if (state->queued-state->completed != 1 ||
        (record->capture_flags&QC5_CAPTURE_VARIANT_REFUSED) || msca_read(0xd040) != 1 ||
        msca_shape_mask() != MSCA_SHAPE_MASK)
        return core_refuse(record,0x70038,state->queued-state->completed);
    unsigned int bank = (state->completed-1u)%2u;
    struct qc5_nv12_bank canonical;
    if (!qc5_nv12_plan(bank,1280,720,&canonical) ||
        memcmp(&canonical,&state->planes[bank],sizeof(canonical)) ||
        (y&~7u) != canonical.y || (uv&~7u) != canonical.uv)
        return core_refuse(record,0x7003c,bank);
    if(state->queued==UINT64_MAX)return core_refuse(record,0x70040,0);
    msca_write(0xd130,canonical.y); msca_write(0xd150,canonical.uv);
    ++state->queued;
    return 1;
}

/* 0 waits without publishing any destination; 1 starts; -1 refuses. */
static int msca_wait_shape(struct qc5_preview *record, struct msca_record *data,
                            struct msca_state *state, unsigned int poll)
{
    if (!msca_policy_valid(state)||!poll || poll > QC5_NV12_POLLS || state->queued || state->completed || msca_read(0xd040)) {
        core_refuse(record,0xd040,msca_read(0xd040));
        return -1;
    }
    if (msca_shape_mask() != MSCA_SHAPE_MASK || !(record->vic_status&1)) return 0;
    if (poll == QC5_NV12_POLLS) return 0; /* No time remains for a frame. */
    return msca_begin_output(record,data,state,poll) ? 1 : -1;
}

static unsigned int msca_stop(void)
{
#ifdef QC5_MDNS_DRAIN
    /* A native MDNS bypass strobe must see a frame before VIC is stopped. */
    unsigned int mdns_drained=mdns_temporal_drain();
#endif
    msca_vic_write(0, 4);
    unsigned int stopped = 0;
    for (unsigned int n = 0; n < 200; ++n) {
        if (!frame_read_vic(0)) { stopped = 1; break; }
        delay_us(1000);
    }
    msca_write(0xd040, 0); msca_write(0x800, 0); msca_write(0xb0, 0);
#ifdef QC5_MDNS_TEMPORAL
    unsigned int mdns_off=mdns_temporal_stop();
#ifdef QC5_MDNS_DRAIN
    mdns_off &= mdns_drained;
#endif
#endif
#ifdef QC5_DYNAMIC_AWB
    if(active_awb.io.write)(void)qc5_awb_capture_stop(&active_awb);
#endif
    msca_vic_write(0x1e8, UINT32_MAX); msca_vic_write(0x1ec, UINT32_MAX);
    unsigned int writers_off = 1;
#ifdef QC5_MDNS_TEMPORAL
    writers_off &= mdns_off;
#endif
    for (unsigned int n = 0; n < sizeof(core_stat_controls) / sizeof(core_stat_controls[0]); ++n)
        writers_off &= msca_read(core_stat_controls[n]) == 0;
    writers_off &= !(msca_read(0x1060) & 1) && !(msca_read(0x7804) & 1);
    return (stopped ? 1u : 0u) | (msca_read(0xd040) == 0 ? 2u : 0u) |
        (frame_read_vic(VIC_DMA_CONTROL) == 0 && frame_read_vic(VIC_DMA_ENABLE) == 0 ? 4u : 0u) |
        (msca_read(0x800) == 0 ? 8u : 0u) | (writers_off ? 16u : 0u);
}

static int nv12_guard(const volatile uint8_t *ram, const struct msca_state *state, uint32_t *bad)
{
    unsigned int next = 0;
    for (unsigned int n = 0; n < QC5_PREVIEW_PHYS - QC5_CAPTURE_RAM_BASE; ++n) {
        uint32_t physical = QC5_CAPTURE_RAM_BASE + n;
        while (next < 2 && physical >= state->planes[next].end) ++next;
        if (next < 2 && physical >= state->planes[next].y) continue;
#ifdef QC5_NV12_ENCODER_HOOK
        if (encoder_owns_address(physical)) continue;
#endif
#ifdef QC5_DYNAMIC_AWB
        if(qc5_awb_owns(&active_awb,physical))continue;
#endif
#ifdef QC5_AF_STATS
        if(qc5_af_owns(&active_af_stats,physical))continue;
#endif
        if (ram[n] != 0xa5) { *bad = physical; return 0; }
    }
    return 1;
}

static uint8_t nv12_clip(int value) { return value < 0 ? 0 : value > 255 ? 255 : (uint8_t)value; }
static int nv12_pack(const volatile uint8_t *ram, struct qc5_preview *record,
                      struct msca_record *data, const struct msca_state *state)
{
    uint32_t *m = data->meta;
#if defined(QC5_NV12_AUDIT) && !defined(QC5_FRAME_TEST)
    /* Do not relax any validation below. Preserve the failed predicate's
     * inputs and bounded plane statistics before the failure union is used. */
    int audit_fd=open("/dev/kmsg",O_WRONLY|O_CLOEXEC);
    if(audit_fd>=0){
        for(unsigned first=0;first<M_FIELD_COUNT;first+=20){
            char line[256];int at=snprintf(line,sizeof line,"<6>QC5_NV12_META first=%u values=",first);
            for(unsigned n=0;n<20;n++)at+=snprintf(line+at,sizeof line-(unsigned)at,"%08x",m[first+n]);
            if(at>0&&at+1<(int)sizeof line){line[at++]='\n';(void)write(audit_fd,line,(unsigned)at);}
        }
        char state_line[192];int state_length=snprintf(state_line,sizeof state_line,
            "<6>QC5_NV12_STATE queued=%llu completed=%llu csi1=%08x csi2=%08x vic=%08x flags=%08x\n",
            (unsigned long long)state->queued,(unsigned long long)state->completed,record->csi_err1,record->csi_err2,record->vic_status,record->capture_flags);
        if(state_length>0&&state_length<(int)sizeof state_line)(void)write(audit_fd,state_line,(unsigned)state_length);
        close(audit_fd);
    }
    if(m[M_STOP]==31)for(unsigned n=0;n<2;n++){
        struct qc5_nv12_bank expected;
        if(qc5_nv12_plan(n,1280,720,&expected)&&!memcmp(&expected,&state->planes[n],sizeof expected))
            qnv_audit_image("msca",n,ram+(expected.y-QC5_CAPTURE_RAM_BASE),ram+(expected.uv-QC5_CAPTURE_RAM_BASE));
    }
#endif
    if (m[M_VERSION] != QC5_NV12_PAYLOAD_VERSION || m[M_TARGET] != QC5_NV12_TARGET ||
        state->queued != QC5_NV12_TARGET || state->completed != QC5_NV12_TARGET ||
        m[M_COMPLETED] != QC5_NV12_TARGET || m[M_STOP] != 31 ||
        !m[M_LATCH_POLLS] || m[M_LATCH_POLLS] >= m[M_FIRST] || m[M_SHAPE_MASK] != MSCA_SHAPE_MASK || !m[M_FIRST] ||
        m[M_FIRST] > m[M_LAST] || m[M_LAST] > m[M_POLLS] || m[M_POLLS] > QC5_NV12_POLLS ||
        m[M_CSC_MATCH] != 1 || m[M_SENSOR_MASK] != 127 || m[M_ISP_IRQ] & 0x3fe ||
        !(m[M_ISP_IRQ]&1) || !(record->vic_status&1) ||
        record->csi_err1 || record->csi_err2 || record->vic_status & VIC_ERRORS) return 0;
    for(unsigned int n=0;n<2;++n){
        struct qc5_nv12_bank canonical;
        if(!qc5_nv12_plan(n,1280,720,&canonical)||memcmp(&canonical,&state->planes[n],sizeof(canonical)))
            return core_refuse(record,0x70020,n);
    }
    uint32_t bad = 0;
    if (!nv12_guard(ram, state, &bad)) return core_refuse(record,bad,ram[bad-QC5_CAPTURE_RAM_BASE]);
    m[M_GUARDS] = 1;
#ifdef QC5_NV12_WB
    if(!wb_record_valid(data))return 0;
#endif
#ifdef QC5_NV12_AE
    if (m[M_AE_INITIAL_GAIN]!=8 ||
#ifdef QC5_IMAGE_CONTINUOUS
        m[M_AE_SAMPLES]!=QC5_NV12_TARGET || m[M_AE_UPDATES]>QC5_NV12_TARGET ||
#else
        m[M_AE_SAMPLES]!=120 || m[M_AE_UPDATES]>120 ||
#endif
        m[M_GAIN]>24 || m[M_EXPOSURE]<4 || m[M_EXPOSURE]>896) return 0;
#else
    m[M_POOL_HASH] = qc5_frame_hash(ram+(QC5_STATS_BASE-QC5_CAPTURE_RAM_BASE),QC5_STATS_BYTES);
#endif
    for (unsigned int n = 0; n < 2; ++n) {
        const struct qc5_nv12_bank *p = &state->planes[n];
        const volatile uint8_t *y = ram + (p->y - QC5_CAPTURE_RAM_BASE);
        const volatile uint8_t *uv = ram + (p->uv - QC5_CAPTURE_RAM_BASE);
        uint32_t sum = 0, u = 0, v = 0, minimum = 255, maximum = 0, changed = 0, uv_changed = 0;
        for (unsigned int i = 0; i < p->y_bytes; ++i) {
            uint32_t value = y[i]; sum += value; changed += value != 0xa5;
            if (value < minimum) minimum = value;
            if (value > maximum) maximum = value;
        }
        for (unsigned int i = 0; i < p->uv_bytes; i += 2) {
            u += uv[i]; v += uv[i + 1];uv_changed += uv[i]!=0xa5;uv_changed += uv[i+1]!=0xa5;
        }
        if (changed < p->y_bytes / 2 || uv_changed < p->uv_bytes/4 || minimum == maximum || (!u && !v)) return 0;
        data->data.success.frames[n] = (struct msca_frame){p->y,p->uv,p->end,
            qc5_frame_hash(y,p->y_bytes),qc5_frame_hash(uv,p->uv_bytes),minimum,maximum,
            sum/p->y_bytes,u/(p->uv_bytes/2),v/(p->uv_bytes/2)};
        if (n == 1) {
            for (unsigned int row = 0; row < QC5_NV12_RGB_HEIGHT; ++row)
                for (unsigned int col = 0; col < 8; ++col) {
                    unsigned int px = (col * 1280 + 640) / 8, py = (row * 720 + 360) / QC5_NV12_RGB_HEIGHT;
                    int luma = y[py * 1280 + px];
                    unsigned int at = (py / 2) * 1280 + (px & ~1u);
                    int cb = (int)uv[at] - 128, cr = (int)uv[at + 1] - 128;
                    unsigned int pixel = (row * 8 + col) * 3;
                    data->data.success.rgb[pixel] = nv12_clip(luma + ((359*cr) >> 8));
                    data->data.success.rgb[pixel+1] = nv12_clip(luma - ((88*cb+183*cr) >> 8));
                    data->data.success.rgb[pixel+2] = nv12_clip(luma + ((454*cb) >> 8));
                }
            record->frame_hash = qc5_frame_hash(y, p->y_bytes + p->uv_bytes);
            record->changed_words = changed; record->packing = QC5_NV12_FOURCC;
            record->selected_frame_phys = p->y; record->sampled_bank_mask = 3;
        }
    }
    m[M_STAGE] = 8;
    return 1;
}

#ifndef QC5_FRAME_TEST
#ifdef QC5_NV12_AE
static int ae_sensor_operation(void *opaque,struct qc5_ae_setting selected,int writing)
{
    struct qc5_preview *record=opaque;
    struct sensor_reg plan[10];uint8_t actual[7]={0};
    if (!sensor_control_plan(selected.lines,selected.gain,plan) ||
        sensor_apb_rate<1000000 || sensor_apb_rate>200000000)
        return core_refuse(record,0x70040,selected.gain);
    uint32_t old_gate=reg_read(CPM,CLKGR);reg_write(CPM,CLKGR,old_gate&~I2C_GATE);
    int ok=configure_i2c(sensor_apb_rate);unsigned int failed=0xffff;uint32_t value=0;
    active_readback=0;
    for(unsigned int n=0;ok&&n<(writing?10u:7u);++n){
        struct sensor_reg item=plan[writing?n:sensor_readback_indices[n]];
        if(writing)ok=sensor_write(item.reg,item.value);
        else ok=read_sensor(item.reg,&actual[n])==SENSOR_ID_OK&&actual[n]==item.value;
        if(!ok){failed=item.reg;value=writing?0x100:actual[n];}
    }
    if(!writing)active_readback=sensor_readback_mask(plan,actual);
    ok=i2c_enable(0)&&ok;
    reg_write(CPM,CLKGR,(reg_read(CPM,CLKGR)&~I2C_GATE)|(old_gate&I2C_GATE));
    if(!ok || (!writing&&active_readback!=127))return core_refuse(record,0x30000+failed,value);
#ifdef QC5_NV12_SPATIAL
    if (writing && !spatial_refresh(core, record, selected.gain)) return 0;
#endif
    active_gain=selected.gain;
    return 1;
}
static int ae_submit(void *opaque,struct qc5_ae_setting setting)
{return ae_sensor_operation(opaque,setting,1);}
static int ae_verify(void *opaque,struct qc5_ae_setting setting)
{return ae_sensor_operation(opaque,setting,0);}

static int ae_time_us(struct qc5_preview *record,uint64_t *result)
{
    struct timespec now;
    if(clock_gettime(CLOCK_MONOTONIC,&now) || now.tv_sec<0 || now.tv_nsec<0)
        return core_refuse(record,0x7004c,0);
    *result=(uint64_t)now.tv_sec*1000000u+(unsigned long)now.tv_nsec/1000u;
    return 1;
}

static int ae_completed_frame(volatile uint8_t *ram,struct qc5_preview *record,
                               struct msca_record *data,const struct msca_state *state,
                               struct qc5_ae_controller *ae)
{
    unsigned int bank=(state->completed-1u)%2u,mean;
    struct qc5_nv12_bank canonical;
    if(!state->completed || state->queued<state->completed ||
        state->queued-state->completed>1 || !qc5_nv12_plan(bank,1280,720,&canonical) ||
        memcmp(&canonical,&state->planes[bank],sizeof(canonical)))
        return core_refuse(record,0x70044,state->completed);
    if(!qc5_ae_meter(ram+(canonical.y-QC5_CAPTURE_RAM_BASE),canonical.y_bytes,&mean))
        return core_refuse(record,0x70048,canonical.y);
    uint64_t us;
    if(!ae_time_us(record,&us))return 0;
    struct qc5_ae_setting before=ae->selected;
    unsigned update_before=ae->updates;
#ifdef QC5_IMAGE_CONTINUOUS
    int ok=qc5_ae_tick(ae,mean,us,msca_updates_allowed(state,12),ae_submit,ae_verify,record);
    int changed=ae->selected.lines!=before.lines||ae->selected.gain!=before.gain;
#else
    int ok=qc5_ae_tick(ae,mean,us,state->completed+12u<120u,ae_submit,ae_verify,record);
    int changed=ae->updates!=update_before;
    (void)before;
#endif
    (void)update_before;
    if(ok&&changed){
        /* Full settle interval starts AFTER the last I2C write. */
        if(!ae_time_us(record,&us)||us>UINT64_MAX-250000)ok=0;
        else ae->due_us=us+250000;
#if defined(QC5_IMAGE_CONTINUOUS) && defined(QC5_DYNAMIC_CCM)
        /* EV changes are independent of AWB statistics. Refresh colour from
         * the actual newly selected exposure too, including during video. */
        uint32_t ev=qc5_awb_ev_q10(ae->selected.lines,ae->selected.gain);
#ifdef QC5_DYNAMIC_BCSH
        if(ok&&!image_colour_event(record,active_awb.ct,ev))ok=0;
#else
        if(ok&&!qc5_ccm_tick(&active_ccm,active_awb.ct,ev))ok=ccm_failed(record);
#endif
#endif
    }
    data->meta[M_GAIN]=ae->selected.gain;data->meta[M_EXPOSURE]=ae->selected.lines;
    data->meta[M_SENSOR_MASK]=active_readback;
    data->meta[M_AE_UPDATES]=ae->updates;data->meta[M_AE_SAMPLES]=ae->samples;
#if defined(QC5_IMAGE_CONTINUOUS) && !defined(QC5_FRAME_TEST)
    if(state->completed==120||state->completed==121||state->completed==QC5_NV12_TARGET){
        int fd=open("/dev/kmsg",O_WRONLY|O_CLOEXEC);
        if(fd>=0){char line[224];int n=snprintf(line,sizeof line,"<6>QC5_IMAGE_CONTROL completed=%llu samples=%u updates=%u pending=%u gain=%u exposure=%u\n",
            (unsigned long long)state->completed,ae->samples,ae->updates,ae->pending,ae->selected.gain,ae->selected.lines);
            if(n>0&&(size_t)n<sizeof line)(void)write(fd,line,(unsigned)n);close(fd);}
    }
#endif
    if(!ok&&!(record->capture_flags&QC5_CAPTURE_VARIANT_REFUSED))
        return core_refuse(record,0x70050,ae->samples);
    return ok;
}
#endif
static void msca_snapshot(struct msca_record *data)
{
    for (unsigned int n = 0; n < 28; ++n) {
        data->data.failure[n].address = 0x13300000u + msca_snapshot_offsets[n];
        data->data.failure[n].value = msca_read(msca_snapshot_offsets[n]);
    }
}

static int msca_poll_events(struct qc5_preview *record,struct msca_record *data,struct msca_state *state)
{
    if(!msca_policy_valid(state))return core_refuse(record,0x70058,0);
    uint32_t *m=data->meta;
    uint32_t irq=msca_read(0xb4),input=frame_read_vic(VIC_IRQ_STATUS);
    m[M_ISP_IRQ]|=irq;m[M_VIC_IRQ]|=input;record->vic_status|=input;
    if((irq&1)&&state->queued>state->completed)state->completion_pending=1;
#ifdef QC5_DYNAMIC_AWB
    if(active_awb.running){
#ifdef QC5_IMAGE_CONTINUOUS
        unsigned before=active_awb.sample_generation;
#else
        unsigned before=active_awb.samples;
#endif
        uint32_t ev=qc5_awb_ev_q10(m[M_EXPOSURE],m[M_GAIN]);
        int allow_gain=msca_updates_allowed(state,12);
        if(!qc5_awb_capture_tick(&active_awb,irq,ev,allow_gain))return awb_failed(record);
#ifdef QC5_IMAGE_CONTINUOUS
        unsigned sampled=active_awb.sample_generation!=before;
#else
        unsigned sampled=active_awb.samples!=before;
#endif
#ifdef QC5_DYNAMIC_CCM
#if defined(QC5_IMAGE_CONTINUOUS) && defined(QC5_DYNAMIC_BCSH)
        if(allow_gain&&sampled&&!image_colour_event(record,active_awb.ct,ev))return 0;
#else
        if(allow_gain&&sampled&&!qc5_ccm_tick(&active_ccm,active_awb.ct,ev))return ccm_failed(record);
#ifdef QC5_DYNAMIC_BCSH
        if(allow_gain&&sampled&&!bcsh_dynamic_tick(record,active_awb.ct,ev))return 0;
#endif
#endif
#else
        (void)before;(void)sampled;
#endif
    }
#endif
    if(irq)msca_write(0xb8,irq);
    if(input)msca_vic_write(0x1f0,input);
    if(irq&0x3fe||input&VIC_ERRORS)return core_refuse(record,0xb4,irq);
    return 1;
}
static int msca_capture_start(volatile uint8_t *ram, struct qc5_preview *record, struct msca_record *data,struct msca_state *state)
{
    uint32_t *m = data->meta;
    for (unsigned int n = 0; n < QC5_PREVIEW_PHYS-QC5_CAPTURE_RAM_BASE; ++n) ram[n] = 0xa5;
    record->intc_mask = intc[4/4]; m[M_INTC_MASK] = record->intc_mask;
    m[M_COLOR_WRITES] = active_color.writes; m[M_LUT_READS] = active_color.reads;
    m[M_LUT_HASH] = active_color.read_hash; m[M_SENSOR_MASK] = active_readback;
    int ok=1;
#ifdef QC5_NV12_ENCODER_HOOK
    ok=encoder_prepare(ram,record);
#endif
#ifdef QC5_DYNAMIC_AWB
    const struct qc5_awb_io awb_io={record,awb_hw_read,awb_hw_write,awb_hw_time};
    if(ok&&!qc5_awb_capture_prepare(&active_awb,&awb_io,ram,0))ok=awb_failed(record);
#ifdef QC5_DYNAMIC_CCM
    const struct qc5_ccm_io ccm_io={record,awb_hw_read,awb_hw_write};
    if(ok&&!qc5_ccm_prepare(&active_ccm,&ccm_io,0))ok=ccm_failed(record);
#endif
#endif
    if(ok)ok=msca_program(record, data, state, msca_plan, QC5_MSCA_PLAN_COUNT);
    if (ok) {
        if (!vic_start(1)) ok = core_refuse(record, 0x10000, frame_read_vic(0));
        else record->capture_flags |= QC5_VIC_START_DEFERRED;
    }
    if (ok) {
        m[M_STAGE] = 5;
        msca_vic_write(0x1f0, frame_read_vic(VIC_IRQ_STATUS));
        msca_vic_write(0x1e8, ~VIC_INPUT_EVENTS);
        msca_vic_write(0, 1);
        if (frame_read_vic(0) != 1) ok = core_refuse(record, 0x10000, frame_read_vic(0));
        else m[M_STAGE] = 5;
    }
    return ok;
}
static int msca_capture(volatile uint8_t *ram, struct qc5_preview *record, struct msca_record *data)
{
#ifdef QC5_CAPTURE_SERVICE
    return encoder_service_capture(ram,record,data);
#else
    struct msca_state state = {0};
#ifdef QC5_NV12_AE
    struct qc5_ae_controller ae={.selected={720,QC5_NV12_INITIAL_GAIN},.target=QC5_AE_TARGET};
#endif
    uint32_t *m=data->meta;int ok=msca_capture_start(ram,record,data,&state);
    for (unsigned int n = 1; ok && n <= QC5_NV12_POLLS; ++n) {
        m[M_POLLS] = n;
        if(!msca_poll_events(record,data,&state)){ok=0;break;}
        if (!state.queued) {
            if (msca_wait_shape(record,data,&state,n) < 0) { ok = 0; break; }
            delay_us(1000);
            continue;
        }
        if (msca_shape_mask() != MSCA_SHAPE_MASK) {
            ok = core_refuse(record,0x70030,msca_shape_mask()); break;
        }
        uint32_t y,uv;
        if (msca_completed_pair(data,&state,&y,&uv)) {
            if (!msca_complete(record, data, &state, y, uv, n)) { ok = 0; break; }
#ifdef QC5_NV12_AE
#ifdef QC5_IMAGE_CONTINUOUS
            if (!ae_completed_frame(ram,record,data,&state,&ae)) { ok=0;break; }
#else
            if (state.completed<=120u&&!ae_completed_frame(ram,record,data,&state,&ae)) { ok=0;break; }
#endif
#endif
#ifdef QC5_NV12_ENCODER_HOOK
            /* The completed input remains owned by the encoder until this
             * callback returns. It is never requeued while VPU can read it. */
#ifdef QC5_STREAM_RUNTIME
            if(!encoder_runtime_frame(ram,record,data,&state)){ok=0;break;}
#else
            if(!encoder_completed(ram,record,state.completed)){ok=0;break;}
#endif
#endif
#ifndef QC5_STREAM_RUNTIME
            if (!msca_requeue(record,&state,y,uv)) { ok = 0; break; }
#endif
        }
        if (state.completed == QC5_NV12_TARGET) break;
        delay_us(1000);
    }
    NV12_PROGRESS("before_snapshot");
#if defined(QC5_IMAGE_CONTINUOUS) && defined(QC5_NV12_AE)
    if(ok&&(ae.pending||ae.samples!=msca_note_count(state.completed))){ok=core_refuse(record,0x70054,ae.pending);}
#endif
    msca_snapshot(data); /* Preserve state before cleanup in every failure path. */
    NV12_PROGRESS("before_stop");
    m[M_STOP] = msca_stop();
#ifdef QC5_STREAM_RUNTIME
    if(!encoder_runtime_stopped(record,m[M_STOP]))ok=0;
#endif
    NV12_PROGRESS("after_stop");
    m[M_CORE_RUN] = msca_read(0x800); m[M_OUTPUT_ENABLE] = msca_read(0xd040);
    m[M_VIC_RUN] = frame_read_vic(0);
    record->csi_err1 = csi[0x20/4]; record->csi_err2 = csi[0x24/4];
    if (ok && state.completed == QC5_NV12_TARGET && m[M_STOP] == 31) {
        delay_us(250000);
        NV12_PROGRESS("before_color_finish");
        if (!color_finish(core, record)) ok = 0;
        else m[M_CSC_MATCH] = color_csc_mode() == 0;
#ifdef QC5_DYNAMIC_AWB
        if(ok&&!qc5_awb_capture_finish(&active_awb))ok=awb_failed(record);
#ifdef QC5_DYNAMIC_CCM
        if(ok&&!qc5_ccm_finish(&active_ccm))ok=ccm_failed(record);
#endif
#endif
#ifdef QC5_NV12_WB
        if(ok&&!wb_finish(core,record,data))ok=0;
#endif
#ifdef QC5_NV12_SPATIAL
        if(ok&&!spatial_finish(core,record,data))ok=0;
#endif
#ifdef QC5_AF_STATS
        if(ok&&!af_stats_finish(ram,record,m[M_STOP]))ok=0;
#endif
#ifdef QC5_SENSOR_ORIENTATION
        if(ok&&!sensor_orientation_finish(record))ok=0;
#endif
        NV12_PROGRESS("before_pack");
        if (ok && nv12_pack(ram, record, data, &state)) {NV12_PROGRESS("after_pack");return CAPTURE_OK;}
        if (!(record->capture_flags & QC5_CAPTURE_VARIANT_REFUSED)) core_refuse(record, 0x7000c, m[M_STOP]);
        msca_snapshot(data);
        return CAPTURE_FRAME;
    }
    return ok && state.completed != QC5_NV12_TARGET && m[M_STOP] == 31 ? CAPTURE_TIMEOUT : CAPTURE_FRAME;
#endif
}

int main(void)
{
    int fd = open("/dev/mem", O_RDWR|O_SYNC|O_CLOEXEC);
    if (fd < 0 || map_sensor_registers(fd) != SENSOR_ID_OK) return CAPTURE_MAP;
    if (probe_gc1084() != SENSOR_ID_OK) return CAPTURE_SENSOR;
    csi=map_phys(fd,0x10023000,4096);phy=map_phys(fd,0x10022000,4096);
    vic=map_phys(fd,0x133e0000,4096);intc=map_phys(fd,0x10001000,4096);
    core=map_phys(fd,0x13300000,QC5_COLOR_MAP_BYTES);
    volatile uint8_t *ram=map_phys(fd,QC5_CAPTURE_RAM_BASE,QC5_CAPTURE_RAM_END-QC5_CAPTURE_RAM_BASE);
    close(fd);
    if (csi==MAP_FAILED||phy==MAP_FAILED||vic==MAP_FAILED||intc==MAP_FAILED||core==MAP_FAILED||ram==MAP_FAILED) return CAPTURE_MAP;
    struct qc5_preview record={.capture_flags=QC5_VIC_EVENT_POLL}; struct msca_record data=msca_initial();
    int status=CAPTURE_CLOCK;
    if (video_clocks(&record)&&reset_isp(&record)) {
        csi_start(); status=CAPTURE_SENSOR_MODE;
        if (csi[0]==0x3130322a && sensor_start(&record)) {
            data.meta[M_STAGE]=1;record.capture_flags|=isp_input_route(core,&record);
            volatile uint8_t *pool=ram+(QC5_STATS_GUARD_BASE-QC5_CAPTURE_RAM_BASE);
            if (!(record.capture_flags&QC5_ISP_ROUTE_APPLIED)||!core_mode_for_restart(core,&record)||
                !sensor_apply_controls(QC5_NV12_INITIAL_GAIN,&record)||!core_prepare(core,pool,&record)||
                !color_apply_plan(core,&record,color_plan,QC5_COLOR_WRITES)
#ifdef QC5_NV12_WB
                || !wb_apply(core,&record,wb_plan,8)
#endif
#ifdef QC5_NV12_SPATIAL
                || !spatial_apply(core,&record)
#endif
                ) {
                status=CAPTURE_FRAME;msca_snapshot(&data);
            } else {data.meta[M_STAGE]=2;status=msca_capture(ram,&record,&data);}
        }
        mmio(csi,8,0);mmio(csi,12,0);mmio(csi,16,0);
    }
    if ((record.capture_flags&QC5_CAPTURE_VARIANT_REFUSED)&&!data.meta[M_FAULT_ADDRESS]) {
        data.meta[M_FAULT_ADDRESS]=record.isp_route_blocker;data.meta[M_FAULT_VALUE]=record.dma_addr_before;
    }
    memcpy(record.pixels,&data,sizeof(data));
    publish_record(ram,&record,status);
    printf("QC5_NV12_RESULT %d completed=%u stage=%u\n",status,data.meta[M_COMPLETED],data.meta[M_STAGE]);
#if defined(QC5_CAPTURE_SERVICE) && defined(QC5_DYNAMIC_AWB)
    printf("QC5_AWB_TIMING samples=%u copy_max_us=%u copy_drops=%u work_max_us=%u work_overruns=%u quantized=%u start=%llu copied=%llu end=%llu fault=%08x value=%08x\n",
        active_awb.samples,active_awb.copy_max_us,active_awb.copy_drops,active_awb.max_us,
        active_awb.work_overruns,active_awb.quantized_work,
        (unsigned long long)active_awb.last_start_us,(unsigned long long)active_awb.last_copy_us,
        (unsigned long long)active_awb.last_end_us,active_awb.fault_reg,active_awb.fault_value);
#endif
    if(status)printf("QC5_NV12_DETAIL fault=%08x value=%08x irq=%08x vic=%08x y_status=%08x uv_status=%08x polls=%u stop=%u\n",
        data.meta[M_FAULT_ADDRESS],data.meta[M_FAULT_VALUE],data.meta[M_ISP_IRQ],data.meta[M_VIC_IRQ],
        data.meta[M_Y_DONE],data.meta[M_UV_DONE],data.meta[M_POLLS],data.meta[M_STOP]);
    return status;
}
#endif
