#ifndef QC5_ISP_SPATIAL_H
#define QC5_ISP_SPATIAL_H
#include <stdint.h>
#include "filter-counts.h"
#include <stddef.h>

/* Calibration is supplied by an offline builder, not embedded in this source.
 * The current GC1084 controller selects 25 discrete analog gains. Plans cover
 * precisely those settings; unsupported gains must not be approximated.
 * This module allocates no frame/statistics buffers and never enables bypass
 * bits. Its caller must park the pipeline before loading a day/night bank.
 */
#define QC5_SPATIAL_GAINS 25u
#define QC5_SPATIAL_INIT 209u
#define QC5_SPATIAL_PATCH 36u
#define QC5_SPATIAL_LATCH 0x8b4cu
struct qc5_spatial_row { uint32_t offset, value; };
struct qc5_spatial_profile {
    struct qc5_spatial_row initial[2][QC5_SPATIAL_INIT];
    uint32_t offsets[QC5_SPATIAL_PATCH];
    uint32_t gain_q16[QC5_SPATIAL_GAINS];
    uint32_t values[2][QC5_SPATIAL_GAINS][QC5_SPATIAL_PATCH];
    uint32_t checksum;
};
struct qc5_spatial_state {
    const struct qc5_spatial_profile *profile;
    uint32_t bank, gain_index, ready;
    qc5_filter_count writes, updates;
};
typedef int (*qc5_spatial_write)(void *, uint32_t, uint32_t);

static uint32_t qc5_spatial_hash_word(uint32_t hash, uint32_t value)
{
    for (unsigned n = 0; n < 4; ++n)
        hash = (hash ^ ((value >> (8u * n)) & 255u)) * UINT32_C(16777619);
    return hash;
}

static uint32_t qc5_spatial_checksum(const struct qc5_spatial_profile *profile)
{
    uint32_t hash = UINT32_C(2166136261);
    for (unsigned b = 0; b < 2; ++b)
        for (unsigned n = 0; n < QC5_SPATIAL_INIT; ++n) {
            hash = qc5_spatial_hash_word(hash, profile->initial[b][n].offset);
            hash = qc5_spatial_hash_word(hash, profile->initial[b][n].value);
        }
    for (unsigned n = 0; n < QC5_SPATIAL_PATCH; ++n)
        hash = qc5_spatial_hash_word(hash, profile->offsets[n]);
    for (unsigned n = 0; n < QC5_SPATIAL_GAINS; ++n)
        hash = qc5_spatial_hash_word(hash, profile->gain_q16[n]);
    for (unsigned b = 0; b < 2; ++b)
        for (unsigned g = 0; g < QC5_SPATIAL_GAINS; ++g)
            for (unsigned n = 0; n < QC5_SPATIAL_PATCH; ++n)
                hash = qc5_spatial_hash_word(hash, profile->values[b][g][n]);
    return hash;
}

static int qc5_spatial_offset(uint32_t offset)
{
    return !(offset & 3u) && offset >= 0x8800u && offset <= 0x8b38u;
}

static int qc5_spatial_valid(const struct qc5_spatial_profile *profile)
{
    if (!profile || qc5_spatial_checksum(profile) != profile->checksum) return 0;
    if (profile->gain_q16[0] != 0 || profile->gain_q16[4] != 65536u ||
        profile->gain_q16[24] != 393216u) return 0;
    for (unsigned g = 1; g < QC5_SPATIAL_GAINS; ++g)
        if (profile->gain_q16[g] <= profile->gain_q16[g-1] ||
            profile->gain_q16[g] - profile->gain_q16[g-1] < 0x100u) return 0;
    for (unsigned b = 0; b < 2; ++b)
        for (unsigned n = 0; n < QC5_SPATIAL_INIT; ++n) {
            struct qc5_spatial_row row = profile->initial[b][n];
            if (n < QC5_SPATIAL_INIT - 2u) {
                if (!qc5_spatial_offset(row.offset)) return 0;
            } else if (row.offset != QC5_SPATIAL_LATCH || row.value != 1u) return 0;
        }
    for (unsigned n = 0; n < QC5_SPATIAL_PATCH; ++n) {
        if (!qc5_spatial_offset(profile->offsets[n])) return 0;
        for (unsigned j = 0; j < n; ++j)
            if (profile->offsets[j] == profile->offsets[n]) return 0;
    }
    return 1;
}

static int qc5_spatial_load(struct qc5_spatial_state *state,
                            const struct qc5_spatial_profile *profile,
                            unsigned bank, qc5_spatial_write write, void *opaque)
{
    if (!state) return 0;
    state->ready = 0;
    if (bank > 1u || !write || !qc5_spatial_valid(profile)) return 0;
    for (unsigned n = 0; n < QC5_SPATIAL_INIT; ++n) {
        struct qc5_spatial_row row = profile->initial[bank][n];
        if (!write(opaque, row.offset, row.value)) return 0;
    }
    *state = (struct qc5_spatial_state){.profile=profile, .bank=bank,
        .gain_index=4, .ready=1, .writes=QC5_SPATIAL_INIT};
    return 1;
}

static int qc5_spatial_gain(struct qc5_spatial_state *state, unsigned gain_index,
                            qc5_spatial_write write, void *opaque)
{
    if (!state || !state->ready || !state->profile || state->bank > 1u ||
        state->gain_index >= QC5_SPATIAL_GAINS || gain_index >= QC5_SPATIAL_GAINS || !write)
        return 0;
    const struct qc5_spatial_profile *profile = state->profile;
    unsigned update = gain_index != state->gain_index;
    unsigned writes = update ? QC5_SPATIAL_PATCH + 1u : 1u;
    if (!qc5_filter_room(state->writes,writes)||!qc5_filter_room(state->updates,1)) return 0;
    /* The profile is immutable after loading. Validate all selected addresses
     * before the first write, retaining a narrow MMIO boundary at runtime. */
    if (update)
        for (unsigned n = 0; n < QC5_SPATIAL_PATCH; ++n)
            if (!qc5_spatial_offset(profile->offsets[n])) return 0;
    state->ready = 0;
    if (update)
        for (unsigned n = 0; n < QC5_SPATIAL_PATCH; ++n)
            if (!write(opaque, profile->offsets[n], profile->values[state->bank][gain_index][n]))
                return 0;
    if (!write(opaque, QC5_SPATIAL_LATCH, 1)) return 0;
    state->gain_index = gain_index;
    state->writes += writes;
    state->updates += update;
    state->ready = 1;
    return 1;
}
#endif
