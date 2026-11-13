/* SPDX-License-Identifier: GPL-3.0-only
 * Adapted from opensensor/open-tx-isp 2fb6534b9ecdc8d39a6041debee6455ec2da512f
 * driver/t23/tx_isp_t23_awb_runtime.inc. Pure math, explicit per-instance state.
 */
/*
 * T23 AWB: the cluster stage and the colour-temperature trend of the stock
 * tx-isp-t23.ko (md5 8237acb1), as the open AWB runtime uses them.
 *
 * Stock sources (disassembly of the stock module):
 *  - Tiziano_Awb_Ct_Detect (0x143c8): the 14 x 14 rg/bg histogram of the
 *    zones (Cluster_rgbg_index_num, filled on every run), and, with
 *    _awb_cluster[0] == 1, the cluster stage between the light source
 *    weights and the weighted mean (0x14e8c .. 0x15628)
 *  - JZ_Isp_Awb (0x1f7ec): picks the cluster object, JZ_Isp_Awb_Awbg2reg
 *    (0x1e1d8): the colour temperature trend on the gains
 *  - tiziano_awb_params_refresh (0x1e450): IQ values into the objects
 *
 * Everything here is plain C on 32 bit values (no kernel includes, no 64 bit
 * division) so the host test and the emulator audit
 * (driver/t23/audit/awb_cluster_emu.py) compile exactly what the module runs.
 *
 * Words of the stock cluster object (the user struct has another order, see
 * tx_isp_t23_tuning_ext.h):
 *   [0] ClusterEn (the stage runs only for exactly 1)
 *   [1] radius^2 of the mean shift (squared distance in rg/bg position units)
 *   [2] radius^2 within which two cluster centres merge
 *   [3] radius^2 within which a zone belongs to a cluster
 *   [4] mean shift convergence (largest change of the centre in rg or bg)
 *   [5] mean shift iteration limit
 *   [6], [7] not read by the AWB
 *   [8] ToleranceEn, [9] tolerance_th: with ToleranceEn the automatic gains
 *       follow a change of the summed Q8 gains (mf values) above tolerance_th
 *       (Tiziano_awb_fpga reads the live _awb_cluster for these two; without
 *       ToleranceEn stock uses the IQ's _awb_cof[1])
 */
#ifndef TX_ISP_T23_AWB_CLUSTER_H
#define TX_ISP_T23_AWB_CLUSTER_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

#define T23X_CL_ZONES 225U      /* 15 x 15 AWB zones */
#define T23X_CL_POS 15U         /* rg / bg position table entries */
#define T23X_CL_GRID 14U        /* cells between the positions */
#define T23X_CL_PEAKS 12U       /* histogram peaks seeded per run */
#define T23X_CL_SEEDS 60U       /* 5 seeds per peak */
/* Beyond stock: a seed moves at most this often per run.  Stock stops at
 * the object's iteration limit ([5], any 32 bit value); with convergence 0
 * a seed can swing between two cells for ever. */
#define T23X_CL_MAX_ITER 255U

/*
 * The stock .bss the stage keeps between runs: Cluster_rgbg_index_num,
 * Cluster_rgbg_index_max, Cluster_rgbg_value1, Cluster_rgbg_value2.  Stock
 * never clears them (the histogram only loses the peaks taken, the counts
 * of merged seeds stay 0, a slot keeps its old centre and count when no
 * seed is left); the behaviour depends on it, so it is kept the same.
 */
struct t23x_awb_cluster_state {
	uint32_t index_num[T23X_CL_GRID * T23X_CL_GRID];
	uint32_t idx_col[T23X_CL_PEAKS];
	uint32_t idx_row[T23X_CL_PEAKS];
	uint32_t idx_val[T23X_CL_PEAKS];
	uint32_t v1_rg[T23X_CL_SEEDS];
	uint32_t v1_bg[T23X_CL_SEEDS];
	uint32_t v1_cnt[T23X_CL_SEEDS];
	uint32_t v2_rg[T23X_CL_SEEDS];
	uint32_t v2_bg[T23X_CL_SEEDS];
	uint32_t v2_cnt[T23X_CL_SEEDS];
	uint32_t v2_wgt[T23X_CL_SEEDS];
};

static inline uint32_t t23x_cl_absdiff(uint32_t a, uint32_t b)
{
	return a < b ? b - a : a - b;
}

/* squared distance of two points in position units, 32 bit like stock */
static inline uint32_t t23x_cl_dist2(uint32_t r0, uint32_t b0,
				     uint32_t r1, uint32_t b1)
{
	uint32_t dr = t23x_cl_absdiff(r0, r1);
	uint32_t db = t23x_cl_absdiff(b0, b1);

	return db * db + dr * dr;
}

/*
 * Cell of a value (Q point, clamped to the position range by the caller):
 * 1..14 between pos[i] and pos[i + 1], 15 at the upper end, 0 outside.
 */
static inline uint32_t t23x_cl_bin(uint32_t v, const uint32_t *pos, uint32_t q)
{
	uint32_t i;

	if (v < (pos[0] << q) || (pos[T23X_CL_POS - 1U] << q) < v)
		return 0;
	if (v == (pos[T23X_CL_POS - 1U] << q))
		return T23X_CL_POS;
	for (i = 0; i < T23X_CL_GRID; i++)
		if (v < (pos[i + 1U] << q))
			return i + 1U;
	return T23X_CL_POS;
}

/*
 * The histogram fill of every Tiziano_Awb_Ct_Detect run: each of the 225
 * zones (rg, bg Q values clamped to the position range) adds one to its
 * cell; the upper end belongs to the last cell.
 */
static inline void t23x_awb_cluster_count(struct t23x_awb_cluster_state *st,
					  const uint32_t *zr, const uint32_t *zb,
					  const uint32_t *pos_rg,
					  const uint32_t *pos_bg, uint32_t q)
{
	uint32_t z;

	for (z = 0; z < T23X_CL_ZONES; z++) {
		uint32_t ri = t23x_cl_bin(zr[z], pos_rg, q);
		uint32_t bi = t23x_cl_bin(zb[z], pos_bg, q);

		if (!ri || !bi)
			continue;       /* not reachable for clamped values */
		if (ri > T23X_CL_GRID)
			ri = T23X_CL_GRID;
		if (bi > T23X_CL_GRID)
			bi = T23X_CL_GRID;
		st->index_num[(bi - 1U) * T23X_CL_GRID + (ri - 1U)]++;
	}
}

/*
 * The cluster stage: replaces the zone weights w[] (the mesh weights after
 * the light sources) by weights that follow the biggest clusters of the
 * histogram.  cl is the stock object (ClusterEn must be 1, checked by the
 * caller), q the point position, zr/zb the clamped zone ratios.  Returns 0
 * when no cluster came out (w unchanged), else 1.
 */
static inline int t23x_awb_cluster_weights(struct t23x_awb_cluster_state *st,
					   const uint32_t *cl,
					   const uint32_t *zr, const uint32_t *zb,
					   uint32_t *w, const uint32_t *pos_rg,
					   const uint32_t *pos_bg, uint32_t q)
{
	const uint32_t round = 1U << (q - 1U);
	uint32_t k, i, j, z, row, col, it;
	uint32_t maxw, maxcnt, half, tot, sr, sb;

	/* the 12 biggest cells; a slot only takes a cell bigger than the
	 * count it still holds (stock does not clear it) */
	for (k = 0; k < T23X_CL_PEAKS; k++) {
		for (row = 0; row < T23X_CL_GRID; row++) {
			for (col = 0; col < T23X_CL_GRID; col++) {
				uint32_t v = st->index_num[row * T23X_CL_GRID +
							   col];

				if (v && st->idx_val[k] < v) {
					st->idx_col[k] = col;
					st->idx_row[k] = row;
					st->idx_val[k] = v;
				}
			}
		}
		st->index_num[st->idx_row[k] * T23X_CL_GRID +
			      st->idx_col[k]] = 0;
	}

	/* five seeds per peak: the cell corners and its centre */
	for (k = 0; k < T23X_CL_PEAKS; k++) {
		uint32_t r0 = pos_rg[st->idx_col[k]];
		uint32_t r1 = pos_rg[st->idx_col[k] + 1U];
		uint32_t b0 = pos_bg[st->idx_row[k]];
		uint32_t b1 = pos_bg[st->idx_row[k] + 1U];

		st->v1_rg[5U * k + 0U] = r0;
		st->v1_bg[5U * k + 0U] = b0;
		st->v1_rg[5U * k + 1U] = r0;
		st->v1_bg[5U * k + 1U] = b1;
		st->v1_rg[5U * k + 2U] = (r0 + 1U + r1) >> 1;
		st->v1_bg[5U * k + 2U] = (b0 + 1U + b1) >> 1;
		st->v1_rg[5U * k + 3U] = r1;
		st->v1_bg[5U * k + 3U] = b0;
		st->v1_rg[5U * k + 4U] = r1;
		st->v1_bg[5U * k + 4U] = b1;
	}

	/* a seed equal to an earlier one is dropped */
	for (i = 0; i < T23X_CL_SEEDS; i++) {
		if (!st->v1_rg[i] || !st->v1_bg[i])
			continue;
		for (j = i + 1U; j < T23X_CL_SEEDS; j++) {
			if (st->v1_rg[i] == st->v1_rg[j] &&
			    st->v1_bg[i] == st->v1_bg[j]) {
				st->v1_rg[j] = 0;
				st->v1_bg[j] = 0;
			}
		}
	}

	/*
	 * Mean shift of each seed: the mean of the zones within radius^2 is
	 * the next centre.  The count is stored only when the seed moves
	 * on; a seed that is already at rest (or is stopped by the iteration
	 * limit) keeps its old count (0 when merged, else stale).
	 */
	for (i = 0; i < T23X_CL_SEEDS; i++) {
		if (!st->v1_rg[i] || !st->v1_bg[i])
			continue;
		for (it = 1U;; it++) {
			uint32_t cnt = 0, mr, mb;

			sr = 0;
			sb = 0;
			for (z = 0; z < T23X_CL_ZONES; z++) {
				if (!(cl[1] < t23x_cl_dist2(
						st->v1_rg[i], st->v1_bg[i],
						(round + zr[z]) >> q,
						(round + zb[z]) >> q))) {
					sr += zr[z];
					sb += zb[z];
					cnt++;
				}
			}
			if (!cnt)
				break;
			mr = ((sr + (cnt >> 1)) / cnt) >> q;
			mb = ((sb + (cnt >> 1)) / cnt) >> q;
			if (!(cl[4] < t23x_cl_absdiff(st->v1_rg[i], mr)) &&
			    !(cl[4] < t23x_cl_absdiff(st->v1_bg[i], mb)))
				break;
			if (cl[5] < it || it >= T23X_CL_MAX_ITER)
				break;
			st->v1_rg[i] = mr;
			st->v1_bg[i] = mb;
			st->v1_cnt[i] = cnt;
		}
	}

	/*
	 * Cluster list: slot k takes the seed with the biggest count that
	 * beats the count the slot still holds, then merges every seed
	 * within the merge radius into a count weighted centre and clears
	 * their counts (and its own).  An empty slot ends the list.
	 */
	for (k = 0; k < T23X_CL_SEEDS; k++) {
		for (j = 0; j < T23X_CL_SEEDS; j++) {
			if (st->v1_cnt[j] && st->v2_cnt[k] < st->v1_cnt[j]) {
				st->v2_rg[k] = st->v1_rg[j];
				st->v2_bg[k] = st->v1_bg[j];
				st->v2_cnt[k] = st->v1_cnt[j];
			}
		}
		if (!st->v2_cnt[k])
			break;
		tot = 0;
		sr = 0;
		sb = 0;
		for (j = 0; j < T23X_CL_SEEDS; j++) {
			uint32_t c = st->v1_cnt[j];

			if (!c)
				continue;
			if (cl[2] < t23x_cl_dist2(st->v2_rg[k], st->v2_bg[k],
						  st->v1_rg[j], st->v1_bg[j]))
				continue;
			tot += c;
			sr += c * st->v1_rg[j];
			st->v1_cnt[j] = 0;
			sb += c * st->v1_bg[j];
		}
		if (tot) {
			st->v2_cnt[k] = 0;
			st->v2_rg[k] = (sr + (tot >> 1)) / tot;
			st->v2_bg[k] = (sb + (tot >> 1)) / tot;
		}
	}

	/* per cluster: the zones with a weight within radius^2, and their
	 * mean weight */
	for (k = 0; k < T23X_CL_SEEDS; k++) {
		if (!st->v2_rg[k] || !st->v2_bg[k])
			continue;
		st->v2_cnt[k] = 0;
		st->v2_wgt[k] = 0;
		for (z = 0; z < T23X_CL_ZONES; z++) {
			if (!w[z])
				continue;
			if (cl[3] < t23x_cl_dist2(st->v2_rg[k], st->v2_bg[k],
						  (round + zr[z]) >> q,
						  (round + zb[z]) >> q))
				continue;
			st->v2_cnt[k]++;
			st->v2_wgt[k] += w[z];
		}
	}
	for (k = 0; k < T23X_CL_SEEDS; k++) {
		uint32_t c = st->v2_cnt[k];

		if (c)
			st->v2_wgt[k] = ((c >> 1) + st->v2_wgt[k]) / c;
	}

	/* a cluster's count is scaled by its mean weight against the best */
	maxw = 0;
	for (k = 0; k < T23X_CL_SEEDS; k++)
		if (st->v2_wgt[k] && maxw < st->v2_wgt[k])
			maxw = st->v2_wgt[k];
	if (maxw) {
		for (k = 0; k < T23X_CL_SEEDS; k++) {
			uint32_t c = st->v2_cnt[k];

			if (c)
				st->v2_cnt[k] = (c * st->v2_wgt[k] +
						 (maxw >> 1)) / maxw;
		}
	}
	maxcnt = 0;
	for (k = 0; k < T23X_CL_SEEDS; k++)
		if (st->v2_cnt[k] && maxcnt < st->v2_cnt[k])
			maxcnt = st->v2_cnt[k];
	if (!maxcnt)
		return 0;

	/* every zone takes the weight of the biggest cluster it lies in
	 * (a zone in none counts as a cluster of 1) */
	half = maxcnt >> 1;
	for (z = 0; z < T23X_CL_ZONES; z++) {
		uint32_t best = 0;

		for (k = 1; k <= T23X_CL_SEEDS; k++) {
			if (!st->v2_rg[k - 1U] || !st->v2_bg[k - 1U])
				continue;
			if (cl[3] < t23x_cl_dist2(st->v2_rg[k - 1U],
						  st->v2_bg[k - 1U],
						  (round + zr[z]) >> q,
						  (round + zb[z]) >> q))
				continue;
			if (!best || st->v2_cnt[best - 1U] <
				     st->v2_cnt[k - 1U])
				best = k;
		}
		if (best)
			w[z] = (st->v2_cnt[best - 1U] * w[z] + half) / maxcnt;
		else
			w[z] = (half + w[z]) / maxcnt;
	}
	return 1;
}

/* ---- object selection, trend, tolerance ---------------------------------- */

/*
 * The object the AWB reads (JZ_Isp_Awb for the cluster, Awbg2reg for the
 * trend): status[0] == 1 the IQ copy (api_para); status[0] == 2, or a user
 * value pending (status[1] == 2), the live object; else the IQ copy.
 */
static inline const uint32_t *t23x_awb_obj_pick(const uint32_t *obj,
						const uint32_t *api_para,
						const uint32_t *status)
{
	if (status[0] == 1U)
		return api_para;
	if (status[0] == 2U || status[1] == 2U)
		return obj;
	return api_para;
}

/*
 * JZ_Isp_Awb_Awbg2reg: the gain the AWB writes, g = (mf * wb_static) >> q
 * from the caller.  Q = g << 2; with the trend enabled ([0] == 1) the offset
 * of the colour temperature range (>= 5000 K: [1], [2]; <= 3000 K: [5],
 * [6]; else [3], [4]) minus 1024 is added (32 bit wrap like stock, a result
 * below 0 reads as huge and lands on the limit); at most 0x3fff.
 */
static inline uint32_t t23x_awb_gain_trend(const uint32_t *tr, uint32_t ct,
					   uint32_t g, int blue)
{
	uint32_t x = g << 2;

	if (tr[0] == 1U) {
		uint32_t off;

		if (ct >= 5000U)
			off = tr[1U + (blue ? 1U : 0U)];
		else if (ct <= 3000U)
			off = tr[5U + (blue ? 1U : 0U)];
		else
			off = tr[3U + (blue ? 1U : 0U)];
		x += off - 1024U;
	}
	return x < 0x4000U ? x : 0x3fffU;
}

#endif /* TX_ISP_T23_AWB_CLUSTER_H */
