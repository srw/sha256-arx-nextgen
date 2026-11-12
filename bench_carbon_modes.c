// bench_carbon_modes.c: Exact 10-target & 50-target measurements + calibrated log2(SHA19-eq)
// for Carbon-Dense (1024 MB, KER_BITS=4 u32) and Carbon-Lite (512 MB, KER_BITS=4 u16)
// on the IDENTICAL 10-target & 50-target suites!
#include <pthread.h>
#include <stdatomic.h>
#include <arm_neon.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>

#define main carbon_unused_main
#include "exp_r19_carbon_lab.c"
#undef main

// KER_BITS = 4 (2^28 = 268,435,456 buckets):
// Mode configurations:
//   - KB=4 (Default): BUCKETS = 2^28, ht_dense = 1,024 MB, ht_lite = 512 MB
//   - KB=5 (Theorem 42): BUCKETS = 2^27, ht_dense = 512 MB, ht_lite = 256 MB
#ifndef KB
#define KB           4
#endif
#define KM           ((1u << KB) - 1u)
#define BUCKETS      (1ULL << (32 - KB))
#define N_UHI        (1u << (14 - KB))
#define BLK_SHIFT    (11 - KB)
#define BLK_SIZE     (N_UHI / 8u)

static u32 *ht_dense;
static u16 *ht_lite;
static int  g_use_lite = 0;

static void *build_kb4_worker(void *arg) {
    tbuild_t *tb = (tbuild_t *)arg;
    const u32 tid = (u32)tb->tid;
    const u32 n_uhi = N_UHI;
    u32 diff_top_tbl[1024], s0_top_tbl[1024];
    for (u32 uhi = 0; uhi < n_uhi; uhi++) {
        u32 ut = uhi << (18 + KB);
        diff_top_tbl[uhi] = ut;
        s0_top_tbl[uhi]   = (ut >> 7) ^ (ut >> 18) ^ (ut << 14) ^ (ut >> 3);
    }
    int lite = (g_use_lite == 1);
    for (u32 ulo18 = 1u; ulo18 < (1u << 18); ulo18++) {
        u32 stored_val = ulo18 & 0xffffu;
        if (stored_val == 0u) continue;
        u32 u_ker   = ((ulo18 ^ (ulo18 >> 3) ^ (ulo18 >> 7)) & KM) << 18;
        u32 base_u  = ulo18 | u_ker;
        u32 s0_base = s0(base_u);
        u32 c_hi3   = s0_base >> 29;
        u32 blk0    = ((c_hi3 - tid) & 7u) << BLK_SHIFT;
        u32 blk1    = ((c_hi3 - tid - 1u) & 7u) << BLK_SHIFT;

        if (!lite) {
            for (u32 uhi = blk0; uhi < blk0 + BLK_SIZE; uhi++) {
                u32 y = (s0_base ^ s0_top_tbl[uhi]) - (base_u | diff_top_tbl[uhi]);
                if ((y >> 29) == tid) {
                    u32 idx = y >> KB;
                    ht_dense[idx] = (ht_dense[idx] << 16) | stored_val;
                }
            }
            for (u32 uhi = blk1; uhi < blk1 + BLK_SIZE; uhi++) {
                u32 y = (s0_base ^ s0_top_tbl[uhi]) - (base_u | diff_top_tbl[uhi]);
                if ((y >> 29) == tid) {
                    u32 idx = y >> KB;
                    ht_dense[idx] = (ht_dense[idx] << 16) | stored_val;
                }
            }
        } else {
            for (u32 uhi = blk0; uhi < blk0 + BLK_SIZE; uhi++) {
                u32 y = (s0_base ^ s0_top_tbl[uhi]) - (base_u | diff_top_tbl[uhi]);
                if ((y >> 29) == tid) ht_lite[y >> KB] = (u16)stored_val;
            }
            for (u32 uhi = blk1; uhi < blk1 + BLK_SIZE; uhi++) {
                u32 y = (s0_base ^ s0_top_tbl[uhi]) - (base_u | diff_top_tbl[uhi]);
                if ((y >> 29) == tid) ht_lite[y >> KB] = (u16)stored_val;
            }
        }
    }
    return NULL;
}

static void *chain_worker_kb4(void *arg) {
    chain_worker_t *cw = (chain_worker_t *)arg;
    int lite = (g_use_lite == 1);
    u32 T16 = cw->base_tr->T16, T17 = cw->base_tr->T17, T18 = cw->base_tr->T18;
    u32 a5 = cw->base_tr->a5, D = cw->base_tr->D;

    u32 deltas[16], D_bads[16], F_targets[16];
    int n_deltas = 0;
    if (((a5 & D) & KM) == 0u) deltas[n_deltas++] = a5 & D;
    for (int dist = 1; dist <= 3; dist++) {
        u32 subD = 0;
        do {
            if (((subD & KM) == 0u) && popc((a5 ^ subD) & D) == dist && n_deltas < 16) deltas[n_deltas++] = subD;
            subD = (subD - D) & D;
        } while (subD != 0);
    }
    for (int d_idx = 0; d_idx < n_deltas; d_idx++) {
        D_bads[d_idx]    = (a5 ^ deltas[d_idx]) & D;
        F_targets[d_idx] = (a5 & ~D) | deltas[d_idx];
    }

    u32 T16_folded     = T16 + C_t16_folded;
    u32 C_G1_minus_T16 = C_W0 - T16;
    u32 notD_lo16      = (~D) & 0xffffu;
    u32 D_hi16         = D & 0xffff0000u;

    const uint32x4_t v_flo0   = vld1q_u32(&FLO_MAJ_SUB[0]);
    const uint32x4_t v_flo4   = vld1q_u32(&FLO_MAJ_SUB[4]);
    const uint32x4_t v_flo8   = vld1q_u32(&FLO_MAJ_SUB[8]);
    const uint32x4_t v_flo12  = vld1q_u32(&FLO_MAJ_SUB[12]);
    const uint32x4_t v_s0l0   = vld1q_u32(&S0_LO4[0]);
    const uint32x4_t v_s0l4   = vld1q_u32(&S0_LO4[4]);
    const uint32x4_t v_s0l8   = vld1q_u32(&S0_LO4[8]);
    const uint32x4_t v_s0l12  = vld1q_u32(&S0_LO4[12]);
    const uint32x4_t v_A0_c   = vdupq_n_u32(A0_const);
    const uint32x4_t v_E0_c   = vdupq_n_u32(E0_const);
    const uint32x4_t v_CG2_b  = vdupq_n_u32(C_G2_base);
    const uint32x4_t v_T17_v  = vdupq_n_u32(T17);
    const uint32x4_t v_m_b0   = vdupq_n_u32(0x00010000u);
    const uint32x4_t v_m_b12  = vdupq_n_u32(0x00060000u);
    const uint32x4_t v_m_hi13 = vdupq_n_u32(0xfff80000u);
    const uint32x4_t v_kmask  = vdupq_n_u32(KM);

    u32 step = ((u32)cw->tid << 18) ^ ((u32)cw->attempt * 0x9E3779B1u);
    u64 local_chains = 0;

    u32 ring_t16[2][64];
    u32 ring_a1[2], ring_e1[2], ring_s0[2];

#if KB == 4
#define COMPUTE_DIFF_DELTA \
            u32 diff     = (s0_0 ^ (s0_0 - t16_0)) & 15u; \
            u32 delta_a1 = ((diff & 7u) << 22) | (((diff >> 3) & 1u) << 16);
#elif KB == 5
#define COMPUTE_DIFF_DELTA \
            u32 diff     = (s0_0 ^ (s0_0 - t16_0)) & 31u; \
            u32 delta_a1 = ((diff & 7u) << 22) | (((diff >> 3) & 1u) << 16) | (((diff >> 4) & 1u) << 17);
#endif

    #define GEN_CANDIDATE_STEP(slot) do { \
        while (!atomic_load_explicit(cw->solved, memory_order_relaxed)) { \
            step++; \
            u32 idx21    = (step * 1332533u + (u32)cw->tid * 262147u) & 0x001fffffu; \
            u32 a1_0     = (idx21 & 0x0000ffffu) | ((idx21 >> 16) << 25); \
            u32 e1_0     = a1_0 + C_E1; \
            u32 s0_0     = S0(a1_0); \
            u32 t16_0    = T16 - (a1_0 + C_W0) + C_G1_base + s0_0 + MAJ(a1_0, A0_const, Am1_const) + S1(e1_0) + CH(e1_0, E0_const, Em1_const); \
            COMPUTE_DIFF_DELTA \
            if (((e1_0 & 0x01ffffffu) + delta_a1 + (15u << 18)) >= 0x02000000u) continue; \
            u32 a1_base  = a1_0 | delta_a1; \
            u32 e1_base       = a1_base + C_E1; \
            u32 s0_a1_base    = S0(a1_base); \
            u32 s1_e1_base    = S1(e1_base & 0x3e03ffffu); \
            u32 e1_mid7_0     = (e1_base >> 18) & 0x7fu; \
            u32 e1_hi2_0      = e1_base >> 30; \
            u32 ch1_base_lo18 = (Em1_const ^ (e1_base & CH1_XOR)) & 0x3e03ffffu; \
            u32 t16_base_f    = T16_folded - a1_base + (a1_base & MAJ1_XOR) + ch1_base_lo18; \
            uint32x4_t v_t16_b = vdupq_n_u32(t16_base_f); \
            uint32x4_t v_s0_b  = vdupq_n_u32(s0_a1_base); \
            uint32x4_t v_s1_b  = vdupq_n_u32(s1_e1_base); \
            uint32x4_t v_m0  = vaddq_u32(v_t16_b, vaddq_u32(v_flo0,  vld1q_u32(&CH1_MID7[e1_mid7_0 + 0]))); \
            uint32x4_t v_m4  = vaddq_u32(v_t16_b, vaddq_u32(v_flo4,  vld1q_u32(&CH1_MID7[e1_mid7_0 + 4]))); \
            uint32x4_t v_m8  = vaddq_u32(v_t16_b, vaddq_u32(v_flo8,  vld1q_u32(&CH1_MID7[e1_mid7_0 + 8]))); \
            uint32x4_t v_m12 = vaddq_u32(v_t16_b, vaddq_u32(v_flo12, vld1q_u32(&CH1_MID7[e1_mid7_0 + 12]))); \
            uint32x4_t v_s0_l0  = veorq_u32(v_s0_b, v_s0l0); \
            uint32x4_t v_s0_l4  = veorq_u32(v_s0_b, v_s0l4); \
            uint32x4_t v_s0_l8  = veorq_u32(v_s0_b, v_s0l8); \
            uint32x4_t v_s0_l12 = veorq_u32(v_s0_b, v_s0l12); \
            uint32x4_t v_s1_l0  = veorq_u32(v_s1_b, vld1q_u32(&S1_MID7[e1_mid7_0 + 0])); \
            uint32x4_t v_s1_l4  = veorq_u32(v_s1_b, vld1q_u32(&S1_MID7[e1_mid7_0 + 4])); \
            uint32x4_t v_s1_l8  = veorq_u32(v_s1_b, vld1q_u32(&S1_MID7[e1_mid7_0 + 8])); \
            uint32x4_t v_s1_l12 = veorq_u32(v_s1_b, vld1q_u32(&S1_MID7[e1_mid7_0 + 12])); \
            for (u32 f_hi = 0; f_hi < 4u; f_hi++) { \
                u32 f_hi_sh  = f_hi << 30; \
                u32 e1_hi    = e1_base + f_hi_sh; \
                u32 hi_delta = (f_hi_sh & MAJ1_XOR) - f_hi_sh + ((Em1_const ^ (e1_hi & CH1_XOR)) & 0xc0000000u); \
                uint32x4_t v_hid   = vdupq_n_u32(hi_delta); \
                uint32x4_t v_s0_hi = vdupq_n_u32(S0_HI2[f_hi]); \
                uint32x4_t v_s1_hi = vdupq_n_u32(S1_HI2[(e1_hi2_0 + f_hi) & 3u]); \
                u32 *dst = &ring_t16[slot][f_hi << 4]; \
                vst1q_u32(&dst[0],  vaddq_u32(vaddq_u32(v_m0,  v_hid), vaddq_u32(veorq_u32(v_s0_l0,  v_s0_hi), veorq_u32(v_s1_l0,  v_s1_hi)))); \
                vst1q_u32(&dst[4],  vaddq_u32(vaddq_u32(v_m4,  v_hid), vaddq_u32(veorq_u32(v_s0_l4,  v_s0_hi), veorq_u32(v_s1_l4,  v_s1_hi)))); \
                vst1q_u32(&dst[8],  vaddq_u32(vaddq_u32(v_m8,  v_hid), vaddq_u32(veorq_u32(v_s0_l8,  v_s0_hi), veorq_u32(v_s1_l8,  v_s1_hi)))); \
                vst1q_u32(&dst[12], vaddq_u32(vaddq_u32(v_m12, v_hid), vaddq_u32(veorq_u32(v_s0_l12, v_s0_hi), veorq_u32(v_s1_l12, v_s1_hi)))); \
            } \
            ring_a1[slot] = a1_base; \
            ring_e1[slot] = e1_base; \
            ring_s0[slot] = s0_a1_base; \
            if (!lite) { \
                for (int f = 0; f < 64; f += 4) { \
                    __builtin_prefetch(&ht_dense[ring_t16[slot][f]   >> KB], 0, 0); \
                    __builtin_prefetch(&ht_dense[ring_t16[slot][f+1] >> KB], 0, 0); \
                    __builtin_prefetch(&ht_dense[ring_t16[slot][f+2] >> KB], 0, 0); \
                    __builtin_prefetch(&ht_dense[ring_t16[slot][f+3] >> KB], 0, 0); \
                } \
            } else { \
                for (int f = 0; f < 64; f += 4) { \
                    __builtin_prefetch(&ht_lite[ring_t16[slot][f]   >> KB], 0, 0); \
                    __builtin_prefetch(&ht_lite[ring_t16[slot][f+1] >> KB], 0, 0); \
                    __builtin_prefetch(&ht_lite[ring_t16[slot][f+2] >> KB], 0, 0); \
                    __builtin_prefetch(&ht_lite[ring_t16[slot][f+3] >> KB], 0, 0); \
                } \
            } \
            break; \
        } \
    } while (0)

    // Prime slot 0
    GEN_CANDIDATE_STEP(0);
    int curr_slot = 0;

    while (!atomic_load_explicit(cw->solved, memory_order_relaxed) && local_chains < 1048576ULL) {
        int next_slot = curr_slot ^ 1;
        GEN_CANDIDATE_STEP(next_slot);

        u32 *all_t16    = ring_t16[curr_slot];
        u32 a1_base     = ring_a1[curr_slot];
        u32 e1_base     = ring_e1[curr_slot];
        u32 s0_a1_base  = ring_s0[curr_slot];
        local_chains += 64;

        u32 hit_a1[64], hit_e1[64], hit_t16[64], hit_G1[64], hit_e2b[64];
        u16 w1_ulo[140];
        u8  w1_h[140];
        int n_w1 = 0, n_hits = 0;

        for (int f = 0; f < 64; f++) {
            u32 t16  = all_t16[f];
            u32 pack = lite ? (u32)ht_lite[t16 >> KB] : ht_dense[t16 >> KB];
            if (!pack) continue;
            int h = n_hits++;
            u32 f_lo    = f & 15u, f_hi = f >> 4;
            u32 a1      = a1_base | (f_lo << 18) | (f_hi << 30);
            u32 e1      = e1_base + (f_lo << 18) + (f_hi << 30);
            u32 sm1_var = (s0_a1_base ^ S0_HI2[f_hi] ^ S0_LO4[f_lo]) + (a1 & MAJ1_XOR);
            hit_t16[h]  = t16; hit_a1[h] = a1; hit_e1[h] = e1;
            hit_G1[h]   = t16 + a1 + C_G1_minus_T16;
            hit_e2b[h]  = Am2_folded - sm1_var;

            w1_ulo[n_w1] = (u16)(pack & 0xffffu);
            w1_h[n_w1]   = (u8)h;
            n_w1++;
            if (!lite && (pack >> 16) != 0u) {
                w1_ulo[n_w1] = (u16)(pack >> 16);
                w1_h[n_w1]   = (u8)h;
                n_w1++;
            }
        }
        for (int p = 0; p < 4; p++) { w1_ulo[n_w1 + p] = 0; w1_h[n_w1 + p] = 0; }

        #define VROTR_NEON(v, n) vsriq_n_u32(vshlq_n_u32((v), 32 - (n)), (v), (n))
        #define VS0_NEON(v) veorq_u32(veorq_u32(VROTR_NEON((v), 2), VROTR_NEON((v), 13)), VROTR_NEON((v), 22))
        #define VS1_NEON(v) veorq_u32(veorq_u32(VROTR_NEON((v), 6), VROTR_NEON((v), 11)), VROTR_NEON((v), 25))

        for (int k = 0; k < n_w1; k += 4) {
            u32 h0 = w1_h[k], h1 = w1_h[k+1], h2 = w1_h[k+2], h3 = w1_h[k+3];
            uint32x4_t v_ulo1 = vmovl_u16(vld1_u16(&w1_ulo[k]));
            uint32x4_t v_t16  = {hit_t16[h0], hit_t16[h1], hit_t16[h2], hit_t16[h3]};
            uint32x4_t v_G1   = {hit_G1[h0],  hit_G1[h1],  hit_G1[h2],  hit_G1[h3]};
            uint32x4_t v_e2b  = {hit_e2b[h0], hit_e2b[h1], hit_e2b[h2], hit_e2b[h3]};
            uint32x4_t v_a1   = {hit_a1[h0],  hit_a1[h1],  hit_a1[h2],  hit_a1[h3]};
            uint32x4_t v_e1   = {hit_e1[h0],  hit_e1[h1],  hit_e1[h2],  hit_e1[h3]};

            uint32x4_t v_z1   = vaddq_u32(v_ulo1, v_t16);
            uint32x4_t v_v1   = veorq_u32(veorq_u32(v_z1, vshrq_n_u32(v_ulo1, 7)), veorq_u32(vshlq_n_u32(v_ulo1, 14), vshrq_n_u32(v_ulo1, 3)));
            uint32x4_t v_b0   = vandq_u32(veorq_u32(veorq_u32(vshlq_n_u32(v_v1, 15), vshlq_n_u32(v_v1, 11)), v_v1), v_m_b0);
            uint32x4_t v_b12  = vandq_u32(veorq_u32(vshlq_n_u32(v_v1, 14), vshlq_n_u32(v_v1, 3)), v_m_b12);
            uint32x4_t v_T1   = vorrq_u32(vorrq_u32(v_b0, v_b12), vandq_u32(vshlq_n_u32(v_v1, 18), v_m_hi13));
            uint32x4_t v_W1   = vorrq_u32(v_ulo1, veorq_u32(veorq_u32(v_T1, vshlq_n_u32(v_T1, 11)), vshlq_n_u32(v_b0, 15)));

            uint32x4_t v_a2   = vaddq_u32(v_W1, v_G1);
            uint32x4_t v_e2   = vaddq_u32(v_a2, v_e2b);
            uint32x4_t v_maj2 = vbslq_u32(veorq_u32(v_a1, v_A0_c), v_a2, v_a1);
            uint32x4_t v_ch2  = vbslq_u32(v_e2, v_e1, v_E0_c);
            uint32x4_t v_sm2  = vaddq_u32(VS0_NEON(v_a2), v_maj2);
            uint32x4_t v_G2   = vaddq_u32(vaddq_u32(v_CG2_b, v_sm2), vaddq_u32(VS1_NEON(v_e2), v_ch2));
            uint32x4_t v_t17b = vsubq_u32(vaddq_u32(v_T17_v, v_G2), v_W1);
            uint32x4_t v_rem  = vandq_u32(v_t17b, v_kmask);

            if (__builtin_expect(vminvq_u32(v_rem) == 0u, 0)) {
                u32 t17b_arr[4], G2_arr[4], sm2_arr[4], W1_arr[4], a2_arr[4], e2_arr[4];
                vst1q_u32(t17b_arr, v_t17b);
                vst1q_u32(G2_arr,   v_G2);
                int hop3_stored = 0;

                int lim = (k + 4 <= n_w1) ? 4 : (n_w1 - k);
                for (int lane = 0; lane < lim; lane++) {
                    u32 t17_base = t17b_arr[lane];
                    if ((t17_base & KM) != 0u) continue;
                    u32 G2 = G2_arr[lane];

                    if (!lite) {
                        for (int d = 0; d < n_deltas; d++)
                            __builtin_prefetch(&ht_dense[(t17_base - deltas[d]) >> KB], 0, 0);
                    }

                    for (int d_idx = 0; d_idx < n_deltas; d_idx++) {
                        u32 delta = deltas[d_idx];
                        u32 t17   = t17_base - delta;
                        u32 pack2 = lite ? (u32)ht_lite[t17 >> KB] : ht_dense[t17 >> KB];
                        if (!pack2) continue;

                        u32 D_bad    = D_bads[d_idx];
                        u32 F_target = F_targets[d_idx];

                        for (u32 p2 = pack2; p2 != 0u; p2 >>= 16) {
                            u32 ulo2  = p2 & 0xffffu;
                            u32 a3_lo = (ulo2 + G2) & 0xffffu;
                            if (d_idx > 0 && ((a3_lo ^ delta) & D_bad & 0xffffu) != 0u) continue;
                            u32 free_lo = (a5 ^ a3_lo) & notD_lo16;
                            if (!free_lo) continue;
                            int n_free_lo = popc(free_lo);

                            u32 W2 = rec_u_z(ulo2, ulo2 + t17);
                            u32 a3 = W2 + G2;
                            if (d_idx > 0 && ((a3 ^ delta) & D_bad) != 0u) continue;

                            u32 M_hi = ((~(a5 ^ a3)) | D_hi16) & 0xffff0000u;
                            if (popc(M_hi) > MAX_FORCED_HI_CARBON[n_free_lo]) continue;

                            if (!hop3_stored) {
                                vst1q_u32(sm2_arr, v_sm2); vst1q_u32(W1_arr, v_W1);
                                vst1q_u32(a2_arr,  v_a2);  vst1q_u32(e2_arr, v_e2);
                                hop3_stored = 1;
                            }
                            u32 h_lane = w1_h[k + lane];
                            u32 a1 = hit_a1[h_lane], e1 = hit_e1[h_lane];
                            u32 t16 = hit_t16[h_lane], G1 = hit_G1[h_lane];
                            u32 W1 = W1_arr[lane], a2 = a2_arr[lane], e2 = e2_arr[lane];
                            u32 e3_base = Am1_const - sm2_arr[lane];

                            u32 M = M_hi | ((~free_lo) & 0xffffu);
                            u32 e3 = a3 + e3_base;
                            u32 sm3 = S0(a3) + MAJ(a3, a2, a1);
                            u32 G3 = C_G3_base + sm3 + S1(e3) + CH(e3, e2, e1);
                            u32 t18 = T18 + G3 - W2;

                            if ((t18 & KM) == 0u) {
                                u32 pack3 = (lite == 1) ? (u32)ht_lite[t18 >> KB] : ht_dense[t18 >> KB];
                                for (u32 p3 = pack3; p3 != 0u; p3 >>= 16) {
                                    u32 ulo3 = p3 & 0xffffu;
                                    if (lite == 2) {
                                        // Pass 2 Upgrade: Mod-4 Bijective Pre-Filter (Theorem 1.1)
                                        if ((((ulo3 + G3) ^ F_target) & M & 3u) != 0u) continue;
                                    }
                                    if ((((ulo3 + G3) ^ F_target) & M & 0xffffu) == 0u) {
                                        u32 W3 = rec_u_z(ulo3, ulo3 + t18), a4 = W3 + G3;
                                        if (((a4 ^ F_target) & M) == 0u) {
                                            int expected = 0;
                                            if (atomic_compare_exchange_strong(cw->solved, &expected, 1)) {
                                                u32 W0 = a1 + C_W0;
                                                u32 A[R+OFF+2]; memcpy(A, cw->base_A, sizeof(A));
                                                A[1+OFF] = a1; A[2+OFF] = a2; A[3+OFF] = a3; A[4+OFF] = a4;
                                                SolverTrace *tr = cw->out_tr; *tr = *cw->base_tr;
                                                tr->a1 = a1; tr->W0 = W0; tr->G1 = G1; tr->t16 = t16; tr->W1 = W1; tr->a2 = a2;
                                                tr->G2 = G2; tr->t17 = t17; tr->W2 = W2; tr->a3 = a3;
                                                tr->G3 = G3; tr->t18 = t18; tr->W3 = W3; tr->a4 = a4;
                                                for (int t = 0; t < 16; t++) tr->Wm[t] = Wt_arr(A, t);
                                                compress(tr->Wm, R, tr->H_out);
                                            }
                                            goto done;
                                        }
                                    }
                                }
                            }

                            if (n_free_lo == 1) {
                                u32 base_w3_lo = ((F_target & M & 0xffffu) - G3) & 0xffffu;
                                for (int s = 0; s < 2; s++) {
                                    u32 w3_lo = (base_w3_lo + (s ? free_lo : 0u)) & 0xffffu;
                                    u32 W3 = rec_u_z(w3_lo, w3_lo + t18), a4 = W3 + G3;
                                    if (((a4 ^ F_target) & M) == 0u && Qf(W3) == t18) {
                                        int expected = 0;
                                        if (atomic_compare_exchange_strong(cw->solved, &expected, 1)) {
                                            u32 W0 = a1 + C_W0;
                                            u32 A[R+OFF+2]; memcpy(A, cw->base_A, sizeof(A));
                                            A[1+OFF] = a1; A[2+OFF] = a2; A[3+OFF] = a3; A[4+OFF] = a4;
                                            SolverTrace *tr = cw->out_tr; *tr = *cw->base_tr;
                                            tr->a1 = a1; tr->W0 = W0; tr->G1 = G1; tr->t16 = t16; tr->W1 = W1; tr->a2 = a2;
                                            tr->G2 = G2; tr->t17 = t17; tr->W2 = W2; tr->a3 = a3;
                                            tr->G3 = G3; tr->t18 = t18; tr->W3 = W3; tr->a4 = a4;
                                            for (int t = 0; t < 16; t++) tr->Wm[t] = Wt_arr(A, t);
                                            compress(tr->Wm, R, tr->H_out);
                                        }
                                        goto done;
                                    }
                                }
                            } else if (n_free_lo <= 11) {
                                u32 base_w3_lo = ((F_target & M & 0xffffu) - G3) & 0xffffu;
                                u32 fb0 = free_lo & (0u - free_lo), rem1 = free_lo ^ fb0;
                                u32 fb1 = rem1 & (0u - rem1), free_rest = rem1 ^ fb1;
                                const uint32x4_t v_sub_lo4  = {0u, fb0, fb1, fb0 | fb1};
                                const uint32x4_t v_ffff     = vdupq_n_u32(0xffffu);
                                const uint32x4_t v_8000     = vdupq_n_u32(0x00008000u);
                                const uint32x4_t v_e0000000 = vdupq_n_u32(0xe0000000u);
                                const uint32x4_t v_t18_v    = vdupq_n_u32(t18);
                                const uint32x4_t v_G3_v     = vdupq_n_u32(G3);
                                const uint32x4_t v_Ft_v     = vdupq_n_u32(F_target);
                                const uint32x4_t v_M_v      = vdupq_n_u32(M);
                                u32 sub = 0;
                                do {
                                    uint32x4_t v_w3_lo = vandq_u32(vaddq_u32(vdupq_n_u32(base_w3_lo + sub), v_sub_lo4), v_ffff);
                                    uint32x4_t v_zlo   = vaddq_u32(v_w3_lo, v_t18_v);
                                    uint32x4_t v_v1    = veorq_u32(veorq_u32(v_zlo, vshrq_n_u32(v_w3_lo, 7)),
                                                                   veorq_u32(vshlq_n_u32(v_w3_lo, 14), vshrq_n_u32(v_w3_lo, 3)));
                                    uint32x4_t v_sv1   = veorq_u32(veorq_u32(vshlq_n_u32(v_v1, 15), vshlq_n_u32(v_v1, 11)), v_v1);
                                    uint32x4_t v_b0    = vandq_u32(v_sv1, v_m_b0);
                                    uint32x4_t v_b12   = vandq_u32(veorq_u32(vshlq_n_u32(v_v1, 14), vshlq_n_u32(v_v1, 3)), v_m_b12);
                                    uint32x4_t v_T     = vorrq_u32(vorrq_u32(v_b0, v_b12), vandq_u32(vshlq_n_u32(v_v1, 18), v_m_hi13));
                                    uint32x4_t v_W3l27 = vorrq_u32(v_w3_lo, v_T);
                                    uint32x4_t v_W3    = veorq_u32(v_W3l27, veorq_u32(vshlq_n_u32(v_T, 11), vshlq_n_u32(v_b0, 15)));
                                    uint32x4_t v_a4    = vaddq_u32(v_W3, v_G3_v);
                                    uint32x4_t v_s0h3  = vshlq_n_u32(veorq_u32(vshrq_n_u32(v_W3l27, 4), vshrq_n_u32(v_W3l27, 15)), 29);
                                    uint32x4_t v_err   = vorrq_u32(vandq_u32(v_sv1, v_8000),
                                                         vorrq_u32(vandq_u32(veorq_u32(v_a4, v_Ft_v), v_M_v),
                                                                   vandq_u32(veorq_u32(vaddq_u32(v_W3, v_t18_v), v_s0h3), v_e0000000)));
                                    if (__builtin_expect(vminvq_u32(v_err) == 0u, 0)) {
                                        u32 W3_cand[4], err_cand[4];
                                        vst1q_u32(W3_cand, v_W3); vst1q_u32(err_cand, v_err);
                                        for (int q = 0; q < 4; q++) {
                                            if (err_cand[q] == 0u && Qf(W3_cand[q]) == t18) {
                                                u32 W3 = W3_cand[q], a4 = W3 + G3;
                                                int expected = 0;
                                                if (atomic_compare_exchange_strong(cw->solved, &expected, 1)) {
                                                    u32 W0 = a1 + C_W0;
                                                    u32 A[R+OFF+2]; memcpy(A, cw->base_A, sizeof(A));
                                                    A[1+OFF] = a1; A[2+OFF] = a2; A[3+OFF] = a3; A[4+OFF] = a4;
                                                    SolverTrace *tr = cw->out_tr; *tr = *cw->base_tr;
                                                    tr->a1 = a1; tr->W0 = W0; tr->G1 = G1; tr->t16 = t16; tr->W1 = W1; tr->a2 = a2;
                                                    tr->G2 = G2; tr->t17 = t17; tr->W2 = W2; tr->a3 = a3;
                                                    tr->G3 = G3; tr->t18 = t18; tr->W3 = W3; tr->a4 = a4;
                                                    for (int t = 0; t < 16; t++) tr->Wm[t] = Wt_arr(A, t);
                                                    compress(tr->Wm, R, tr->H_out);
                                                }
                                                goto done;
                                            }
                                        }
                                    }
                                    sub = (sub - free_rest) & free_rest;
                                } while (sub != 0);
                            }
                        }
                    }
                }
            }
        }
        curr_slot ^= 1;
        #undef VROTR_NEON
        #undef VS0_NEON
        #undef VS1_NEON
    }
done:
    atomic_fetch_add(cw->total_chains, local_chains);
    return NULL;
}

// Calibrate 8-core SHA-19 reference throughput (both scalar C compress and 4-wide ARM64 NEON SHA-19)
static atomic_uint_fast64_t g_ref_sink = 0;
static void *sha19_scalar_ref_worker(void *arg) {
    u32 tid = (u32)(uintptr_t)arg;
    u32 Wm[16] = {0}, out[8];
    Wm[0] = 0x61626380u ^ (tid << 24);
    Wm[15] = 440u;
    u32 acc = 0;
    for (u32 i = 0; i < (1u << 20); i++) {
        Wm[1] = i;
        compress(Wm, R, out);
        acc ^= out[0];
    }
    atomic_fetch_add(&g_ref_sink, acc);
    return NULL;
}

static void *sha19_neon_ref_worker(void *arg) {
    u32 tid = (u32)(uintptr_t)arg;
    #define VROR(v, n) vsriq_n_u32(vshlq_n_u32((v), 32 - (n)), (v), (n))
    #define VS0(v) veorq_u32(veorq_u32(VROR((v), 2), VROR((v), 13)), VROR((v), 22))
    #define VS1(v) veorq_u32(veorq_u32(VROR((v), 6), VROR((v), 11)), VROR((v), 25))
    #define Vs0(v) veorq_u32(veorq_u32(VROR((v), 7), VROR((v), 18)), vshrq_n_u32((v), 3))
    #define Vs1(v) veorq_u32(veorq_u32(VROR((v), 17), VROR((v), 19)), vshrq_n_u32((v), 10))
    uint32x4_t W[19];
    for (int t = 0; t < 16; t++) W[t] = vdupq_n_u32(0u);
    W[0] = (uint32x4_t){0u + tid*4u, 1u + tid*4u, 2u + tid*4u, 3u + tid*4u};
    W[15] = vdupq_n_u32(440u);
    uint32x4_t acc = vdupq_n_u32(0u);
    for (u32 i = 0; i < (1u << 20); i++) {
        W[1] = vdupq_n_u32(i);
        for (int t = 16; t < 19; t++)
            W[t] = vaddq_u32(vaddq_u32(Vs1(W[t-2]), W[t-7]), vaddq_u32(Vs0(W[t-15]), W[t-16]));
        uint32x4_t a=vdupq_n_u32(IV[0]), b=vdupq_n_u32(IV[1]), c=vdupq_n_u32(IV[2]), d=vdupq_n_u32(IV[3]);
        uint32x4_t e=vdupq_n_u32(IV[4]), f=vdupq_n_u32(IV[5]), g=vdupq_n_u32(IV[6]), h=vdupq_n_u32(IV[7]);
        for (int t = 0; t < 19; t++) {
            uint32x4_t ch  = vbslq_u32(e, f, g);
            uint32x4_t maj = vbslq_u32(veorq_u32(a, b), c, a);
            uint32x4_t T1  = vaddq_u32(vaddq_u32(h, VS1(e)), vaddq_u32(ch, vaddq_u32(vdupq_n_u32(K[t]), W[t])));
            uint32x4_t T2  = vaddq_u32(VS0(a), maj);
            h=g; g=f; f=e; e=vaddq_u32(d, T1); d=c; c=b; b=a; a=vaddq_u32(T1, T2);
        }
        acc = veorq_u32(acc, a);
    }
    atomic_fetch_add(&g_ref_sink, vgetq_lane_u32(acc, 0));
    #undef VROR
    #undef VS0
    #undef VS1
    #undef Vs0
    #undef Vs1
    return NULL;
}

// Vectorized 4-wide Hensel step mod (1 << (k+1))
static inline uint32x4_t hensel_step_4wide(
    uint32x4_t v_cand, uint32x4_t v_r_base0, uint32x4_t v_a12_xor_a11,
    uint32x4_t v_e14_a11_base, uint32x4_t v_e13_lo10, uint32x4_t v_E15,
    uint32x4_t v_C_E12_base, uint32x4_t v_C_a8_base, uint32x4_t v_a11, uint32x4_t v_mk)
{
    uint32x4_t v_r0 = vandq_u32(vaddq_u32(v_r_base0, vandq_u32(v_cand, v_a12_xor_a11)), v_mk);
    uint32x4_t v_e140 = vandq_u32(vaddq_u32(v_e14_a11_base, v_cand), v_mk);
    uint32x4_t v_ch15 = vandq_u32(vbslq_u32(v_E15, v_e140, v_e13_lo10), v_mk);
    uint32x4_t v_maj = vandq_u32(vbslq_u32(veorq_u32(v_a11, v_cand), v_r0, v_a11), v_mk);
    uint32x4_t v_lhs0 = vandq_u32(vaddq_u32(vsubq_u32(v_C_E12_base, v_ch15), vaddq_u32(v_C_a8_base, v_maj)), v_mk);
    uint32x4_t v_diff = vandq_u32(veorq_u32(v_lhs0, v_r0), v_mk);
    return vceqzq_u32(v_diff);
}

// Theory 41: Combined 4-wide NEON Carry-Sudoku Hensel Lifting + Full-Word D Liberation
static void *ctx_worker_carbon_record(void *arg) {
    ctx_worker_t *cw = (ctx_worker_t *)arg;
    u32 A[R+OFF+2] = {0};
    for (int i = 0; i < 8; i++) A[i] = WIV_GLOBAL[i];
    u32 st[8], aw[8];
    for (int i = 0; i < 8; i++) st[i] = cw->dig[i] - IV[i];
    window_from_state(st, aw);
    for (int i = 0; i < 8; i++) A[(R-7+i)+OFF] = aw[i];

    u32 a12 = A[12+OFF], a13 = A[13+OFF], a14 = A[14+OFF], a15 = A[15+OFF];
    u32 E16 = E_arr(A, 16);
    u32 e15_base = a15 - S0(a14) - MAJ(a14, a13, a12);
    u32 a9_const_base = S0(a12) - a13;
    u32 C_E11_base = e15_base - K[14];

    static const uint32_t s_deltas[5][4] = {
        {0, 1, 2, 3}, {0, 4, 8, 12}, {0, 16, 32, 48}, {0, 64, 128, 192}, {0, 256, 512, 768}
    };
    static const u32 s_mks[5] = {3, 15, 63, 255, 1023};
    uint32x4_t v_deltas[5], v_mks[5];
    for (int s = 0; s < 5; s++) {
        v_deltas[s] = vld1q_u32(s_deltas[s]);
        v_mks[s]    = vdupq_n_u32(s_mks[s]);
    }

    u64 rng = ((u64)(cw->tid + 1) << 56) ^ (cw->attempt * 0xD1B54A32D192ED03ULL) ^ 0x123456789ABCDEF0ULL;
    u64 local_tried = 0;

    while (!atomic_load_explicit(cw->found, memory_order_relaxed)) {
        rng += 0x9E3779B97F4A7C15ULL;
        u64 z = (rng ^ (rng >> 30)) * 0xBF58476D1CE4E5B9ULL;
        u32 a11_lo13 = ((u32)z) & 0x1fffu;
        u32 E13_seed = (u32)(z >> 32);

        u32 a11 = a11_lo13 | (a12 & 0xffc00000u) | ((~a12) & 0x003fe000u);
        u32 E15 = e15_base + a11;
        u32 s1_E15 = S1(E15);
        u32 s0_a11 = S0(a11);
        u32 e14_a11_base = a14 - S0(a13) - MAJ(a13, a12, a11);
        u32 C_E12_base   = E16 - a12 - s1_E15 - K[15] - (u32)PADLEN_BITS;
        u32 C_a8_base    = s0_a11 - a12;

        u32 e13_lo10    = (E13_seed & 0x300u) | 0xffu;
        u32 r_base0     = (e13_lo10 + a9_const_base + (a12 & a11)) & 0x3ffu;
        u32 a12_xor_a11 = (a12 ^ a11) & 0x3ffu;

        uint32x4_t v_r_base0_vec     = vdupq_n_u32(r_base0);
        uint32x4_t v_a12_xor_a11_vec = vdupq_n_u32(a12_xor_a11);
        uint32x4_t v_e14_a11_base_vec = vdupq_n_u32(e14_a11_base);
        uint32x4_t v_e13_lo10_vec    = vdupq_n_u32(e13_lo10);
        uint32x4_t v_E15_vec         = vdupq_n_u32(E15);
        uint32x4_t v_C_E12_base_vec  = vdupq_n_u32(C_E12_base);
        uint32x4_t v_C_a8_base_vec   = vdupq_n_u32(C_a8_base);
        uint32x4_t v_a11_vec         = vdupq_n_u32(a11);

        u32 cur[32], nxt[32];
        int n_cur = 0;

        // Step 0: directly test {0, 1, 2, 3}
        {
            uint32x4_t v_cand0 = v_deltas[0];
            uint32x4_t v_match0 = hensel_step_4wide(
                v_cand0, v_r_base0_vec, v_a12_xor_a11_vec, v_e14_a11_base_vec,
                v_e13_lo10_vec, v_E15_vec, v_C_E12_base_vec, v_C_a8_base_vec,
                v_a11_vec, v_mks[0]);
            if (!vmaxvq_u32(v_match0)) continue;
            u32 m0 = vgetq_lane_u32(v_match0, 0);
            u32 m1 = vgetq_lane_u32(v_match0, 1);
            u32 m2 = vgetq_lane_u32(v_match0, 2);
            u32 m3 = vgetq_lane_u32(v_match0, 3);
            if (m0) cur[n_cur++] = 0;
            if (m1) cur[n_cur++] = 1;
            if (m2) cur[n_cur++] = 2;
            if (m3) cur[n_cur++] = 3;
        }

        // Steps 1..4: 2-bit extensions
        for (int step = 1; step < 5 && n_cur > 0; step++) {
            uint32x4_t v_mk = v_mks[step];
            uint32x4_t v_delta = v_deltas[step];
            int n_nxt = 0;
            for (int j = 0; j < n_cur; j++) {
                uint32x4_t v_cand = vaddq_u32(vdupq_n_u32(cur[j]), v_delta);
                uint32x4_t v_match = hensel_step_4wide(
                    v_cand, v_r_base0_vec, v_a12_xor_a11_vec, v_e14_a11_base_vec,
                    v_e13_lo10_vec, v_E15_vec, v_C_E12_base_vec, v_C_a8_base_vec,
                    v_a11_vec, v_mk);

                if (!vmaxvq_u32(v_match)) continue;
                u32 cand_arr[4], match_arr[4];
                vst1q_u32(cand_arr, v_cand);
                vst1q_u32(match_arr, v_match);
                if (match_arr[0] && n_nxt < 32) nxt[n_nxt++] = cand_arr[0];
                if (match_arr[1] && n_nxt < 32) nxt[n_nxt++] = cand_arr[1];
                if (match_arr[2] && n_nxt < 32) nxt[n_nxt++] = cand_arr[2];
                if (match_arr[3] && n_nxt < 32) nxt[n_nxt++] = cand_arr[3];
            }
            n_cur = n_nxt;
            for (int j = 0; j < n_cur; j++) cur[j] = nxt[j];
        }
        if (!n_cur) continue;

        u32 e13_mid3        = ((E13_seed >> 2) & 7u) << 19;
        u32 target_a9_19_31 = (((~(E15 ^ a11)) & 0xffc00000u) | e13_mid3);
        u32 e13_19_31       = (target_a9_19_31 - (a9_const_base + (a12 & 0xffc00000u))) & 0xfff80000u;

        for (int r_idx = 0; r_idx < n_cur; r_idx++) {
            u32 a10_lo10  = cur[r_idx];
            u32 r10       = (r_base0 + (a10_lo10 & a12_xor_a11)) & 0x3ffu;
            u32 E12_lo8   = (C_E12_base - (~E15) - (E15 & ((e14_a11_base + a10_lo10) & 0xffu))) & 0xffu;
            u32 E13_0     = e13_19_31 | ((E13_seed & 1u) << 10) | e13_lo10;
            u32 req_s1_lo = (e14_a11_base + 1u - E12_lo8 - K[13] - 0x80u) & 0xffu;
            u32 d_s1      = req_s1_lo ^ (S1(E13_0) & 0xffu);
            u32 x_s1      = d_s1 ^ ((d_s1 & 7u) << 5);
            u32 E13       = E13_0 | (x_s1 << 11);
            u32 C_E12_folded = C_E12_base - ((~E15) & E13);
            u32 req_s0_a9 = (a10_lo10 + 1u) & 0x3ffu;

            for (u32 dz_a = 0; dz_a < 8u; dz_a++) {
                u32 a10_lo13   = a10_lo10 | (dz_a << 10);
                u32 a9_0       = E13 + a9_const_base + MAJ(a12, a11, a10_lo13);
                u32 want_a9_13 = (req_s0_a9 ^ (a9_0 >> 2) ^ (a9_0 >> 22)) & 0x1ffu;
                u32 x9         = (want_a9_13 - (a9_0 >> 13)) & 0x1ffu;
                u32 a9         = a9_0 + (x9 << 13);
                if ((S0(a9) & 0x3ffu) != req_s0_a9) continue;
                if (((E15 ^ a11 ^ a9) >> 22) != 0x3ffu) continue;

                local_tried++;
                u32 a11_xor_a9 = a11 ^ a9;
                u32 C_a8_total = C_E12_folded + C_a8_base + (a11 & a9);
                u32 a10_lo22   = a10_lo13 | (x9 << 13);
                u32 E14        = e14_a11_base + a10_lo22;
                u32 bt         = C_a8_total + (a10_lo22 & a11_xor_a9);
                u32 a8         = bt - (E15 & E14);

                u32 req_lo9    = ((r10 + 1u) ^ (a8 >> 2) ^ (a8 >> 13)) & 0x1ffu;
                u32 req_b9     = (((r10 + 1u) >> 9) ^ ((a8 >> 11) & 1u) ^ (req_lo9 & 1u)) & 1u;
                u32 req_sh     = (req_lo9 | (req_b9 << 9)) << 22;

                u32 E14_init = E14;
                for (int k = 22; k <= 31; k++) {
                    u32 bk = (a8 ^ req_sh) & (1u << k);
                    E14 += bk;
                    bt  += bk & a11_xor_a9;
                    a8   = bt - (E15 & E14);
                }
                u32 a10 = a10_lo22 | (E14 - E14_init);
                u32 E12 = C_E12_folded - (E15 & E14);
                u32 E11 = C_E11_base - S1(E14) - CH(E14, E13, E12);
                u32 a7  = E11 - a11 + S0(a10) + MAJ(a10, a9, a8);
                u32 a6  = 0xffffffffu + S0(a9) - a10 + MAJ(a9, a8, a7);
                u32 a5  = 0xffffffffu - a9 + S0(a8) + MAJ(a8, a7, a6);
                u32 D   = a5 ^ a6;
                int pD  = popc(D);

                // Theory 41: Full-word D liberation popc(D) <= 2
                int ok_d = (pD <= 2);
                if (ok_d) {
                    int expected = 0;
                    if (atomic_compare_exchange_strong(cw->found, &expected, 1)) {
                        *cw->out_a9  = a9;
                        *cw->out_a10 = a10;
                        *cw->out_a11 = a11;
                    }
                    goto done;
                }
            }
        }
    }
done:
    atomic_fetch_add(cw->total_tried, local_tried);
    return NULL;
}

static atomic_int g_kb4_mode = 0;

// Unified 8-thread pool (7 background workers + main = 8 threads total, zero oversubscription):
//   mode 1: build_kb4_worker
//   mode 2: chain_worker_kb4
//   mode 3: ctx_worker_carbon (or ctx_worker_carbon_record if g_use_lite == 3)
static void *pool_kb4_worker(void *arg) {
    int tid = (int)(intptr_t)arg;
    u32 last_gen = 0, spins = 0;
    while (1) {
        u32 cg = atomic_load_explicit(&g_kb4_mode, memory_order_acquire);
        if ((cg >> 2) == last_gen) { if (++spins < 524288u) __builtin_arm_yield(); else usleep(20); continue; }
        spins = 0; last_gen = cg >> 2;
        u32 m = cg & 3u;
        if (m == 1u) build_kb4_worker(&g_pool_bld[tid]);
        else if (m == 2u) chain_worker_kb4(&g_pool_ch[tid]);
        else if (m == 3u) {
            if (g_use_lite == 3) ctx_worker_carbon_record(&g_pool_ctx[tid]);
            else                 ctx_worker_carbon(&g_pool_ctx[tid]);
        }
        atomic_fetch_add_explicit(&g_pool_done, 1u, memory_order_release);
    }
    return NULL;
}

static void init_kb4_pool(void) {
    static pthread_t th[7];
    for (int t = 1; t <= 7; t++) pthread_create(&th[t-1], NULL, pool_kb4_worker, (void *)(intptr_t)t);
}

static u64 find_ctx_unified(const u32 dig[8], int kmax, u64 attempt, SolverTrace *tr, u32 A[R+OFF+2]) {
    atomic_int found = 0;
    atomic_uint_fast64_t total_tried = 0;
    u32 a9 = 0, a10 = 0, a11 = 0;
    for (int t = 0; t < 8; t++) {
        g_pool_ctx[t].tid = t; g_pool_ctx[t].attempt = attempt; g_pool_ctx[t].dig = dig; g_pool_ctx[t].kmax = kmax;
        g_pool_ctx[t].found = &found;
        g_pool_ctx[t].out_a9 = &a9; g_pool_ctx[t].out_a10 = &a10; g_pool_ctx[t].out_a11 = &a11;
        g_pool_ctx[t].total_tried = &total_tried;
    }
    atomic_store_explicit(&g_pool_done, 0u, memory_order_relaxed);
    u32 next_g = ((atomic_load(&g_kb4_mode) >> 2) + 1u) << 2;
    atomic_store_explicit(&g_kb4_mode, (int)(next_g | 3u), memory_order_release);
    if (g_use_lite == 3) ctx_worker_carbon_record(&g_pool_ctx[0]);
    else                 ctx_worker_carbon(&g_pool_ctx[0]);
    while (atomic_load_explicit(&g_pool_done, memory_order_acquire) < 7u) __builtin_arm_yield();
    eval_op1_to_op3(dig, a9, a10, a11, tr, A);
    return (u64)total_tried;
}

static int run_suite(const char *name, int lite, double rate_scalar, double rate_neon) {
    g_use_lite = lite;
    double t0 = now_sec();
    if (lite != 1) ht_dense = calloc(BUCKETS, sizeof(u32));
    else           ht_lite  = calloc(BUCKETS, sizeof(u16));

    for (int t = 0; t < 8; t++) { g_pool_bld[t].tid = t; g_pool_bld[t].nthreads = 8; }
    atomic_store_explicit(&g_pool_done, 0u, memory_order_relaxed);
    u32 next_g = ((atomic_load(&g_kb4_mode) >> 2) + 1u) << 2;
    atomic_store_explicit(&g_kb4_mode, (int)(next_g | 1u), memory_order_release);
    build_kb4_worker(&g_pool_bld[0]);
    while (atomic_load_explicit(&g_pool_done, memory_order_acquire) < 7u) __builtin_arm_yield();
    double t_init = now_sec() - t0;

    int all_ok = 1;
    // 10-target suite (identical targets to exp_r19_distant_theories.c):
    double t10_0 = now_sec();
    u64 tot10_op2 = 0, tot10_chains = 0;
    for (u32 k = 0; k < 10; k++) {
        u32 digK[8];
        for (int i = 0; i < 8; i++) digK[i] = H_ABC[i] ^ (k * 0x9E3779B1u + (u32)i * (k ? 0x85EBCA6Bu : 0u));
        SolverTrace trK; u32 AK[R+OFF+2];
        u64 op2K = 0, chainsK = 0, attK = 0;
        int okK = 0;
        while (!okK) {
            attK++;
            op2K += find_ctx_unified(digK, 3, attK + k * 100, &trK, AK);
            atomic_int solved = 0;
            atomic_uint_fast64_t tc = 0;
            SolverTrace out_tr;
            for (int t = 0; t < 8; t++) {
                g_pool_ch[t].tid = t; g_pool_ch[t].attempt = attK + k * 100; g_pool_ch[t].base_tr = &trK; g_pool_ch[t].base_A = AK;
                g_pool_ch[t].solved = &solved; g_pool_ch[t].out_tr = &out_tr; g_pool_ch[t].total_chains = &tc;
            }
            atomic_store_explicit(&g_pool_done, 0u, memory_order_relaxed);
            u32 ng2 = ((atomic_load(&g_kb4_mode) >> 2) + 1u) << 2;
            atomic_store_explicit(&g_kb4_mode, (int)(ng2 | 2u), memory_order_release);
            chain_worker_kb4(&g_pool_ch[0]);
            while (atomic_load_explicit(&g_pool_done, memory_order_acquire) < 7u) __builtin_arm_yield();
            chainsK += (u64)tc;
            if (solved) { trK = out_tr; okK = 1; }
        }
        tot10_op2 += op2K;
        tot10_chains += chainsK;
        if (!verify_preimage_full(&trK, digK)) all_ok = 0;
    }
    double dt10 = now_sec() - t10_0;

    // 50-target statistical suite:
    u64 tot50_op2 = 0, tot50_chains = 0;
    double t50_0 = now_sec();
    for (u32 k = 0; k < 50; k++) {
        u32 digK[8];
        for (int i = 0; i < 8; i++) digK[i] = H_ABC[i] ^ ((k + 100u) * 0x9E3779B1u + (u32)i * 0x85EBCA6Bu);
        SolverTrace trK; u32 AK[R+OFF+2];
        u64 op2K = 0, chainsK = 0, attK = 0;
        int okK = 0;
        while (!okK) {
            attK++;
            op2K += find_ctx_unified(digK, 3, attK + k * 100, &trK, AK);
            atomic_int solved = 0;
            atomic_uint_fast64_t tc = 0;
            SolverTrace out_tr;
            for (int t = 0; t < 8; t++) {
                g_pool_ch[t].tid = t; g_pool_ch[t].attempt = attK + k * 100; g_pool_ch[t].base_tr = &trK; g_pool_ch[t].base_A = AK;
                g_pool_ch[t].solved = &solved; g_pool_ch[t].out_tr = &out_tr; g_pool_ch[t].total_chains = &tc;
            }
            atomic_store_explicit(&g_pool_done, 0u, memory_order_relaxed);
            u32 ng2 = ((atomic_load(&g_kb4_mode) >> 2) + 1u) << 2;
            atomic_store_explicit(&g_kb4_mode, (int)(ng2 | 2u), memory_order_release);
            chain_worker_kb4(&g_pool_ch[0]);
            while (atomic_load_explicit(&g_pool_done, memory_order_acquire) < 7u) __builtin_arm_yield();
            chainsK += (u64)tc;
            if (solved) { trK = out_tr; okK = 1; }
        }
        tot50_op2 += op2K;
        tot50_chains += chainsK;
        if (!verify_preimage_full(&trK, digK)) all_ok = 0;
    }
    double dt50 = now_sec() - t50_0;
    double mean50_s = dt50 / 50.0;
    double log2_scalar = log2(mean50_s * rate_scalar);
    double log2_neon   = log2(mean50_s * rate_neon);
    double log2_alg    = log2((double)(tot50_op2 + tot50_chains) / 50.0);

    printf("  [%-24s] RAM=%4d MB | Init=%.3fs (0 CAS) | 10-Tgt=%.2f ms | 50-Tgt=%.2f ms | Op2=%5llu | Chains=%7llu | log2(SHA19-scalar)=%.2f | log2(SHA19-NEON)=%.2f | log2(alg)=%.2f | %s\n",
           name, (int)((lite == 1) ? (BUCKETS * sizeof(u16) >> 20) : (BUCKETS * sizeof(u32) >> 20)), t_init, (dt10 / 10.0) * 1000.0, mean50_s * 1000.0,
           (unsigned long long)(tot50_op2 / 50), (unsigned long long)(tot50_chains / 50),
           log2_scalar, log2_neon, log2_alg,
           all_ok ? "60/60 OK" : "FAIL");
    if (lite != 1) free(ht_dense); else free(ht_lite);
    return all_ok;
}

#ifndef BENCH_MODES_NO_MAIN
int main(void) {
    init_iv_constants();

    // 1. Calibrate 8-core SHA-19 reference throughput before spinning up persistent pool:
    pthread_t rth[8];
    double ts0 = now_sec();
    for (int t = 0; t < 8; t++) pthread_create(&rth[t], NULL, sha19_scalar_ref_worker, (void *)(uintptr_t)t);
    for (int t = 0; t < 8; t++) pthread_join(rth[t], NULL);
    double dt_scalar = now_sec() - ts0;
    double rate_scalar = (8.0 * (double)(1u << 20)) / dt_scalar;

    double tn0 = now_sec();
    for (int t = 0; t < 8; t++) pthread_create(&rth[t], NULL, sha19_neon_ref_worker, (void *)(uintptr_t)t);
    for (int t = 0; t < 8; t++) pthread_join(rth[t], NULL);
    double dt_neon = now_sec() - tn0;
    double rate_neon = (8.0 * 4.0 * (double)(1u << 20)) / dt_neon;

    printf("========================================================================================================================================================================\n");
    printf("  CARBON HIGH R=19 MODES BENCHMARK (UNIFIED 8-THREAD POOL + CALIBRATED 8-CORE SHA-19 EQUIVALENT LOG2 COMPLEXITY)\n");
    printf("  8-Core SHA-19 Reference Rates: Scalar C = %.2f M SHA19/s (2^%.2f/s) | 4-Wide ARM64 NEON = %.2f M SHA19/s (2^%.2f/s)\n",
           rate_scalar * 1e-6, log2(rate_scalar), rate_neon * 1e-6, log2(rate_neon));
    printf("========================================================================================================================================================================\n");

    init_kb4_pool();
    int ok1 = run_suite("Carbon-Dense Baseline", 0, rate_scalar, rate_neon);
    int ok2 = run_suite("Carbon-Lite (512 MB)", 1, rate_scalar, rate_neon);
    int ok3 = run_suite("Carbon Pass 2 Upgraded", 2, rate_scalar, rate_neon);
    int ok4 = run_suite("Carbon Pass 3 Record", 3, rate_scalar, rate_neon);
    printf("========================================================================================================================================================================\n");
    return (ok1 && ok2 && ok3 && ok4) ? 0 : 1;
}
#endif
