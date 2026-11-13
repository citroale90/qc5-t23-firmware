/* SPDX-License-Identifier: GPL-3.0-only
 * Adapted from opensensor/open-tx-isp 2fb6534b9ecdc8d39a6041debee6455ec2da512f
 * driver/t23/tx_isp_t23_awb_runtime.inc. Pure math, explicit per-instance state.
 */
static uint64_t awb_u64_div(uint64_t n,uint64_t d){return n/d;}
static uint32_t awb_mul_q(struct qc5_awb_state *s,uint32_t left, uint32_t right)
{
    return (uint32_t)(((uint64_t)left * right) >>
                      s->profile->point);
}

static uint32_t awb_div_q(struct qc5_awb_state *s,uint64_t numerator,
                                            uint64_t denominator)
{
    if (!denominator)
        return 0;
    return (uint32_t)awb_u64_div(
        numerator << s->profile->point, denominator);
}

static uint32_t awb_interpolate_q(struct qc5_awb_state *s,uint32_t x_q,
                                                    uint32_t x0,
                                                    uint32_t x1,
                                                    uint32_t y0_q,
                                                    uint32_t y1_q)
{
    uint32_t q = s->profile->point;
    uint32_t x0_q = x0 << q;
    uint32_t span_q = (x1 - x0) << q;
    uint32_t delta_x;
    uint64_t delta;

    if (!span_q || x_q <= x0_q)
        return y0_q;
    if (x_q >= (x1 << q))
        return y1_q;

    delta_x = x_q - x0_q;
    if (y1_q >= y0_q) {
        delta = (uint64_t)(y1_q - y0_q) * delta_x;
        return y0_q + (uint32_t)awb_u64_div(delta, span_q);
    }

    delta = (uint64_t)(y0_q - y1_q) * delta_x;
    return y0_q - (uint32_t)awb_u64_div(delta, span_q);
}

static uint32_t awb_mesh_q(struct qc5_awb_state *s,const uint32_t *mesh,
                                             uint32_t rg_q,
                                             uint32_t bg_q)
{
    const uint32_t *rg_pos = s->profile->rg_pos;
    const uint32_t *bg_pos = s->profile->bg_pos;
    uint32_t q = s->profile->point;
    uint32_t rg_index = 13U;
    uint32_t bg_index = 13U;
    uint32_t row0_q;
    uint32_t row1_q;
    uint32_t i;

    rg_q = AWB_MAX(rg_q, rg_pos[0] << q);
    rg_q = AWB_MIN(rg_q, rg_pos[14] << q);
    bg_q = AWB_MAX(bg_q, bg_pos[0] << q);
    bg_q = AWB_MIN(bg_q, bg_pos[14] << q);

    for (i = 0; i < 14U; ++i) {
        if (rg_q <= (rg_pos[i + 1U] << q)) {
            rg_index = i;
            break;
        }
    }
    for (i = 0; i < 14U; ++i) {
        if (bg_q <= (bg_pos[i + 1U] << q)) {
            bg_index = i;
            break;
        }
    }

    row0_q = awb_interpolate_q(s,
        rg_q, rg_pos[rg_index], rg_pos[rg_index + 1U],
        mesh[bg_index * 15U + rg_index] << q,
        mesh[bg_index * 15U + rg_index + 1U] << q);
    row1_q = awb_interpolate_q(s,
        rg_q, rg_pos[rg_index], rg_pos[rg_index + 1U],
        mesh[(bg_index + 1U) * 15U + rg_index] << q,
        mesh[(bg_index + 1U) * 15U + rg_index + 1U] << q);

    return awb_interpolate_q(s,
        bg_q, bg_pos[bg_index], bg_pos[bg_index + 1U], row0_q, row1_q);
}

static uint32_t awb_light_source_weight_q(struct qc5_awb_state *s,uint32_t rg_q,
                                                            uint32_t bg_q)
{
    const uint32_t q = s->profile->point;
    const uint32_t rounding = 1U << (q - 1U);
    uint32_t rg = (rg_q + rounding) >> q;
    uint32_t bg = (bg_q + rounding) >> q;
    uint32_t best_weight_q = 0;
    uint32_t source;

    for (source = 0; source < s->profile->light_count;
         ++source) {
        uint32_t ref_rg = s->profile->lights[source * 2U];
        uint32_t ref_bg =
            s->profile->lights[source * 2U + 1U];
        uint32_t delta_rg;
        uint32_t delta_bg;
        uint32_t distance;
        uint32_t weight = 0;

        if (ref_rg)
            ref_rg--;
        if (ref_bg)
            ref_bg--;
        delta_rg = rg >= ref_rg ? rg - ref_rg : ref_rg - rg;
        delta_bg = bg >= ref_bg ? bg - ref_bg : ref_bg - bg;
        distance = delta_rg * delta_rg + delta_bg * delta_bg;

        if (distance < 0x332U) {
            weight = 1U;
            if (distance < 0x2a7U) {
                weight = 2U;
                if (distance < 0x267U) {
                    weight = 3U;
                    if (distance < 0x23cU) {
                        weight = 4U;
                        if (distance < 0x21cU) {
                            weight = 5U;
                            if (distance < 0x202U)
                                weight =
                                    s->profile->light_lut[
                                        distance];
                        }
                    }
                }
            }
        }
        best_weight_q = AWB_MAX(best_weight_q, weight << q);
    }

    return best_weight_q;
}

/*
 * The cluster part of stock Tiziano_Awb_Ct_Detect.  Every run fills the
 * rg/bg histogram of the 225 zones as stock does (it is state of the next
 * cluster run); with the cluster object enabled (ClusterEn == 1) the zone
 * weights are then replaced by the cluster weights (0x14e8c..0x15628).
 * Stock handles every zone, also the ones below the pixel threshold (their
 * ratios read 0 and are clamped to the first position): the arrays here are
 * built that way, and only the zones this run uses get a new weight.
 */
static void awb_cluster_stage(struct qc5_awb_state *s,
    const struct qc5_awb_snapshot *snapshot, bool indoor,
    const uint32_t *mesh, bool use_result)
{
    uint32_t *zr=s->cluster_r,*zb=s->cluster_b,*zw=s->cluster_w;
    const uint32_t q = s->profile->point;
    uint32_t i;

    for (i = 0; i < T23X_CL_ZONES; ++i) {
        uint32_t r = s->raw_rg[i];
        uint32_t b = s->raw_bg[i];

        if (snapshot->pixels[i] <= s->profile->pixel_threshold) {
            r = 0;
            b = 0;
        }
        r = AWB_MAX(r, s->profile->rg_pos[0] << q);
        r = AWB_MIN(r, s->profile->rg_pos[14] << q);
        b = AWB_MAX(b, s->profile->bg_pos[0] << q);
        b = AWB_MIN(b, s->profile->bg_pos[14] << q);
        zr[i] = r;
        zb[i] = b;
        zw[i] = awb_mesh_q(s,mesh, r, b);
        if (indoor)
            zw[i] = AWB_MAX(zw[i],
                        awb_light_source_weight_q(s,r, b));
    }
    t23x_awb_cluster_count(&s->clusters, zr, zb,
                           s->profile->rg_pos,
                           s->profile->bg_pos, q);
    if (s->profile->cluster[0] != 1U)
        return;
    if (!t23x_awb_cluster_weights(&s->clusters,
                                  s->profile->cluster, zr, zb, zw,
                                  s->profile->rg_pos,
                                  s->profile->bg_pos, q) ||
        !use_result)
        return;
    for (i = 0; i < T23X_CL_ZONES; ++i)
        if (s->pixel_weight[i])
            s->mesh_weight[i] = zw[i];
}

static int awb_calculate(struct qc5_awb_state *s,
    const struct qc5_awb_snapshot *snapshot,
    struct qc5_awb_calculation *result)
{
    const uint32_t q = s->profile->point;
    const uint32_t rounding = 1U << (q - 1U);
    const uint32_t total = QC5_AWB_ZONES;
    const uint32_t bg_offset = QC5_AWB_ZONES;
    uint32_t max_pixels = 0;
    uint32_t coefficient_rg_q;
    uint32_t coefficient_bg_q;
    uint64_t total_mesh_weight = 0;
    uint64_t sum_rg = 0;
    uint64_t sum_bg = 0;
    uint64_t sum_weight = 0;
    uint32_t average_rg_q;
    uint32_t average_bg_q;
    uint32_t active_zones = 0;
    bool indoor = s->ev >=
                  (s->profile->indoor_ev << q);
    const uint32_t *rgbg_weight_mesh = indoor ?
        s->profile->indoor_mesh :
        s->profile->outdoor_mesh;
    uint32_t i;

    coefficient_rg_q = awb_mul_q(s,
        0x100U << q, s->profile->wb_static[0]);
    coefficient_bg_q = awb_mul_q(s,
        0x100U << q, s->profile->wb_static[1]);

    for (i = 0; i < total; ++i)
        max_pixels = AWB_MAX(max_pixels, snapshot->pixels[i]);
    if (!max_pixels) {
        /* stock still runs the histogram and the cluster stage */
        awb_cluster_stage(s,snapshot, indoor, rgbg_weight_mesh,
                                       false);
        result->ct=5000u;return 0;
    }

    for (i = 0; i < total; ++i) {
        uint32_t green = snapshot->g[i];
        uint32_t pixels = snapshot->pixels[i];
        uint32_t rg_q = 0;
        uint32_t bg_q = 0;

        if (green) {
            uint32_t raw_rg_q = (uint32_t)awb_u64_div(
                (uint64_t)snapshot->r[i] << q, green);
            uint32_t raw_bg_q = (uint32_t)awb_u64_div(
                (uint64_t)snapshot->b[i] << q, green);

            rg_q = awb_mul_q(s,raw_rg_q, coefficient_rg_q);
            bg_q = awb_mul_q(s,raw_bg_q, coefficient_bg_q);
        }
        s->raw_rg[i] = rg_q;
        s->raw_bg[i] = bg_q;

        if (pixels <= s->profile->pixel_threshold ||
            !rg_q || !bg_q) {
            s->ratio[i] = 0;
            s->ratio[bg_offset + i] = 0;
            s->pixel_weight[i] = 0;
            s->mesh_weight[i] = 0;
            continue;
        }

        rg_q = AWB_MAX(rg_q, s->profile->rg_pos[0] << q);
        rg_q = AWB_MIN(rg_q, s->profile->rg_pos[14] << q);
        bg_q = AWB_MAX(bg_q, s->profile->bg_pos[0] << q);
        bg_q = AWB_MIN(bg_q, s->profile->bg_pos[14] << q);
        s->ratio[i] = rg_q;
        s->ratio[bg_offset + i] = bg_q;
        s->pixel_weight[i] =
            awb_div_q(s,pixels, max_pixels);
        s->mesh_weight[i] =
            awb_mesh_q(s,
                rgbg_weight_mesh, rg_q, bg_q);
        s->mesh_weight[i]=qc5_awb_ct_weight(s,rg_q,bg_q,s->mesh_weight[i],
            awb_mesh_q(s,s->profile->ct_mesh,rg_q,bg_q));
        active_zones++;
    }

    if (indoor) {
        for (i = 0; i < total; ++i) {
            uint32_t light_weight;

            if (!s->ratio[i] ||
                !s->ratio[bg_offset + i] ||
                !s->pixel_weight[i])
                continue;
            light_weight = awb_light_source_weight_q(s,
                s->ratio[i],
                s->ratio[bg_offset + i]);
            if (light_weight > s->mesh_weight[i]) {
                s->mesh_weight[i] = light_weight;
                result->light_source_zones++;
            }
        }
    }
    awb_cluster_stage(s,snapshot, indoor, rgbg_weight_mesh, true);
    for (i = 0; i < total; ++i)
        total_mesh_weight += s->mesh_weight[i];

    if (!active_zones || !total_mesh_weight)
        goto no_usable_zone;

    for (i = 0; i < total; ++i) {
        uint32_t weight = awb_mul_q(s,
            s->mesh_weight[i],
            s->pixel_weight[i]);

        sum_rg += awb_mul_q(s,
            s->ratio[i], weight);
        sum_bg += awb_mul_q(s,
            s->ratio[bg_offset + i], weight);
        sum_weight += weight;
    }
    if (!sum_weight)
        goto no_usable_zone;

    average_rg_q = awb_div_q(s,sum_rg, sum_weight);
    average_bg_q = awb_div_q(s,sum_bg, sum_weight);

    if (s->profile->distance[0] == 1U) {
        uint32_t distance_sum = 0; /* Exact OEM 32-bit accumulator wraps. */
        uint64_t refined_rg = 0;
        uint64_t refined_bg = 0;
        uint64_t refined_weight = 0;
        uint32_t normalized_distance;

        for (i = 0; i < total; ++i) {
            uint32_t rg_q = s->ratio[i];
            uint32_t bg_q = s->ratio[bg_offset + i];
            uint32_t delta_rg;
            uint32_t delta_bg;
            uint32_t distance;

            if (!s->mesh_weight[i] ||
                !s->pixel_weight[i]) {
                s->distance[i] = 0;
                continue;
            }
            delta_rg = average_rg_q >= rg_q ?
                average_rg_q - rg_q : rg_q - average_rg_q;
            delta_bg = average_bg_q >= bg_q ?
                average_bg_q - bg_q : bg_q - average_bg_q;
            distance = awb_mul_q(s,delta_rg, delta_rg) +
                       awb_mul_q(s,delta_bg, delta_bg);
            s->distance[i] = distance;
            distance_sum += awb_mul_q(s,
                awb_mul_q(s,awb_mul_q(s,distance,s->mesh_weight[i]),s->pixel_weight[i]),
                s->profile->zone_mesh[i]<<q);
        }

        normalized_distance = awb_div_q(s,
            distance_sum, sum_weight);
        if (normalized_distance) {
            uint32_t distance_hi =
                s->profile->distance[1];
            uint32_t distance_lo =
                s->profile->distance[2];

            for (i = 0; i < total; ++i) {
                uint32_t relative_distance;
                uint32_t distance_weight;
                uint32_t weight;

                if (!s->mesh_weight[i] ||
                    !s->pixel_weight[i])
                    continue;
                relative_distance = awb_div_q(s,
                    s->distance[i], normalized_distance);
                if (relative_distance > distance_hi) {
                    distance_weight = 0;
                } else if (relative_distance >= distance_lo) {
                    uint32_t ramp = awb_div_q(s,
                        relative_distance - distance_lo,
                        distance_hi - distance_lo);

                    distance_weight = awb_mul_q(s,
                        s->mesh_weight[i],
                        (1U << q) - AWB_MIN(ramp, 1U << q));
                } else {
                    distance_weight = s->mesh_weight[i];
                }
                s->distance_weight[i] = distance_weight;
                weight = awb_mul_q(s,
                    distance_weight, s->pixel_weight[i]);
                refined_rg += awb_mul_q(s,
                    s->ratio[i], weight);
                refined_bg += awb_mul_q(s,
                    s->ratio[bg_offset + i], weight);
                refined_weight += weight;
            }
            if (refined_weight) {
                average_rg_q = awb_div_q(s,
                    refined_rg, refined_weight);
                average_bg_q = awb_div_q(s,
                    refined_bg, refined_weight);
            }
        }
    }

    average_rg_q = AWB_MAX(average_rg_q,
                       s->profile->rg_pos[0] << q);
    average_rg_q = AWB_MIN(average_rg_q,
                       s->profile->rg_pos[14] << q);
    average_bg_q = AWB_MAX(average_bg_q,
                       s->profile->bg_pos[0] << q);
    average_bg_q = AWB_MIN(average_bg_q,
                       s->profile->bg_pos[14] << q);

    result->rg = (average_rg_q + rounding) >> q;
    result->bg = (average_bg_q + rounding) >> q;
    result->active_zones = active_zones;
    {
        uint32_t inverse_ct_q = awb_mesh_q(s,
            s->profile->ct_mesh,
            average_rg_q, average_bg_q);

        result->ct = inverse_ct_q ?
            (uint32_t)awb_u64_div(((uint64_t)1000000U << q) +
                                (inverse_ct_q >> 1), inverse_ct_q) :
            5000U;
    }
    return 0;

no_usable_zone:
    /*
     * Statistics present but no zone inside the CT weight mesh (dark or
     * IR-lit scene, saturated or partial first frame).  Stock JZ_Isp_Awb
     * does not keep the previous gains here: its weighted mean stays 0,
     * which is unity MF over the static WB gains at the 5000 K default
     * (emulated: 0x6c0/0x770 on the sc1a4t IQ).  Do the same instead of
     * failing with -ENODATA and holding stale gains; the caller keeps
     * this result out of the history.
     */
    result->rg = 0;
    result->bg = 0;
    result->ct = 5000U;
    result->active_zones = active_zones;
    return 0;
}

static void awb_history_update(struct qc5_awb_state *s,uint32_t *rg,
                                                 uint32_t *bg,
                                                 uint32_t *ct)
{
    uint32_t window;
    uint32_t start;
    uint32_t i;
    uint32_t weight_sum = 0;
    uint64_t rg_sum = 0;
    uint64_t bg_sum = 0;
    uint64_t ct_sum = 0;

    if (!s->history_count) {
        for (i = 0; i < 15U; ++i) {
            s->history_rg[i] = *rg;
            s->history_bg[i] = *bg;
            s->history_ct[i] = *ct;
        }
        s->history_count = 1U;
    } else {
        memmove(s->history_rg,
                s->history_rg + 1, 14U * sizeof(uint32_t));
        memmove(s->history_bg,
                s->history_bg + 1, 14U * sizeof(uint32_t));
        memmove(s->history_ct,
                s->history_ct + 1, 14U * sizeof(uint32_t));
        s->history_rg[14] = *rg;
        s->history_bg[14] = *bg;
        s->history_ct[14] = *ct;
        s->history_count = AWB_MIN(
            s->history_count + 1U, 15U);
    }

    window = AWB_MIN((uint32_t)s->profile->history,
                 s->history_count);
    window = AWB_MAX(window, 1U);
    start = 15U - window;
    for (i = start; i < 15U; ++i) {
        uint32_t weight = i + 1U;

        rg_sum += (uint64_t)weight * s->history_rg[i];
        bg_sum += (uint64_t)weight * s->history_bg[i];
        ct_sum += (uint64_t)weight * s->history_ct[i];
        weight_sum += weight;
    }
    *rg = (uint32_t)awb_u64_div(rg_sum + (weight_sum >> 1), weight_sum);
    *bg = (uint32_t)awb_u64_div(bg_sum + (weight_sum >> 1), weight_sum);
    *ct = (uint32_t)awb_u64_div(ct_sum + (weight_sum >> 1), weight_sum);
}

static uint32_t awb_ratio_to_mf(struct qc5_awb_state *s,uint32_t ratio)
{
    uint32_t q = s->profile->point;
    uint32_t rounding = 1U << (q - 1U);
    uint64_t reciprocal;

    if (!ratio)
        return 0x100U;
    reciprocal = awb_u64_div(1ULL << (q + 16U), ratio);
    return (uint32_t)((reciprocal + rounding) >> q);
}

