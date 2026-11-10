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
//   - Mode DENSE: u32 *ht_dense (1,024 MB, 2 x u16 slots, 0 MB bitmap, 0 MB a1_cosets!)
//   - Mode LITE : u16 *ht_lite  (  512 MB, 1 x u16 slot,  0 MB bitmap, 0 MB a1_cosets!)
#define KB4          4
#define KM4          15u
#define BUCKETS4     (1ULL << 28)

static u32 *ht_dense;
static u16 *ht_lite;
static int  g_use_lite = 0;

static void *build_kb4_worker(void *arg) {
    tbuild_t *tb = (tbuild_t *)arg;
    const u32 tid = (u32)tb->tid;
    const u32 n_uhi = 1u << (14 - KB4); // 1024 entries (bits 22..31)
    u32 diff_top_tbl[1024], s0_top_tbl[1024];
    for (u32 uhi = 0; uhi < n_uhi; uhi++) {
        u32 ut = uhi << (18 + KB4);
        diff_top_tbl[uhi] = ut;
        s0_top_tbl[uhi]   = (ut >> 7) ^ (ut >> 18) ^ (ut << 14) ^ (ut >> 3);
    }
    int lite = g_use_lite;
    for (u32 ulo18 = 1u; ulo18 < (1u << 18); ulo18++) {
        u32 stored_val = ulo18 & 0xffffu;
        if (stored_val == 0u) continue;
        u32 u_ker   = ((ulo18 ^ (ulo18 >> 3) ^ (ulo18 >> 7)) & KM4) << 18;
        u32 base_u  = ulo18 | u_ker;
        u32 s0_base = s0(base_u);
        u32 c_hi3   = s0_base >> 29;
        u32 blk0    = ((c_hi3 - tid) & 7u) << 7;
        u32 blk1    = ((c_hi3 - tid - 1u) & 7u) << 7;

        if (!lite) {
            for (u32 uhi = blk0; uhi < blk0 + 128u; uhi++) {
                u32 y = (s0_base ^ s0_top_tbl[uhi]) - (base_u | diff_top_tbl[uhi]);
                if ((y >> 29) == tid) {
                    u32 idx = y >> KB4;
                    ht_dense[idx] = (ht_dense[idx] << 16) | stored_val;
                }
            }
            for (u32 uhi = blk1; uhi < blk1 + 128u; uhi++) {
                u32 y = (s0_base ^ s0_top_tbl[uhi]) - (base_u | diff_top_tbl[uhi]);
                if ((y >> 29) == tid) {
                    u32 idx = y >> KB4;
                    ht_dense[idx] = (ht_dense[idx] << 16) | stored_val;
                }
            }
        } else {
            for (u32 uhi = blk0; uhi < blk0 + 128u; uhi++) {
                u32 y = (s0_base ^ s0_top_tbl[uhi]) - (base_u | diff_top_tbl[uhi]);
                if ((y >> 29) == tid) ht_lite[y >> KB4] = (u16)stored_val;
            }
            for (u32 uhi = blk1; uhi < blk1 + 128u; uhi++) {
                u32 y = (s0_base ^ s0_top_tbl[uhi]) - (base_u | diff_top_tbl[uhi]);
                if ((y >> 29) == tid) ht_lite[y >> KB4] = (u16)stored_val;
            }
        }
    }
    return NULL;
}

static void *chain_worker_kb4(void *arg) {
    chain_worker_t *cw = (chain_worker_t *)arg;
    int lite = g_use_lite;
    u32 T16 = cw->base_tr->T16, T17 = cw->base_tr->T17, T18 = cw->base_tr->T18;
    u32 a5 = cw->base_tr->a5, D = cw->base_tr->D;

    u32 deltas[16], D_bads[16], F_targets[16];
    int n_deltas = 0;
    deltas[n_deltas++] = a5 & D;
    for (int dist = 1; dist <= 3; dist++) {
        u32 subD = 0;
        do {
            if (popc((a5 ^ subD) & D) == dist && n_deltas < 16) deltas[n_deltas++] = subD;
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
    const uint32x4_t v_kmask  = vdupq_n_u32(KM4);

    u32 step = ((u32)cw->tid << 18) ^ ((u32)cw->attempt * 0x9E3779B1u);
    u64 local_chains = 0;

    while (!atomic_load_explicit(cw->solved, memory_order_relaxed) && local_chains < 1048576ULL) {
        step++;
        // At KER_BITS=4, 4-bit coset lock (t16 & 15 == 0) using ONLY 4 bits {a1[22..24], a1[16]}!
        u32 idx21    = (step * 1332533u + (u32)cw->tid * 262147u) & 0x001fffffu;
        u32 a1_0     = (idx21 & 0x0000ffffu) | ((idx21 >> 16) << 25);
        u32 e1_0     = a1_0 + C_E1;
        u32 s0_0     = S0(a1_0);
        u32 t16_0    = T16 - (a1_0 + C_W0) + C_G1_base + s0_0 + MAJ(a1_0, A0_const, Am1_const) + S1(e1_0) + CH(e1_0, E0_const, Em1_const);
        u32 diff4    = (s0_0 ^ (s0_0 - t16_0)) & 15u;
        u32 delta_a1 = ((diff4 & 7u) << 22) | (((diff4 >> 3) & 1u) << 16);
        if (((e1_0 & 0x01ffffffu) + delta_a1 + (15u << 18)) >= 0x02000000u) continue;
        u32 a1_base  = a1_0 | delta_a1;

        u32 e1_base       = a1_base + C_E1;
        u32 s0_a1_base    = S0(a1_base);
        u32 s1_e1_base    = S1(e1_base & 0x3e03ffffu);
        u32 e1_mid7_0     = (e1_base >> 18) & 0x7fu;
        u32 e1_hi2_0      = e1_base >> 30;
        u32 ch1_base_lo18 = (Em1_const ^ (e1_base & CH1_XOR)) & 0x3e03ffffu;
        u32 t16_base_f    = T16_folded - a1_base + (a1_base & MAJ1_XOR) + ch1_base_lo18;

        uint32x4_t v_t16_b = vdupq_n_u32(t16_base_f);
        uint32x4_t v_s0_b  = vdupq_n_u32(s0_a1_base);
        uint32x4_t v_s1_b  = vdupq_n_u32(s1_e1_base);

        uint32x4_t v_m0  = vaddq_u32(v_t16_b, vaddq_u32(v_flo0,  vld1q_u32(&CH1_MID7[e1_mid7_0 + 0])));
        uint32x4_t v_m4  = vaddq_u32(v_t16_b, vaddq_u32(v_flo4,  vld1q_u32(&CH1_MID7[e1_mid7_0 + 4])));
        uint32x4_t v_m8  = vaddq_u32(v_t16_b, vaddq_u32(v_flo8,  vld1q_u32(&CH1_MID7[e1_mid7_0 + 8])));
        uint32x4_t v_m12 = vaddq_u32(v_t16_b, vaddq_u32(v_flo12, vld1q_u32(&CH1_MID7[e1_mid7_0 + 12])));

        uint32x4_t v_s0_l0  = veorq_u32(v_s0_b, v_s0l0);
        uint32x4_t v_s0_l4  = veorq_u32(v_s0_b, v_s0l4);
        uint32x4_t v_s0_l8  = veorq_u32(v_s0_b, v_s0l8);
        uint32x4_t v_s0_l12 = veorq_u32(v_s0_b, v_s0l12);

        uint32x4_t v_s1_l0  = veorq_u32(v_s1_b, vld1q_u32(&S1_MID7[e1_mid7_0 + 0]));
        uint32x4_t v_s1_l4  = veorq_u32(v_s1_b, vld1q_u32(&S1_MID7[e1_mid7_0 + 4]));
        uint32x4_t v_s1_l8  = veorq_u32(v_s1_b, vld1q_u32(&S1_MID7[e1_mid7_0 + 8]));
        uint32x4_t v_s1_l12 = veorq_u32(v_s1_b, vld1q_u32(&S1_MID7[e1_mid7_0 + 12]));

        u32 all_t16[64];
        for (u32 f_hi = 0; f_hi < 4u; f_hi++) {
            u32 f_hi_sh  = f_hi << 30;
            u32 e1_hi    = e1_base + f_hi_sh;
            u32 hi_delta = (f_hi_sh & MAJ1_XOR) - f_hi_sh + ((Em1_const ^ (e1_hi & CH1_XOR)) & 0xc0000000u);
            uint32x4_t v_hid   = vdupq_n_u32(hi_delta);
            uint32x4_t v_s0_hi = vdupq_n_u32(S0_HI2[f_hi]);
            uint32x4_t v_s1_hi = vdupq_n_u32(S1_HI2[(e1_hi2_0 + f_hi) & 3u]);

            u32 *dst = &all_t16[f_hi << 4];
            vst1q_u32(&dst[0],  vaddq_u32(vaddq_u32(v_m0,  v_hid), vaddq_u32(veorq_u32(v_s0_l0,  v_s0_hi), veorq_u32(v_s1_l0,  v_s1_hi))));
            vst1q_u32(&dst[4],  vaddq_u32(vaddq_u32(v_m4,  v_hid), vaddq_u32(veorq_u32(v_s0_l4,  v_s0_hi), veorq_u32(v_s1_l4,  v_s1_hi))));
            vst1q_u32(&dst[8],  vaddq_u32(vaddq_u32(v_m8,  v_hid), vaddq_u32(veorq_u32(v_s0_l8,  v_s0_hi), veorq_u32(v_s1_l8,  v_s1_hi))));
            vst1q_u32(&dst[12], vaddq_u32(vaddq_u32(v_m12, v_hid), vaddq_u32(veorq_u32(v_s0_l12, v_s0_hi), veorq_u32(v_s1_l12, v_s1_hi))));
        }

        if (!lite) {
            for (int f = 0; f < 64; f += 4) {
                __builtin_prefetch(&ht_dense[all_t16[f]   >> KB4], 0, 0);
                __builtin_prefetch(&ht_dense[all_t16[f+1] >> KB4], 0, 0);
                __builtin_prefetch(&ht_dense[all_t16[f+2] >> KB4], 0, 0);
                __builtin_prefetch(&ht_dense[all_t16[f+3] >> KB4], 0, 0);
            }
        } else {
            for (int f = 0; f < 64; f += 4) {
                __builtin_prefetch(&ht_lite[all_t16[f]   >> KB4], 0, 0);
                __builtin_prefetch(&ht_lite[all_t16[f+1] >> KB4], 0, 0);
                __builtin_prefetch(&ht_lite[all_t16[f+2] >> KB4], 0, 0);
                __builtin_prefetch(&ht_lite[all_t16[f+3] >> KB4], 0, 0);
            }
        }
        local_chains += 64;

        u32 hit_a1[64], hit_e1[64], hit_t16[64], hit_G1[64], hit_e2b[64];
        u16 w1_ulo[140];
        u8  w1_h[140];
        int n_w1 = 0, n_hits = 0;

        for (int f = 0; f < 64; f++) {
            u32 t16  = all_t16[f];
            u32 pack = lite ? (u32)ht_lite[t16 >> KB4] : ht_dense[t16 >> KB4];
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
                    if ((t17_base & KM4) != 0u) continue;
                    u32 G2 = G2_arr[lane];

                    for (int d_idx = 0; d_idx < n_deltas; d_idx++) {
                        u32 delta = deltas[d_idx];
                        u32 t17   = t17_base - delta;
                        u32 pack2 = lite ? (u32)ht_lite[t17 >> KB4] : ht_dense[t17 >> KB4];
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

                            if ((t18 & KM4) == 0u) {
                                u32 pack3 = lite ? (u32)ht_lite[t18 >> KB4] : ht_dense[t18 >> KB4];
                                for (u32 p3 = pack3; p3 != 0u; p3 >>= 16) {
                                    u32 ulo3 = p3 & 0xffffu;
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

static atomic_int g_kb4_mode = 0;

// Unified 8-thread pool (7 background workers + main = 8 threads total, zero oversubscription):
//   mode 1: build_kb4_worker
//   mode 2: chain_worker_kb4
//   mode 3: ctx_worker_carbon
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
        else if (m == 3u) ctx_worker_carbon(&g_pool_ctx[tid]);
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
    ctx_worker_carbon(&g_pool_ctx[0]);
    while (atomic_load_explicit(&g_pool_done, memory_order_acquire) < 7u) __builtin_arm_yield();
    eval_op1_to_op3(dig, a9, a10, a11, tr, A);
    return (u64)total_tried;
}

static int run_suite(const char *name, int lite, double rate_scalar, double rate_neon) {
    g_use_lite = lite;
    double t0 = now_sec();
    if (!lite) ht_dense = calloc(BUCKETS4, sizeof(u32));
    else       ht_lite  = calloc(BUCKETS4, sizeof(u16));

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
           name, lite ? 512 : 1024, t_init, (dt10 / 10.0) * 1000.0, mean50_s * 1000.0,
           (unsigned long long)(tot50_op2 / 50), (unsigned long long)(tot50_chains / 50),
           log2_scalar, log2_neon, log2_alg,
           all_ok ? "60/60 OK" : "FAIL");
    if (!lite) free(ht_dense); else free(ht_lite);
    return all_ok;
}

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
    int ok1 = run_suite("Carbon-Dense (KB=4 u32)", 0, rate_scalar, rate_neon);
    int ok2 = run_suite("Carbon-Lite  (KB=4 u16)", 1, rate_scalar, rate_neon);
    printf("========================================================================================================================================================================\n");
    return (ok1 && ok2) ? 0 : 1;
}
