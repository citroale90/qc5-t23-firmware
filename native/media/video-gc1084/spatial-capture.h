#ifndef QC5_SPATIAL_CAPTURE_H
#define QC5_SPATIAL_CAPTURE_H
#include "isp-spatial.h"
#include "gc1084-spatial.private.h"
#ifdef QC5_BCSH
#include "bcsh-capture.h"
#endif
#ifdef QC5_NV12_DETAIL
#include "detail-capture.h"
#endif
#ifdef QC5_AF_STATS
#include "af-stats-capture.h"
#endif
#ifdef QC5_MDNS_TEMPORAL
#include "mdns-temporal-capture.h"
#elif defined(QC5_MDNS_BUFFER_PROBE)
#include "mdns-probe-capture.h"
#endif

static struct qc5_spatial_state active_spatial;
static uint32_t spatial_readback_hash;
static int spatial_write(void *opaque, uint32_t offset, uint32_t value)
{
    color_write(opaque, offset, value);
    return 1;
}
static int spatial_apply(volatile uint32_t *isp, struct qc5_preview *record)
{
    memset(&active_spatial, 0, sizeof(active_spatial)); spatial_readback_hash = 0;
#ifdef QC5_NV12_DETAIL
    memset(&active_detail,0,sizeof active_detail);
#endif
#ifdef QC5_AF_STATS
    memset(&active_af_stats,0,sizeof active_af_stats);
#endif
    /* Only load coefficients before VIC, core and all DMA writers start.
     * The bypass bit is changed later, in the existing guarded trigger. */
    if (!color_idle(isp, record)) return 0;
#ifdef QC5_BCSH
    if(!bcsh_prepare(isp,record))return 0;
#endif
    if (!qc5_spatial_load(&active_spatial, &gc1084_spatial, 0, spatial_write, (void *)isp) ||
        !qc5_spatial_gain(&active_spatial, QC5_NV12_INITIAL_GAIN, spatial_write, (void *)isp))
        return core_refuse(record, 0x73000, QC5_NV12_INITIAL_GAIN);
#ifdef QC5_NV12_DETAIL
    if(!detail_apply(isp,record))return 0;
#endif
#ifdef QC5_AF_STATS
    if(!af_stats_prepare(isp,record))return 0;
#endif
#ifdef QC5_MDNS_TEMPORAL
    if(!mdns_temporal_prepare(isp,record))return 0;
#elif defined(QC5_MDNS_BUFFER_PROBE)
    if(!mdns_probe_prepare(isp,record))return 0;
#endif
    return color_idle(isp, record);
}
static int spatial_refresh(volatile uint32_t *isp, struct qc5_preview *record, unsigned gain)
{
    if (!qc5_spatial_gain(&active_spatial, gain, spatial_write, (void *)isp))
        return core_refuse(record, 0x73004, gain);
#ifdef QC5_NV12_DETAIL
    if(!detail_refresh(isp,record,gain))return 0;
#endif
#ifdef QC5_MDNS_TEMPORAL
    if(!mdns_temporal_gain(isp,record,gain))return 0;
#endif
    return 1;
}
static int spatial_finish_frames(volatile uint32_t *isp, struct qc5_preview *record,
                          const struct msca_record *data,qc5_filter_count completed)
{
    (void)completed;
    spatial_readback_hash = 0;
    if (!active_spatial.ready || active_spatial.profile != &gc1084_spatial ||
        active_spatial.bank != 0 || active_spatial.gain_index != data->meta[M_GAIN] ||
        data->meta[M_STOP] != 31 || color_read(isp, 0x0c) != QC5_NV12_BYPASS)
        return core_refuse(record, 0x73008, active_spatial.gain_index);
    uint32_t hash = UINT32_C(2166136261);
    for (unsigned n = 0; n < QC5_SPATIAL_PATCH; ++n) {
        uint32_t offset = gc1084_spatial.offsets[n], value = color_read(isp, offset);
        if (value != gc1084_spatial.values[0][active_spatial.gain_index][n])
            return core_refuse(record, offset, value);
        hash = color_hash_pair(hash, offset, value);
    }
    spatial_readback_hash = hash;
#ifdef QC5_BCSH
    if(!bcsh_finish_frames(isp,record,data->meta[M_STOP],completed))return 0;
#endif
#ifdef QC5_NV12_DETAIL
    if(!detail_finish(isp,record,data->meta[M_GAIN]))return 0;
#endif
#ifdef QC5_MDNS_TEMPORAL
    if(!mdns_temporal_finish(isp,record,data->meta[M_GAIN],data->meta[M_STOP]))return 0;
#elif defined(QC5_MDNS_BUFFER_PROBE)
    if(!mdns_probe_finish(isp,record,data->meta[M_STOP]))return 0;
#endif
    return 1;
}
static int spatial_finish(volatile uint32_t *isp,struct qc5_preview *record,const struct msca_record *data)
{return spatial_finish_frames(isp,record,data,QC5_NV12_TARGET);}
#endif
