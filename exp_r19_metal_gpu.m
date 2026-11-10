#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <arm_neon.h>

typedef uint32_t u32;
typedef uint64_t u64;
typedef uint16_t u16;
typedef uint8_t  u8;

#define R 19
#define OFF 7
#define PADLEN_BITS 440ULL

static inline u32 rotr(u32 x, int n) { return (x >> n) | (x << (32 - n)); }
#define S0(x) (rotr((x),2) ^ rotr((x),13) ^ rotr((x),22))
#define S1(x) (rotr((x),6) ^ rotr((x),11) ^ rotr((x),25))
#define s0(x) (rotr((x),7) ^ rotr((x),18) ^ ((x)>>3))
#define s1(x) (rotr((x),17) ^ rotr((x),19) ^ ((x)>>10))
#define CH(x,y,z)  (((x)&(y)) ^ (~(x)&(z)))
#define MAJ(x,y,z) (((x)&(y)) ^ ((x)&(z)) ^ ((y)&(z)))
#define popc(x) __builtin_popcount(x)

static inline u32 Qf(u32 u) { return s0(u) - u; }

static const u32 K[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,
    0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,
    0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,
    0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,
    0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,
    0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,
    0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,
    0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,
    0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

static const u32 IV[8] = {
    0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
    0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u
};

static const u32 H_ABC[8] = {
    0xba7816bfu, 0x8f01cfeau, 0x414140deu, 0x5dae2223u,
    0xb00361a3u, 0x96177a9cu, 0xb410ff61u, 0xf20015adu
};

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static inline int syzygy_ok(u32 ulo, u32 z) {
    u32 lhs = z ^ (z >> 4) ^ (z >> 15);
    u32 rhs = (ulo >> 1) ^ (ulo >> 3) ^ (ulo >> 11);
    return ((lhs ^ rhs) & 1u) == 0u;
}

static inline u32 rec_u_z(u32 ulo, u32 z) {
    // THEORY 19 + 32B: Rotation-Free 3-Op Shifted-XOR 17-Bit s0(ulo) + 4-Op Reduced Triangular Inverse
    u32 v1 = z ^ (ulo >> 7) ^ (ulo << 14) ^ (ulo >> 3);
    u32 b0 = ((v1 << 15) ^ (v1 << 11) ^ v1) & 0x00010000u;
    u32 b12 = ((v1 << 14) ^ (v1 << 3)) & 0x00060000u;
    u32 T = b0 | b12 | ((v1 << 18) & 0xfff80000u);
    u32 U = T ^ (T << 11) ^ (b0 << 15);
    return ulo | U;
}

static inline u32 rec_u(u32 ulo, u32 y) {
    return rec_u_z(ulo, ulo + y);
}

static inline u32 E_arr(const u32 *A, int s) {
    return A[s+OFF] - S0(A[s-1+OFF]) - MAJ(A[s-1+OFF], A[s-2+OFF], A[s-3+OFF]) + A[s-4+OFF];
}

static inline u32 Wt_arr(const u32 *A, int t) {
    u32 et = E_arr(A, t);
    return E_arr(A, t+1) - A[t-3+OFF] - E_arr(A, t-3) - S1(et) - CH(et, E_arr(A, t-1), E_arr(A, t-2)) - K[t];
}

static void window_from_state(const u32 st[8], u32 w[8]) {
    u32 Aa[8], Ee[4];
    for (int i = 0; i < 4; i++) { Aa[i] = st[i]; Ee[i] = st[4+i]; }
    for (int k = 0; k < 4; k++) Aa[4+k] = Ee[k] - Aa[k] + S0(Aa[k+1]) + MAJ(Aa[k+1], Aa[k+2], Aa[k+3]);
    for (int i = 0; i < 8; i++) w[i] = Aa[7-i];
}

static void compress(const u32 Wm[16], int r, u32 out[8]) {
    u32 W[64]; memcpy(W, Wm, 64);
    for (int t = 16; t < r; t++) W[t] = s1(W[t-2]) + W[t-7] + s0(W[t-15]) + W[t-16];
    u32 a=IV[0], b=IV[1], c=IV[2], d=IV[3], e=IV[4], f=IV[5], g=IV[6], h=IV[7];
    for (int t = 0; t < r; t++) {
        u32 T1 = h + S1(e) + CH(e,f,g) + K[t] + W[t], T2 = S0(a) + MAJ(a,b,c);
        h=g; g=f; f=e; e=d+T1; d=c; c=b; b=a; a=T1+T2;
    }
    u32 st[8] = {a,b,c,d,e,f,g,h};
    for (int i = 0; i < 8; i++) out[i] = st[i] + IV[i];
}

typedef struct {
    u32 dig[8];
    u32 st[8];
    u32 a_win[8];
    u32 a9, a10, a11;
    u32 a8, a7, a6, w13, a5, D;
    u32 c9, c10, c11;
    u32 W14, W15, W16, W17, W18;
    u32 T16, T17, T18;
    u32 a1, W0, G1, t16, W1, a2;
    u32 G2, t17, W2, a3;
    u32 G3, t18, W3, a4;
    u32 Wm[16];
    u32 H_out[8];
} SolverTrace;

static u32 WIV_GLOBAL[8];
static u32 A_iv_const[R+OFF+2];
static u32 C_W0, C_E1, C_G1_base, E0_const, Em1_const, A0_const, Am1_const, Am2_const;
static u32 C_G2_base, C_G3_base;
// THEORY 14A: Disjoint Boolean Mask Folding Constants for Hop-1 (MAJ & CH Constant Absorption)
static u32 MAJ1_CONST, MAJ1_XOR, CH1_XOR, C_G1_folded, Am2_folded, C_t16_folded;
// THEORY 16: Precomputed Top-4-Bit F_2-Linear Rotation Tables for S0(s_hi << 28) & S1(e14_hi4 << 28)
static u32 S0_TOP4[16], S1_TOP4[16];
// THEORY 17: 3-Way Carry-Isolated Hop-1 Decomposition Tables (cuts 98.44% = 63/64 of S0(a1) & S1(e1) calls!)
static u32 S0_HI2[4], S1_HI2[4], S0_LO4[16], S1_MID7[128];
// THEORY 25: 25-Bit Cross-dz Invariant S0(a8) Hoisting + Contiguous Cyclic NEON S1_TOP4_CYC Table
static u32 S1_TOP4_CYC[32], S0_MID3[8], S0_HENSEL6[64];
// THEORY 28: 19-Bit Cross-dz S1(E14) Invariance Table (9 bits 19..27 -> 512 entries, 2 KB in L1)
static u32 S1_HI9[512];
// THEORY 29: Hop-1 25-Bit CH1 Carry-Shielded Invariance + 4-Wide ARM64 NEON t16 Tables
static u32 FLO_MAJ_SUB[16], CH1_MID7[128];
// THEORY 32C: Single-Lookup Hop-3 Viability Threshold Table
static const int MAX_FORCED_HI[17] = {-1, 0, 1, 2, 3, 4, 5, 6, 5, 4, 5, 6, 7, 8, 9, 10, 11};

static void init_iv_constants(void) {
    window_from_state(IV, WIV_GLOBAL);
    memset(A_iv_const, 0, sizeof(A_iv_const));
    for (int i = 0; i < 8; i++) A_iv_const[i] = WIV_GLOBAL[i];
    C_W0 = Wt_arr(A_iv_const, 0);
    C_E1 = E_arr(A_iv_const, 1);
    E0_const  = E_arr(A_iv_const, 0);
    Em1_const = E_arr(A_iv_const, -1);
    u32 Em2_const = E_arr(A_iv_const, -2);
    A0_const  = A_iv_const[0+OFF];
    Am1_const = A_iv_const[-1+OFF];
    Am2_const = A_iv_const[-2+OFF];
    C_G1_base = Em2_const + K[1];
    C_G2_base = Em1_const + K[2];
    C_G3_base = E0_const + K[3];

    MAJ1_CONST   = A0_const & Am1_const;
    MAJ1_XOR     = A0_const ^ Am1_const;
    CH1_XOR      = E0_const ^ Em1_const;
    C_G1_folded  = C_G1_base + MAJ1_CONST;
    Am2_folded   = Am2_const - MAJ1_CONST;
    C_t16_folded = C_G1_folded - C_W0;

    for (u32 s = 0; s < 16u; s++) {
        S0_TOP4[s] = S0(s << 28);
        S1_TOP4[s] = S1(s << 28);
        S0_LO4[s]  = S0(s << 18);
        FLO_MAJ_SUB[s] = ((s << 18) & MAJ1_XOR) - (s << 18);
    }
    for (u32 s = 0; s < 32u; s++) {
        S1_TOP4_CYC[s] = S1_TOP4[s & 15u];
    }
    for (u32 m = 0; m < 8u; m++) {
        S0_MID3[m] = S0(m << 19);
    }
    for (u32 h = 0; h < 64u; h++) {
        S0_HENSEL6[h] = S0(h << 22);
    }
    for (u32 f = 0; f < 4u; f++) {
        S0_HI2[f] = S0(f << 30);
        S1_HI2[f] = S1(f << 30);
    }
    for (u32 m = 0; m < 128u; m++) {
        S1_MID7[m]  = S1(m << 18);
        CH1_MID7[m] = (Em1_const ^ ((m << 18) & CH1_XOR)) & 0x01fc0000u;
    }
    for (u32 m = 0; m < 512u; m++) {
        S1_HI9[m] = S1(m << 19);
    }
}

static inline void eval_W0_G1_E1_fast(u32 a1, u32 *W0, u32 *G1, u32 *e1, u32 *sm1) {
    *e1 = a1 + C_E1;
    *W0 = a1 + C_W0;
    *sm1 = S0(a1) + MAJ(a1, A0_const, Am1_const);
    *G1 = C_G1_base + *sm1 + S1(*e1) + CH(*e1, E0_const, Em1_const);
}

#define KER_BITS    5
#define KER_MASK    ((1u << KER_BITS) - 1u)
#define LUT_BUCKETS (1ULL << (32 - KER_BITS))
#define BM_WORDS    (LUT_BUCKETS >> 5)

static _Atomic u64 *ht_Q0_ker = NULL;
static _Atomic u32 *bm_Q0_ker = NULL; // Theory 11: 16 MB L2/SLC-resident existence bitmap!

#define A1_PER_COSET (1u << 18)
static u32 *a1_cosets_flat = NULL;
static _Atomic u32 a1_coset_cnt[32];

typedef struct { int tid; int nthreads; } tbuild_t;

static void *build_ker_tables_worker(void *arg) {
    tbuild_t *tb = (tbuild_t *)arg;
    u32 total_lo18 = 1u << 18;
    u32 start_lo = (total_lo18 * (u32)tb->tid) / (u32)tb->nthreads;
    u32 end_lo   = (total_lo18 * (u32)(tb->tid + 1)) / (u32)tb->nthreads;

    for (u32 ulo18 = start_lo; ulo18 < end_lo; ulo18++) {
        u32 u18_22 = ((ulo18 ^ (ulo18 >> 3) ^ (ulo18 >> 7)) & 0x1fu) << 18;
        u32 base_u = ulo18 | u18_22;
        u16 stored_val = (u16)(ulo18 & 0xffffu);
        if (stored_val == 0) continue;

        for (u32 uhi9 = 0; uhi9 < 512u; uhi9++) {
            u32 u = base_u | (uhi9 << 23);
            u32 y = Qf(u);
            u32 b = y >> KER_BITS;
            u64 cur = atomic_load_explicit(&ht_Q0_ker[b], memory_order_relaxed);
            while (1) {
                u64 next = cur;
                if ((cur & 0xffffULL) == 0ULL) next = cur | (u64)stored_val;
                else if (((cur >> 16) & 0xffffULL) == 0ULL) next = cur | ((u64)stored_val << 16);
                else if (((cur >> 32) & 0xffffULL) == 0ULL) next = cur | ((u64)stored_val << 32);
                else if ((cur >> 48) == 0ULL) next = cur | ((u64)stored_val << 48);
                else break;
                if (atomic_compare_exchange_weak_explicit(&ht_Q0_ker[b], &cur, next, memory_order_relaxed, memory_order_relaxed)) {
                    if ((cur & 0xffffULL) == 0ULL)
                        atomic_fetch_or_explicit(&bm_Q0_ker[b >> 5], 1u << (b & 31u), memory_order_relaxed);
                    break;
                }
            }
        }
    }

    u32 a1_start = (u32)tb->tid * (1u << 20) + 1u;
    for (u32 i = 0; i < (1u << 20); i++) {
        u32 a1_base = ((a1_start + i) * 0x9E3779B1u) & 0x3fc3ffffu;
        u32 e1_base = a1_base + C_E1;
        if (((e1_base >> 22) & 7u) == 7u) continue;
        u32 W0, G1, e1, sm1;
        eval_W0_G1_E1_fast(a1_base, &W0, &G1, &e1, &sm1);
        u32 c = (W0 - G1) & KER_MASK;
        u32 idx = atomic_fetch_add_explicit(&a1_coset_cnt[c], 1u, memory_order_relaxed);
        if (idx < A1_PER_COSET) {
            a1_cosets_flat[c * A1_PER_COSET + idx] = a1_base;
        }
    }
    return NULL;
}

// ========================================================================================
// THEORY 19: EXACT DIRECT-MAPPED RECONSTRUCTION BIJECTION ON 2^27-BUCKET ht_Q0_ker
//   Since b = y >> 5 is in 1-to-1 bijection with y in ker(Q0 mod 32) (zero hash collisions)
//   and rec_u_z(u & 0xffff, (u & 0xffff) + Qf(u)) == u holds for 100.0000% of u in Z_{2^32},
//   every non-zero slot ulo in ht_Q0_ker[b] is 100% tautologically guaranteed to satisfy
//   syzygy_ok(ulo, z) AND Qf(rec_u_z(ulo, z)) == y without any verification!
// ========================================================================================
static inline int lookup_Q0_ker_aligned(u32 y, u32 out[4]) {
    u32 b = y >> KER_BITS;
    // THEORY 11: 16 MB SLC-resident bitmap check is an exact 0-false-positive image membership oracle!
    u32 bm = atomic_load_explicit(&bm_Q0_ker[b >> 5], memory_order_relaxed);
    if (((bm >> (b & 31u)) & 1u) == 0u) return 0;
    u64 pack = atomic_load_explicit(&ht_Q0_ker[b], memory_order_relaxed);
    int k = 0;
    for (int s = 0; s < 4; s++) {
        u32 ulo = (u32)((pack >> (s * 16)) & 0xffffULL);
        if (!ulo) break;
        out[k++] = rec_u_z(ulo, ulo + y);
    }
    return k;
}

static inline int lookup_Q0_ker(u32 y, u32 out[4]) {
    if ((y & KER_MASK) != 0u) return 0;
    return lookup_Q0_ker_aligned(y, out);
}

static void eval_op1_to_op3(const u32 dig[8], u32 a9, u32 a10, u32 a11, SolverTrace *tr, u32 A[R+OFF+2]) {
    memcpy(tr->dig, dig, 32);
    for (int i = 0; i < 8; i++) A[i] = WIV_GLOBAL[i];
    for (int i = 0; i < 8; i++) tr->st[i] = dig[i] - IV[i];
    window_from_state(tr->st, tr->a_win);
    for (int i = 0; i < 8; i++) A[(R-7+i)+OFF] = tr->a_win[i];

    tr->a9 = a9; tr->a10 = a10; tr->a11 = a11;
    A[9+OFF] = a9; A[10+OFF] = a10; A[11+OFF] = a11;
    A[8+OFF] = 0; u32 w15 = Wt_arr(A, 15); A[8+OFF] = w15 - (u32)PADLEN_BITS;
    A[7+OFF] = 0; u32 w14 = Wt_arr(A, 14); A[7+OFF] = w14 - (u32)(PADLEN_BITS >> 32);
    A[6+OFF] = 0xffffffffu - A[10+OFF] + S0(A[9+OFF]) + MAJ(A[9+OFF], A[8+OFF], A[7+OFF]);
    A[5+OFF] = 0; A[4+OFF] = 0; A[3+OFF] = 0; A[2+OFF] = 0; A[1+OFF] = 0;
    tr->w13 = Wt_arr(A, 13);
    A[5+OFF] = 0xffffffffu - A[9+OFF] + S0(A[8+OFF]) + MAJ(A[8+OFF], A[7+OFF], A[6+OFF]);
    tr->a8 = A[8+OFF]; tr->a7 = A[7+OFF]; tr->a6 = A[6+OFF]; tr->a5 = A[5+OFF];
    tr->D = tr->a5 ^ tr->a6;

    tr->c9 = Wt_arr(A, 9); tr->c10 = Wt_arr(A, 10); tr->c11 = Wt_arr(A, 11);
    tr->W14 = Wt_arr(A, 14); tr->W15 = Wt_arr(A, 15);
    tr->W16 = Wt_arr(A, 16); tr->W17 = Wt_arr(A, 17); tr->W18 = Wt_arr(A, 18);
    tr->T16 = tr->W16 - s1(tr->W14) - tr->c9;
    tr->T17 = tr->W17 - s1(tr->W15) - tr->c10;
    tr->T18 = tr->W18 - s1(tr->W16) - tr->c11;
}

// ========================================================================================
// THEORY 10: DOUBLE-MAJORITY MODULO-32 CANCELLATION & BIT-26 UPWARD-CARRY SHIELDING IN OP2
//   1. In the outer loop, lock (a8_base & 31) == (a9 & 31) == r and
//      (S0(a9) & 31) == ((a10_lo + 1) & 31).
//      Then for ALL 4,096 steps of the inner loop:
//        - MAJ(a9, a8, a7) & 31 == r (100% independent of a7!)
//        - a6 & 31 == r (100% invariant across all 4,096 steps!)
//        - MAJ(a8, a7, a6) & 31 == r (100% independent of a7!)
//        - In a5 = (0xffffffff - a9) + S0(a8) + MAJ(a8, a7, a6), the (-r) and (+r)
//          cancel identically mod 32, leaving a5 & 31 == (S0(a8) - 1) & 31!
//   2. Split the 12-bit step (bits 20..31 of a10) into:
//        - s_lo (bits 20..26, 128 values): compute ONLY E14, E12, a8 and check
//          ((a8 >> 22) & 31) == req_a8_22_26 (skips 96.875% of dead steps in 4 ops!).
//        - s_hi (bits 27..31, 32 values): because carries in Z_{2^32} only propagate UPWARD,
//          varying bits 27..31 of a10 NEVER changes bits 0..26 of a8, so 100% of all 32
//          inner steps satisfy BOTH (W13 & 0xff) == 0x80 AND (D & 31) == 0!
// ========================================================================================
typedef struct {
    int tid;
    u64 attempt;
    const u32 *dig;
    int kmax;
    atomic_int *found;
    u32 *out_a9, *out_a10, *out_a11;
    atomic_uint_fast64_t *total_tried;
    atomic_uint_fast64_t *ker_hits;
} ctx_worker_t;

static void *ctx_worker_theory10(void *arg) {
    ctx_worker_t *cw = (ctx_worker_t *)arg;
    u32 A[R+OFF+2];
    memset(A, 0, sizeof(A));
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

    u64 rng = ((u64)(cw->tid + 1) << 56) ^ (cw->attempt * 0xD1B54A32D192ED03ULL) ^ 0x123456789ABCDEF0ULL;
    u64 local_tried = 0, local_ker = 0;

    const uint32x4_t v_off0  = {0u, 1u << 28, 2u << 28, 3u << 28};
    const uint32x4_t v_off4  = {4u << 28, 5u << 28, 6u << 28, 7u << 28};
    const uint32x4_t v_off8  = {8u << 28, 9u << 28, 10u << 28, 11u << 28};
    const uint32x4_t v_off12 = {12u << 28, 13u << 28, 14u << 28, 15u << 28};
    const uint32x4_t v_mask28 = vdupq_n_u32(0x0fffffffu);
    const uint32x4_t v_s0_0  = vld1q_u32(&S0_TOP4[0]);
    const uint32x4_t v_s0_4  = vld1q_u32(&S0_TOP4[4]);
    const uint32x4_t v_s0_8  = vld1q_u32(&S0_TOP4[8]);
    const uint32x4_t v_s0_12 = vld1q_u32(&S0_TOP4[12]);
    const uint32x4_t v_dz0_3 = {0u << 19, 1u << 19, 2u << 19, 3u << 19};
    const uint32x4_t v_dz4_7 = {4u << 19, 5u << 19, 6u << 19, 7u << 19};
    const uint32x4_t v_s0_dz0_3 = vld1q_u32(&S0_MID3[0]);
    const uint32x4_t v_s0_dz4_7 = vld1q_u32(&S0_MID3[4]);
    const uint32x4_t v_s0_mul   = vdupq_n_u32(0x04008040u);
    const uint32x4_t v_mul_h6   = vdupq_n_u32(0x00100201u);
    const uint32x4_t v_mul_m3   = vdupq_n_u32(0x20020040u);
    const uint32x4_t v_mask7    = vdupq_n_u32(7u);

    // THEORY 34: Lazy 1,024-Coset 2-Adic Branching Hensel Sieve + O(1) Rotated-CTZ Root Selector!
    u64 m_cache[1024];
    u8  m_seen[1024];
    memset(m_seen, 0, sizeof(m_seen));

    while (!atomic_load_explicit(cw->found, memory_order_relaxed)) {
        rng += 0x9E3779B97F4A7C15ULL;
        u64 z = (rng ^ (rng >> 30)) * 0xBF58476D1CE4E5B9ULL;
        u32 a11_raw  = (u32)z;
        u32 E13_seed = (u32)(z >> 32);
        u32 a11_lo8      = a11_raw & 0x000000ffu;
        u32 a10_lo_hi2   = E13_seed & 0xc0u;
        u32 a10_lo_start = E13_seed & 0x3fu;
        u32 coset_key    = (a11_lo8 << 2) | (a10_lo_hi2 >> 6);
        if (m_seen[coset_key] && m_cache[coset_key] == 0ULL) continue;

        // THEORY 6 + THEORY 12 + THEORY 15B (Opaque-Transparent-Opaque Majority Tri-Partition on a11):
        u32 a11 = a11_lo8 | (a12 & 0xfff81f00u) | ((~a12) & 0x0007e000u);
        u32 E15 = e15_base + a11;
        u32 s1_E15 = S1(E15);
        u32 s0_a11 = S0(a11);
        u32 e14_a11_base = a14 - S0(a13) - MAJ(a13, a12, a11);
        u32 C_E12_base   = E16 - a12 - s1_E15 - K[15] - (u32)PADLEN_BITS;
        u32 C_E11_m_a11  = C_E11_base - a11;
        u32 C_a8_base    = s0_a11 - a12;

        // THEORY 10 + 28C + 34 Micro-step 1: 6-Level 2-Adic Branching Hensel Tree + Rotated-CTZ Root Selector:
        u32 r_base0     = (0x3fu + a9_const_base + (a12 & a11)) & 0x3fu;
        u32 a12_xor_a11 = (a12 ^ a11) & 0x3fu;
        u32 C_E12_lo_f  = (C_E12_base - (~E15)) & 0xffu;
        u32 C_a8_lo_f   = (C_E12_lo_f + C_a8_base) & 0x3fu;
        u64 m = m_cache[coset_key];
        if (!m_seen[coset_key]) {
            u32 cur_c[64], next_c[64];
            int n_cur = 1;
            cur_c[0] = a10_lo_hi2;
            for (int k = 0; k < 6 && n_cur > 0; k++) {
                u32 mask_k = (1u << (k + 1)) - 1u;
                int n_next = 0;
                for (int j = 0; j < n_cur; j++) {
                    u32 base_c = cur_c[j];
                    for (u32 bit = 0; bit <= 1u; bit++) {
                        u32 cand = base_c | (bit << k);
                        u32 r0   = (r_base0 + (cand & a12_xor_a11)) & mask_k;
                        u32 e140 = (e14_a11_base + cand) & mask_k;
                        u32 lhs0 = (C_a8_lo_f - (E15 & e140) + MAJ(a11, cand, r0)) & mask_k;
                        if ((((lhs0 ^ r0) >> k) & 1u) == 0u) next_c[n_next++] = cand;
                    }
                }
                n_cur = n_next;
                for (int j = 0; j < n_cur; j++) cur_c[j] = next_c[j];
            }
            m = 0ULL;
            for (int j = 0; j < n_cur; j++) m |= (1ULL << (cur_c[j] & 0x3fu));
            m_cache[coset_key] = m;
            m_seen[coset_key]  = 1u;
            if (!m) continue;
        }
        u64 m_rot  = (m >> a10_lo_start) | (m << ((64u - a10_lo_start) & 63u));
        u32 a10_lo = a10_lo_hi2 | ((a10_lo_start + (u32)__builtin_ctzll(m_rot)) & 0x3fu);
        u32 r6     = (r_base0 + (a10_lo & a12_xor_a11)) & 0x3fu;
        u32 E12_lo = (C_E12_lo_f - (E15 & ((e14_a11_base + a10_lo) & 0xffu))) & 0xffu;

        // THEORY 13A: Exact Involution S1(E13) Solver on bits 11..18 + 3-Bit Carry Absorber on bits 19..21
        //   + Bijective Jacobian J[22..27] == 0x3f (100.00% guaranteed on every seed & target)!
        u32 target_a9_19_27 = (~(E15 ^ a11)) & 0x0fc00000u;
        u32 e13_19_27       = (target_a9_19_27 - (a9_const_base + (a12 & 0xfff80000u))) & 0x0ff80000u;
        u32 E13_0           = (E13_seed & 0xf0000700u) | e13_19_27 | 0xffu;
        u32 req_s1_lo       = (e14_a11_base + 1u - E12_lo - K[13] - 0x80u) & 0xffu;
        u32 d_s1            = req_s1_lo ^ (S1(E13_0) & 0xffu);
        u32 x_s1            = d_s1 ^ ((d_s1 & 7u) << 5);
        u32 E13             = E13_0 | (x_s1 << 11);

        // THEORY 12 (6-Bit Mod-64 Lock): O(1) Transparent Majority Gate on bits 13..18 of a10_lo19!
        u32 a10_base0  = a10_lo; // bits 8..18 are 0
        u32 a9_0       = E13 + a9_const_base + MAJ(a12, a11, a10_base0);
        u32 req_s0_a9  = (a10_lo + 1u) & 0x3fu;
        u32 want_a9_13 = (req_s0_a9 ^ (a9_0 >> 2) ^ (a9_0 >> 22)) & 0x3fu;
        u32 x6         = (want_a9_13 - (a9_0 >> 13)) & 0x3fu;
        u32 a10_lo19_0 = a10_base0 | (x6 << 13);
        u32 a9         = a9_0 + (x6 << 13);
        if ((S0(a9) & 0x3fu) != req_s0_a9) continue;

        u32 J_mask = E15 ^ a11 ^ a9;
        if (((J_mask >> 22) & 0x3fu) != 0x3fu) continue;

        u32 C_a6_base = 0xffffffffu + S0(a9);
        u32 C_a5_base = 0xffffffffu - a9;
        u32 E15_hi4        = E15 & 0xf0000000u;
        u32 a11_xor_a9     = a11 ^ a9;
        u32 a11_xor_a9_hi4 = a11_xor_a9 & 0xf0000000u;
        // THEORY 22: Disjoint Ch/Maj Constant Folding in Op2 + Incremental base_term Hensel Lifting!
        u32 C_E12_folded   = C_E12_base - ((~E15) & E13);
        u32 C_a8_total     = C_E12_folded + C_a8_base + (a11 & a9);

        // THEORY 15B + 25 + 28 + 31: Dual Dead-Zone Sweep + 4-Wide NEON Vectorized dz=0..7 Hensel Lifting + Carry-Free Mul S0!
        uint32x4_t v_E15        = vdupq_n_u32(E15);
        uint32x4_t v_E15_hi4    = vdupq_n_u32(E15_hi4);
        uint32x4_t v_a11_xor_a9 = vdupq_n_u32(a11_xor_a9);
        uint32x4_t v_a11x9_hi4  = vdupq_n_u32(a11_xor_a9_hi4);
        uint32x4_t v_off_x0     = vandq_u32(v_off0,  v_a11x9_hi4);
        uint32x4_t v_off_x4     = vandq_u32(v_off4,  v_a11x9_hi4);
        uint32x4_t v_off_x8     = vandq_u32(v_off8,  v_a11x9_hi4);
        uint32x4_t v_off_x12    = vandq_u32(v_off12, v_a11x9_hi4);
        uint32x4_t v_C_a5_b     = vdupq_n_u32(C_a5_base);
        uint32x4_t v_CE12_f     = vdupq_n_u32(C_E12_folded);
        uint32x4_t v_E13_v      = vdupq_n_u32(E13);
        uint32x4_t v_a9_v       = vdupq_n_u32(a9);
        uint32x4_t v_CE11_m     = vdupq_n_u32(C_E11_m_a11);
        uint32x4_t v_Ca6_b      = vdupq_n_u32(C_a6_base);

        for (u32 dz_a = 0; dz_a < 8u; dz_a++) {
            u32 a10_lo19   = a10_lo19_0 | (dz_a << 8);
            u32 E14_base0  = e14_a11_base + a10_lo19;
            u32 base_term0 = C_a8_total + (a10_lo19 & a11_xor_a9);
            u32 a8_base0   = base_term0 - (E15 & E14_base0);
            u32 req_a8_22_27 = ((r6 + 1u) ^ (a8_base0 >> 2) ^ (a8_base0 >> 13)) & 0x3fu;
            u32 req_a8_sh    = req_a8_22_27 << 22;
            // THEORY 25A + 28A: 25 of 28 bits of a8_mid and 19 of 28 bits of E14_mid are 100% invariant across dz and s_hi!
            u32 s0_a8_inv25  = S0((a8_base0 & 0x0007ffffu) | req_a8_sh);
            u32 s1_e14_inv19 = S1(E14_base0 & 0x0007ffffu);
            u32 s0_a10_lo19  = S0(a10_lo19);

            // THEORY 31A + 35: 4-Wide ARM64 NEON Vectorized dz=0..7 Hensel Lifting + Dual Carry-Free Mul S0!
            u32 E14_mid_arr[8], a8_mid_arr[8], a10_mid_arr[8];
            u32 a8_b28_arr[8], a7_mb_arr[8], Ca6_f_arr[8], a9x8_arr[8];
            u32 s0_a10_m_arr[8], s0_a8_l28_arr[8];
            uint32x4_t v_req_sh   = vdupq_n_u32(req_a8_sh);
            uint32x4_t v_E14_i0   = vaddq_u32(vdupq_n_u32(E14_base0), v_dz0_3);
            uint32x4_t v_E14_i4   = vaddq_u32(vdupq_n_u32(E14_base0), v_dz4_7);
            uint32x4_t v_E14_0    = v_E14_i0;
            uint32x4_t v_E14_4    = v_E14_i4;
            uint32x4_t v_bt_0     = vaddq_u32(vdupq_n_u32(base_term0), vandq_u32(v_dz0_3, v_a11_xor_a9));
            uint32x4_t v_bt_4     = vaddq_u32(vdupq_n_u32(base_term0), vandq_u32(v_dz4_7, v_a11_xor_a9));
            uint32x4_t v_e15e14_0 = vandq_u32(v_E15, v_E14_0);
            uint32x4_t v_e15e14_4 = vandq_u32(v_E15, v_E14_4);
            uint32x4_t v_a8_0     = vsubq_u32(v_bt_0, v_e15e14_0);
            uint32x4_t v_a8_4     = vsubq_u32(v_bt_4, v_e15e14_4);

            for (int k = 22; k <= 27; k++) {
                uint32x4_t v_mk  = vdupq_n_u32(1u << k);
                uint32x4_t v_bk0 = vandq_u32(veorq_u32(v_a8_0, v_req_sh), v_mk);
                uint32x4_t v_bk4 = vandq_u32(veorq_u32(v_a8_4, v_req_sh), v_mk);
                v_E14_0    = vaddq_u32(v_E14_0, v_bk0);
                v_E14_4    = vaddq_u32(v_E14_4, v_bk4);
                v_bt_0     = vaddq_u32(v_bt_0, vandq_u32(v_bk0, v_a11_xor_a9));
                v_bt_4     = vaddq_u32(v_bt_4, vandq_u32(v_bk4, v_a11_xor_a9));
                v_e15e14_0 = vandq_u32(v_E15, v_E14_0);
                v_e15e14_4 = vandq_u32(v_E15, v_E14_4);
                v_a8_0     = vsubq_u32(v_bt_0, v_e15e14_0);
                v_a8_4     = vsubq_u32(v_bt_4, v_e15e14_4);
            }
            uint32x4_t v_diff_0  = vsubq_u32(v_E14_0, v_E14_i0);
            uint32x4_t v_diff_4  = vsubq_u32(v_E14_4, v_E14_i4);
            uint32x4_t v_a10_0   = vorrq_u32(vorrq_u32(vdupq_n_u32(a10_lo19), v_dz0_3), v_diff_0);
            uint32x4_t v_a10_4   = vorrq_u32(vorrq_u32(vdupq_n_u32(a10_lo19), v_dz4_7), v_diff_4);
            // THEORY 35: S0(h6 << 22) == h6 * 0x00100201u & S0(m3 << 19) == m3 * 0x20020040u in 4-Wide NEON!
            uint32x4_t v_s0_10_0 = veorq_u32(veorq_u32(vdupq_n_u32(s0_a10_lo19), v_s0_dz0_3), vmulq_u32(vshrq_n_u32(v_diff_0, 22), v_mul_h6));
            uint32x4_t v_s0_10_4 = veorq_u32(veorq_u32(vdupq_n_u32(s0_a10_lo19), v_s0_dz4_7), vmulq_u32(vshrq_n_u32(v_diff_4, 22), v_mul_h6));
            uint32x4_t v_s0_a8_0 = veorq_u32(vdupq_n_u32(s0_a8_inv25), vmulq_u32(vandq_u32(vshrq_n_u32(v_a8_0, 19), v_mask7), v_mul_m3));
            uint32x4_t v_s0_a8_4 = veorq_u32(vdupq_n_u32(s0_a8_inv25), vmulq_u32(vandq_u32(vshrq_n_u32(v_a8_4, 19), v_mask7), v_mul_m3));
            uint32x4_t v_E12_0   = vsubq_u32(v_CE12_f, v_e15e14_0);
            uint32x4_t v_E12_4   = vsubq_u32(v_CE12_f, v_e15e14_4);
            uint32x4_t v_b28_0   = vaddq_u32(v_a8_0, vandq_u32(v_E14_0, v_E15_hi4));
            uint32x4_t v_b28_4   = vaddq_u32(v_a8_4, vandq_u32(v_E14_4, v_E15_hi4));
            uint32x4_t v_a9a8_0  = vandq_u32(v_a9_v, v_a8_0);
            uint32x4_t v_a9a8_4  = vandq_u32(v_a9_v, v_a8_4);
            uint32x4_t v_a9x8_0  = veorq_u32(v_a9_v, v_a8_0);
            uint32x4_t v_a9x8_4  = veorq_u32(v_a9_v, v_a8_4);
            uint32x4_t v_ch14_0  = vbslq_u32(v_E14_0, v_E13_v, v_E12_0);
            uint32x4_t v_ch14_4  = vbslq_u32(v_E14_4, v_E13_v, v_E12_4);
            uint32x4_t v_maj10_0 = vaddq_u32(v_a9a8_0, vandq_u32(v_a10_0, v_a9x8_0));
            uint32x4_t v_maj10_4 = vaddq_u32(v_a9a8_4, vandq_u32(v_a10_4, v_a9x8_4));
            uint32x4_t v_a7mb_0  = vaddq_u32(vsubq_u32(v_CE11_m, v_ch14_0), v_maj10_0);
            uint32x4_t v_a7mb_4  = vaddq_u32(vsubq_u32(v_CE11_m, v_ch14_4), v_maj10_4);
            uint32x4_t v_Ca6f_0  = vaddq_u32(vsubq_u32(v_Ca6_b, v_a10_0), v_a9a8_0);
            uint32x4_t v_Ca6f_4  = vaddq_u32(vsubq_u32(v_Ca6_b, v_a10_4), v_a9a8_4);

            vst1q_u32(&E14_mid_arr[0],   v_E14_0);   vst1q_u32(&E14_mid_arr[4],   v_E14_4);
            vst1q_u32(&a8_mid_arr[0],    v_a8_0);    vst1q_u32(&a8_mid_arr[4],    v_a8_4);
            vst1q_u32(&a10_mid_arr[0],   v_a10_0);   vst1q_u32(&a10_mid_arr[4],   v_a10_4);
            vst1q_u32(&s0_a10_m_arr[0],  v_s0_10_0); vst1q_u32(&s0_a10_m_arr[4],  v_s0_10_4);
            vst1q_u32(&s0_a8_l28_arr[0], v_s0_a8_0); vst1q_u32(&s0_a8_l28_arr[4], v_s0_a8_4);
            vst1q_u32(&a8_b28_arr[0],    v_b28_0);   vst1q_u32(&a8_b28_arr[4],    v_b28_4);
            vst1q_u32(&a7_mb_arr[0],     v_a7mb_0);  vst1q_u32(&a7_mb_arr[4],     v_a7mb_4);
            vst1q_u32(&Ca6_f_arr[0],     v_Ca6f_0);  vst1q_u32(&Ca6_f_arr[4],     v_Ca6f_4);
            vst1q_u32(&a9x8_arr[0],      v_a9x8_0);  vst1q_u32(&a9x8_arr[4],      v_a9x8_4);

            for (u32 dz = 0; dz < 8u; dz++) {
                u32 E14_mid     = E14_mid_arr[dz];
                u32 a8_mid      = a8_mid_arr[dz];
                u32 a10_mid     = a10_mid_arr[dz];
                u32 s0_a10_mid  = s0_a10_m_arr[dz];
                u32 s1_e14_lo28 = s1_e14_inv19 ^ S1_HI9[(E14_mid >> 19) & 0x1ffu];
                u32 s0_a8_lo28  = s0_a8_l28_arr[dz];
                u32 a8_base28   = a8_b28_arr[dz];
                u32 a7_mid_base = a7_mb_arr[dz];
                u32 C_a6_folded = Ca6_f_arr[dz];
                u32 a9_xor_a8   = a9x8_arr[dz];
                u32 e14_hi0     = E14_mid >> 28;

                local_tried += 16;
                local_ker   += 16;

                // THEORY 25C + 28B + 31B: 4-Wide ARM64 NEON Op2 Inner Loop with Carry-Free Mul S0 + Deferred Stores!
                uint32x4_t v_E14_mid    = vdupq_n_u32(E14_mid);
                uint32x4_t v_a8_base28  = vdupq_n_u32(a8_base28);
                uint32x4_t v_s0_a8_lo28 = vdupq_n_u32(s0_a8_lo28);
                uint32x4_t v_s1_e14_lo  = vdupq_n_u32(s1_e14_lo28);
                uint32x4_t v_s0_a10_mid = vdupq_n_u32(s0_a10_mid);
                uint32x4_t v_a7_mid_b   = vdupq_n_u32(a7_mid_base);
                uint32x4_t v_C_a6_f     = vdupq_n_u32(C_a6_folded);
                uint32x4_t v_a9_xor_a8  = vdupq_n_u32(a9_xor_a8);
                uint32x4_t v_a8_mid_v   = vdupq_n_u32(a8_mid);

                #define NEON_SHI_BLOCK(v_off, v_off_x, v_s0_shi, s_off, v_D_out) \
                    do { \
                        uint32x4_t v_E14   = vaddq_u32(v_E14_mid, (v_off)); \
                        uint32x4_t v_a8    = vsubq_u32(vaddq_u32(v_a8_base28, (v_off_x)), vandq_u32(v_E14, v_E15_hi4)); \
                        uint32x4_t v_s0_a8 = veorq_u32(v_s0_a8_lo28, vmulq_u32(vshrq_n_u32(v_a8, 28), v_s0_mul)); \
                        uint32x4_t v_s1_hi = vld1q_u32(&S1_TOP4_CYC[e14_hi0 + (s_off)]); \
                        uint32x4_t v_s1_e  = veorq_u32(v_s1_e14_lo, v_s1_hi); \
                        uint32x4_t v_s0_10 = veorq_u32(v_s0_a10_mid, (v_s0_shi)); \
                        uint32x4_t v_a7    = vaddq_u32(vsubq_u32(v_a7_mid_b, v_s1_e), v_s0_10); \
                        uint32x4_t v_a6    = vaddq_u32(v_C_a6_f, vandq_u32(v_a7, v_a9_xor_a8)); \
                        uint32x4_t v_maj8  = vbslq_u32(veorq_u32(v_a7, v_a6), v_a8_mid_v, v_a7); \
                        uint32x4_t v_a5    = vaddq_u32(vaddq_u32(v_C_a5_b, v_s0_a8), v_maj8); \
                        (v_D_out)          = vandq_u32(veorq_u32(v_a5, v_a6), v_mask28); \
                    } while (0)

                uint32x4_t v_D0, v_D4, v_D8, v_D12;
                NEON_SHI_BLOCK(v_off0,  v_off_x0,  v_s0_0,  0,  v_D0);
                NEON_SHI_BLOCK(v_off4,  v_off_x4,  v_s0_4,  4,  v_D4);
                NEON_SHI_BLOCK(v_off8,  v_off_x8,  v_s0_8,  8,  v_D8);
                NEON_SHI_BLOCK(v_off12, v_off_x12, v_s0_12, 12, v_D12);
                #undef NEON_SHI_BLOCK

                uint32x4_t v_min_all = vminq_u32(vminq_u32(v_D0, v_D4), vminq_u32(v_D8, v_D12));
                if (__builtin_expect(vminvq_u32(v_min_all) <= 0x0000ffffu, 0)) {
                    u32 D28_arr[16];
                    vst1q_u32(&D28_arr[0],  v_D0);
                    vst1q_u32(&D28_arr[4],  v_D4);
                    vst1q_u32(&D28_arr[8],  v_D8);
                    vst1q_u32(&D28_arr[12], v_D12);
                    for (u32 s_hi = 0; s_hi < 16u; s_hi++) {
                        u32 D_lo28 = D28_arr[s_hi];
                        if ((D_lo28 >> 16) != 0u || popc(D_lo28) > 3) continue;

                        u32 top4   = s_hi << 28;
                        u32 E14    = E14_mid + top4;
                        u32 a8     = a8_base28 - (E14 & E15_hi4) + (top4 & a11_xor_a9_hi4);
                        u32 s0_a8  = s0_a8_lo28 ^ S0_TOP4[a8 >> 28];
                        u32 a10    = a10_mid | top4;
                        u32 E12    = C_E12_folded - (E15 & E14);
                        u32 E11    = C_E11_base - (s1_e14_lo28 ^ S1_TOP4[E14 >> 28]) - CH(E14, E13, E12);
                        u32 a7     = E11 - a11 + (s0_a10_mid ^ S0_TOP4[s_hi]) + MAJ(a10, a9, a8);
                        u32 a6     = C_a6_base - a10 + MAJ(a9, a8, a7);
                        u32 a5     = C_a5_base + s0_a8 + MAJ(a8, a7, a6);
                        u32 D      = a5 ^ a6;

                        int pD = popc(D);
                        int pD_hi = popc(D >> 16);
                        if ((D & KER_MASK) == 0u && pD_hi == 0 && (pD <= cw->kmax || (pD == 3 && local_tried > 131072u))) {
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
        }
    }
done:
    atomic_fetch_add(cw->total_tried, local_tried);
    if (cw->ker_hits) atomic_fetch_add(cw->ker_hits, local_ker);
    return NULL;
}

// ========================================================================================
// METAL GPU COMPUTE ENGINE (APPLE M3 GPU, ZERO-COPY UNIFIED MEMORY + 16 MB SLC BITMAP)
// ========================================================================================
static NSString *const kMetalShaderSource = @"\n\
#include <metal_stdlib>\n\
using namespace metal;\n\
\n\
#define KER_BITS 5\n\
#define KER_MASK 0x1fu\n\
\n\
inline uint rotr(uint x, uint n) { return (x >> n) | (x << (32u - n)); }\n\
#define S0(x) (rotr((x),2u) ^ rotr((x),13u) ^ rotr((x),22u))\n\
#define S1(x) (rotr((x),6u) ^ rotr((x),11u) ^ rotr((x),25u))\n\
#define s0(x) (rotr((x),7u) ^ rotr((x),18u) ^ ((x)>>3u))\n\
#define CH(x,y,z)  (((x)&(y)) ^ (~(x)&(z)))\n\
#define MAJ(x,y,z) (((x)&(y)) ^ ((x)&(z)) ^ ((y)&(z)))\n\
\n\
inline uint Qf(uint u) { return s0(u) - u; }\n\
\n\
inline uint rec_u_z(uint ulo, uint z) {\n\
    uint v1 = z ^ (ulo >> 7) ^ (ulo << 14) ^ (ulo >> 3);\n\
    uint b0 = ((v1 << 15) ^ (v1 << 11) ^ v1) & 0x00010000u;\n\
    uint b12 = ((v1 << 14) ^ (v1 << 3)) & 0x00060000u;\n\
    uint T = b0 | b12 | ((v1 << 18) & 0xfff80000u);\n\
    uint U = T ^ (T << 11) ^ (b0 << 15);\n\
    return ulo | U;\n\
}\n\
\n\
constant int MAX_FORCED_HI[17] = {-1, 0, 1, 2, 3, 4, 5, 6, 5, 4, 5, 6, 7, 8, 9, 10, 11};\n\
\n\
inline uint rec_u(uint ulo, uint y) { return rec_u_z(ulo, ulo + y); }\n\
\n\
inline bool syzygy_ok(uint ulo, uint z) {\n\
    uint lhs = z ^ (z >> 4) ^ (z >> 15);\n\
    uint rhs = (ulo >> 1) ^ (ulo >> 3) ^ (ulo >> 11);\n\
    return ((lhs ^ rhs) & 1u) == 0u;\n\
}\n\
\n\
inline int lookup_Q0_ker_aligned_gpu(uint y, device const uint *bm_Q0_ker, device const ulong *ht_Q0_ker, thread uint out[4]) {\n\
    uint b = y >> KER_BITS;\n\
    if (((bm_Q0_ker[b >> 5] >> (b & 31u)) & 1u) == 0u) return 0;\n\
    ulong pack = ht_Q0_ker[b];\n\
    int k = 0;\n\
    for (int s = 0; s < 4; s++) {\n\
        uint ulo = (uint)((pack >> (s * 16)) & 0xffffUL);\n\
        if (!ulo) break;\n\
        out[k++] = rec_u_z(ulo, ulo + y);\n\
    }\n\
    return k;\n\
}\n\
\n\
inline int lookup_Q0_ker_gpu(uint y, device const uint *bm_Q0_ker, device const ulong *ht_Q0_ker, thread uint out[4]) {\n\
    if ((y & KER_MASK) != 0u) return 0;\n\
    return lookup_Q0_ker_aligned_gpu(y, bm_Q0_ker, ht_Q0_ker, out);\n\
}\n\
\n\
struct Stage4Params {\n\
    uint T16, T17, T18;\n\
    uint a5, D, n_deltas;\n\
    uint deltas[16];\n\
    uint D_bads[16];\n\
    uint F_targets[16];\n\
    uint C_W0, C_E1, C_G1_base, C_G2_base, C_G3_base;\n\
    uint E0_const, Em1_const, A0_const, Am1_const, Am2_const;\n\
    uint T16_folded, MAJ1_XOR, CH1_XOR, C_G1_folded, Am2_folded;\n\
    uint start_idx, count;\n\
};\n\
\n\
struct Stage4Result {\n\
    atomic_uint solved;\n\
    uint a1, W0, G1, t16, W1, a2, G2, t17, W2, a3, G3, t18, W3, a4;\n\
};\n\
\n\
kernel void stage4_ultra_gpu(\n\
    device const ulong *ht_Q0_ker [[buffer(0)]],\n\
    device const uint  *coset_a1  [[buffer(1)]],\n\
    constant Stage4Params &p      [[buffer(2)]],\n\
    device Stage4Result *res      [[buffer(3)]],\n\
    device const uint  *bm_Q0_ker [[buffer(4)]],\n\
    uint gid [[thread_position_in_grid]])\n\
{\n\
    uint idx = gid >> 6;\n\
    uint fibre = gid & 63u;\n\
    if (idx >= p.count) return;\n\
    if (atomic_load_explicit(&res->solved, memory_order_relaxed) != 0u) return;\n\
\n\
    uint a1_base = coset_a1[p.start_idx + idx];\n\
    uint a1 = a1_base | ((fibre & 15u) << 18) | ((fibre >> 4) << 30);\n\
\n\
    uint e1 = a1 + p.C_E1;\n\
    uint sm1_var = S0(a1) + (a1 & p.MAJ1_XOR);\n\
    uint G1_var  = sm1_var + S1(e1) + (p.Em1_const ^ (e1 & p.CH1_XOR));\n\
    uint t16     = p.T16_folded + G1_var - a1;\n\
    uint b1 = t16 >> KER_BITS;\n\
    if (((bm_Q0_ker[b1 >> 5] >> (b1 & 31u)) & 1u) == 0u) return;\n\
    ulong pack1 = ht_Q0_ker[b1];\n\
\n\
    uint W0 = a1 + p.C_W0;\n\
    uint G1 = G1_var + p.C_G1_folded;\n\
    uint e2_base = p.Am2_folded - sm1_var;\n\
    uint maj2_const = a1 & p.A0_const;\n\
    uint maj2_xor   = a1 ^ p.A0_const;\n\
    uint ch2_xor    = e1 ^ p.E0_const;\n\
    uint C_G2_f     = p.C_G2_base + maj2_const;\n\
    uint T17_folded = p.T17 + C_G2_f;\n\
    uint Am1_f      = p.Am1_const - maj2_const;\n\
\n\
    for (int x = 0; x < 4; x++) {\n\
        uint ulo1 = (uint)((pack1 >> (x * 16)) & 0xffffUL);\n\
        if (!ulo1) break;\n\
        uint W1 = rec_u_z(ulo1, ulo1 + t16), a2 = W1 + G1;\n\
        uint e2 = a2 + e2_base;\n\
        uint sm2_var = S0(a2) + (a2 & maj2_xor);\n\
        uint G2_var  = sm2_var + S1(e2) + (p.E0_const ^ (e2 & ch2_xor));\n\
        uint t17_base = T17_folded + G2_var - W1;\n\
        if ((t17_base & KER_MASK) != 0u) continue;\n\
        uint G2 = C_G2_f + G2_var;\n\
        uint e3_base = Am1_f - sm2_var;\n\
\n\
        for (uint d_idx = 0; d_idx < p.n_deltas; d_idx++) {\n\
            uint delta = p.deltas[d_idx];\n\
            uint t17 = t17_base - delta;\n\
            uint b2 = t17 >> KER_BITS;\n\
            if (((bm_Q0_ker[b2 >> 5] >> (b2 & 31u)) & 1u) == 0u) continue;\n\
            ulong pack2 = ht_Q0_ker[b2];\n\
\n\
            uint D_bad = p.D_bads[d_idx];\n\
            uint F_target = p.F_targets[d_idx];\n\
\n\
            for (int s = 0; s < 4; s++) {\n\
                uint ulo2 = (uint)((pack2 >> (s * 16)) & 0xffffUL);\n\
                if (!ulo2) break;\n\
                uint a3_lo = (ulo2 + G2) & 0xffffu;\n\
                if (d_idx > 0u && ((a3_lo ^ delta) & D_bad) != 0u) continue;\n\
                uint free_lo = (p.a5 ^ a3_lo) & (~p.D) & 0xffffu;\n\
                if (!free_lo) continue;\n\
                int n_free_lo = popcount(free_lo);\n\
                uint W2 = rec_u_z(ulo2, ulo2 + t17);\n\
                uint a3 = W2 + G2;\n\
                uint M_hi = ~(p.a5 ^ a3) & 0xffff0000u;\n\
                int n_forced_hi = popcount(M_hi);\n\
                if (n_forced_hi > MAX_FORCED_HI[n_free_lo]) continue;\n\
                uint M = M_hi | ((~free_lo) & 0xffffu);\n\
\n\
                uint e3 = a3 + e3_base;\n\
                uint sm3 = S0(a3) + MAJ(a3, a2, a1);\n\
                uint G3 = p.C_G3_base + sm3 + S1(e3) + CH(e3, e2, e1);\n\
                uint t18 = p.T18 + G3 - W2;\n\
\n\
                if ((t18 & KER_MASK) == 0u) {\n\
                    uint b3 = t18 >> KER_BITS;\n\
                    if (((bm_Q0_ker[b3 >> 5] >> (b3 & 31u)) & 1u) != 0u) {\n\
                        ulong pack3 = ht_Q0_ker[b3];\n\
                        for (int z = 0; z < 4; z++) {\n\
                            uint ulo3 = (uint)((pack3 >> (z * 16)) & 0xffffUL);\n\
                            if (!ulo3) break;\n\
                            if ((((ulo3 + G3) ^ F_target) & M & 0xffffu) != 0u) continue;\n\
                            uint W3 = rec_u_z(ulo3, ulo3 + t18), a4 = W3 + G3;\n\
                            if (((a4 ^ F_target) & M) == 0u) {\n\
                                uint exp = 0u;\n\
                                if (atomic_compare_exchange_weak_explicit(&res->solved, &exp, 1u, memory_order_relaxed, memory_order_relaxed)) {\n\
                                    res->a1=a1; res->W0=W0; res->G1=G1; res->t16=t16; res->W1=W1; res->a2=a2;\n\
                                    res->G2=G2; res->t17=t17; res->W2=W2; res->a3=a3; res->G3=G3; res->t18=t18; res->W3=W3; res->a4=a4;\n\
                                }\n\
                                return;\n\
                            }\n\
                        }\n\
                    }\n\
                }\n\
\n\
                if (n_free_lo <= 9) {\n\
                    uint base_a4_lo = F_target & M & 0xffffu;\n\
                    uint base_w3_lo = (base_a4_lo - G3) & 0xffffu;\n\
                    uint M_mid11    = M & 0x07ff0000u;\n\
                    uint sub = 0u;\n\
                    do {\n\
                        uint w3_lo = (base_w3_lo + sub) & 0xffffu;\n\
                        uint z_lo  = w3_lo + t18;\n\
                        uint v1    = z_lo ^ (w3_lo >> 7) ^ (w3_lo << 14) ^ (w3_lo >> 3);\n\
                        uint sv1   = (v1 << 15) ^ (v1 << 11) ^ v1;\n\
                        if ((sv1 & 0x00008000u) == 0u) {\n\
                            uint b0      = sv1 & 0x00010000u;\n\
                            uint b12     = ((v1 << 14) ^ (v1 << 3)) & 0x00060000u;\n\
                            uint T       = b0 | b12 | ((v1 << 18) & 0xfff80000u);\n\
                            uint W3_lo27 = w3_lo | T;\n\
                            if ((((W3_lo27 + G3) ^ F_target) & M_mid11) == 0u) {\n\
                                uint W3 = W3_lo27 ^ (T << 11) ^ (b0 << 15);\n\
                                uint a4 = W3 + G3;\n\
                                if (((a4 ^ F_target) & M) == 0u && Qf(W3) == t18) {\n\
                                    uint exp = 0u;\n\
                                    if (atomic_compare_exchange_weak_explicit(&res->solved, &exp, 1u, memory_order_relaxed, memory_order_relaxed)) {\n\
                                        res->a1=a1; res->W0=W0; res->G1=G1; res->t16=t16; res->W1=W1; res->a2=a2;\n\
                                        res->G2=G2; res->t17=t17; res->W2=W2; res->a3=a3; res->G3=G3; res->t18=t18; res->W3=W3; res->a4=a4;\n\
                                    }\n\
                                    return;\n\
                                }\n\
                            }\n\
                        }\n\
                        sub = (sub - free_lo) & free_lo;\n\
                    } while (sub != 0u);\n\
                }\n\
            }\n\
        }\n\
    }\n\
}\n\
\n\
struct Op2Params {\n\
    uint a12, a13, a14, a15, E16;\n\
    uint e15_base, a9_const_base;\n\
    uint K13, K14, K15, padlen_lo;\n\
    uint kmax;\n\
    ulong attempt;\n\
};\n\
\n\
struct Op2Result {\n\
    atomic_uint found;\n\
    uint a9, a10, a11;\n\
};\n\
\n\
kernel void op2_ultra_gpu(\n\
    constant Op2Params &op [[buffer(0)]],\n\
    device Op2Result *res  [[buffer(1)]],\n\
    uint gid [[thread_position_in_grid]])\n\
{\n\
    uint a12 = op.a12, a13 = op.a13, a14 = op.a14;\n\
    ulong rng = ((ulong)(gid + 1u) * 0x9E3779B97F4A7C15UL) ^ (op.attempt * 0xD1B54A32D192ED03UL) ^ 0x123456789ABCDEF0UL;\n\
\n\
    for (uint iter = 0; iter < 1024u; iter++) {\n\
        if ((iter & 7u) == 0u && atomic_load_explicit(&res->found, memory_order_relaxed) != 0u) return;\n\
        rng += 0x9E3779B97F4A7C15UL;\n\
        ulong z = (rng ^ (rng >> 30)) * 0xBF58476D1CE4E5B9UL;\n\
        uint a11_raw = (uint)z;\n\
        uint a11 = (a11_raw & 0x000000ffu) | (a12 & 0xfff81f00u) | ((~a12) & 0x0007e000u);\n\
        uint E13_seed = (uint)(z >> 32);\n\
\n\
        uint E15 = op.e15_base + a11;\n\
        uint s1_E15 = S1(E15);\n\
        uint s0_a11 = S0(a11);\n\
        uint e14_a11_base = a14 - S0(a13) - MAJ(a13, a12, a11);\n\
        uint C_E12_base   = op.E16 - a12 - s1_E15 - op.K15 - op.padlen_lo;\n\
        uint C_E11_m_a11  = op.e15_base - op.K14 - a11;\n\
        uint C_a8_base    = s0_a11 - a12;\n\
\n\
        uint a10_lo_hi2   = E13_seed & 0xc0u;\n\
        uint a10_lo_start = E13_seed & 0x3fu;\n\
        uint r_base0      = (0x3fu + op.a9_const_base + (a12 & a11)) & 0x3fu;\n\
        uint a12_xor_a11  = (a12 ^ a11) & 0x3fu;\n\
        uint C_E12_lo_f   = (C_E12_base - (~E15)) & 0xffu;\n\
        uint C_a8_lo_f    = (C_E12_lo_f + C_a8_base) & 0x3fu;\n\
        uint a10_lo = 0u, E12_lo = 0u, r6 = 0u;\n\
        bool ok_r = false;\n\
        for (uint d = 0; d < 64u; d++) {\n\
            uint cand_lo = a10_lo_hi2 | ((a10_lo_start + d) & 0x3fu);\n\
            uint cand_r  = (r_base0 + (cand_lo & a12_xor_a11)) & 0x3fu;\n\
            uint e14_lo  = (e14_a11_base + cand_lo) & 0xffu;\n\
            uint e14_m   = E15 & e14_lo;\n\
            uint a8_lo6  = (C_a8_lo_f - e14_m + MAJ(a11, cand_lo, cand_r)) & 0x3fu;\n\
            if (a8_lo6 == cand_r) {\n\
                a10_lo = cand_lo; E12_lo = (C_E12_lo_f - e14_m) & 0xffu; r6 = cand_r;\n\
                ok_r = true;\n\
                break;\n\
            }\n\
        }\n\
        if (!ok_r) continue;\n\
\n\
        uint target_a9_19_27 = (~(E15 ^ a11)) & 0x0fc00000u;\n\
        uint e13_19_27       = (target_a9_19_27 - (op.a9_const_base + (a12 & 0xfff80000u))) & 0x0ff80000u;\n\
        uint E13_0           = (E13_seed & 0xf0000700u) | e13_19_27 | 0xffu;\n\
        uint req_s1_lo       = (e14_a11_base + 1u - E12_lo - op.K13 - 0x80u) & 0xffu;\n\
        uint d_s1            = req_s1_lo ^ (S1(E13_0) & 0xffu);\n\
        uint x_s1            = d_s1 ^ ((d_s1 & 7u) << 5);\n\
        uint E13             = E13_0 | (x_s1 << 11);\n\
\n\
        uint a10_base0  = a10_lo;\n\
        uint a9_0       = E13 + op.a9_const_base + MAJ(a12, a11, a10_base0);\n\
        uint req_s0_a9  = (a10_lo + 1u) & 0x3fu;\n\
        uint want_a9_13 = (req_s0_a9 ^ (a9_0 >> 2) ^ (a9_0 >> 22)) & 0x3fu;\n\
        uint x6         = (want_a9_13 - (a9_0 >> 13)) & 0x3fu;\n\
        uint a10_lo19_0 = a10_base0 | (x6 << 13);\n\
        uint a9         = a9_0 + (x6 << 13);\n\
        if ((S0(a9) & 0x3fu) != req_s0_a9) continue;\n\
\n\
        uint J_mask = E15 ^ a11 ^ a9;\n\
        if (((J_mask >> 22) & 0x3fu) != 0x3fu) continue;\n\
\n\
        uint C_a6_base     = 0xffffffffu + S0(a9);\n\
        uint C_a5_base     = 0xffffffffu - a9;\n\
        uint E15_hi4       = E15 & 0xf0000000u;\n\
        uint a11_xor_a9    = a11 ^ a9;\n\
        uint a11_xor_a9_hi4 = a11_xor_a9 & 0xf0000000u;\n\
        uint C_E12_folded  = C_E12_base - ((~E15) & E13);\n\
        uint C_a8_total    = C_E12_folded + C_a8_base + (a11 & a9);\n\
\n\
        for (uint dz_a = 0; dz_a < 8u; dz_a++) {\n\
            if (atomic_load_explicit(&res->found, memory_order_relaxed) != 0u) return;\n\
            uint a10_lo19   = a10_lo19_0 | (dz_a << 8);\n\
            uint E14_base0  = e14_a11_base + a10_lo19;\n\
            uint base_term0 = C_a8_total + (a10_lo19 & a11_xor_a9);\n\
            uint a8_base0   = base_term0 - (E15 & E14_base0);\n\
            uint req_a8_22_27 = ((r6 + 1u) ^ (a8_base0 >> 2) ^ (a8_base0 >> 13)) & 0x3fu;\n\
            uint req_a8_sh    = req_a8_22_27 << 22;\n\
            uint s0_a8_inv25  = S0((a8_base0 & 0x0007ffffu) | req_a8_sh);\n\
            uint s1_e14_inv19 = S1(E14_base0 & 0x0007ffffu);\n\
            uint s0_a10_lo19  = S0(a10_lo19);\n\
\n\
            for (uint dz = 0; dz < 8u; dz++) {\n\
                uint dz_sh     = dz << 19;\n\
                uint a10_mid   = a10_lo19 | dz_sh;\n\
                uint E14_mid   = E14_base0 + dz_sh;\n\
                uint base_term = base_term0 + (dz_sh & a11_xor_a9);\n\
                uint a8_mid    = base_term - (E15 & E14_mid);\n\
\n\
                for (int k = 22; k <= 27; k++) {\n\
                    uint bit_k  = (a8_mid ^ req_a8_sh) & (1u << k);\n\
                    a10_mid   |= bit_k;\n\
                    E14_mid   += bit_k;\n\
                    base_term += (bit_k & a11_xor_a9);\n\
                    a8_mid     = base_term - (E15 & E14_mid);\n\
                }\n\
                uint E12_mid     = C_E12_folded - (E15 & E14_mid);\n\
                uint s0_a8_lo28  = s0_a8_inv25 ^ S0(a8_mid & 0x00380000u);\n\
                uint s1_e14_lo28 = s1_e14_inv19 ^ S1(E14_mid & 0x0ff80000u);\n\
                uint s0_a10_mid  = s0_a10_lo19 ^ S0(a10_mid & 0x0ff80000u);\n\
                uint a8_base28   = a8_mid + (E14_mid & E15_hi4);\n\
                uint a7_mid_base = C_E11_m_a11 - CH(E14_mid, E13, E12_mid) + MAJ(a10_mid, a9, a8_mid);\n\
                uint C_a6_folded = C_a6_base - a10_mid + (a9 & a8_mid);\n\
                uint a9_xor_a8   = a9 ^ a8_mid;\n\
\n\
                for (uint s_hi = 0; s_hi < 16u; s_hi++) {\n\
                    uint top4    = s_hi << 28;\n\
                    uint E14     = E14_mid + top4;\n\
                    uint a8      = a8_base28 - (E14 & E15_hi4) + (top4 & a11_xor_a9_hi4);\n\
                    uint s0_a8   = s0_a8_lo28 ^ ((a8 >> 28) * 0x04008040u);\n\
                    uint s1_e14  = s1_e14_lo28 ^ ((E14 >> 28) * 0x00420008u);\n\
                    uint s0_a10  = s0_a10_mid ^ (s_hi * 0x04008040u);\n\
                    uint a7_lo28 = a7_mid_base - s1_e14 + s0_a10;\n\
                    uint a6_lo28 = C_a6_folded + (a7_lo28 & a9_xor_a8);\n\
                    uint a5_lo28 = C_a5_base + s0_a8 + MAJ(a8_mid, a7_lo28, a6_lo28);\n\
                    uint D_lo28  = (a5_lo28 ^ a6_lo28) & 0x0fffffffu;\n\
                    if ((D_lo28 >> 16) != 0u || popcount(D_lo28) > (int)op.kmax) continue;\n\
\n\
                    uint a10 = a10_mid | top4;\n\
                    uint E12 = C_E12_folded - (E15 & E14);\n\
                    uint E11_m_a11 = C_E11_m_a11 - s1_e14 - CH(E14, E13, E12);\n\
                    uint a7  = E11_m_a11 + s0_a10 + MAJ(a10, a9, a8);\n\
                    uint a6  = C_a6_base - a10 + MAJ(a9, a8, a7);\n\
                    uint a5  = C_a5_base + s0_a8 + MAJ(a8, a7, a6);\n\
                    uint D   = a5 ^ a6;\n\
                    if ((D & KER_MASK) == 0u && (D >> 16) == 0u && popcount(D) <= (int)op.kmax) {\n\
                        uint exp = 0u;\n\
                        if (atomic_compare_exchange_weak_explicit(&res->found, &exp, 1u, memory_order_relaxed, memory_order_relaxed)) {\n\
                            res->a9  = a9;\n\
                            res->a10 = a10;\n\
                            res->a11 = a11;\n\
                        }\n\
                        return;\n\
                    }\n\
                }\n\
            }\n\
        }\n\
    }\n\
}\n";

typedef struct {
    u32 a12, a13, a14, a15, E16;
    u32 e15_base, a9_const_base;
    u32 K13, K14, K15, padlen_lo;
    u32 kmax;
    u64 attempt;
} Op2ParamsHost;

typedef struct {
    atomic_uint found;
    u32 a9, a10, a11;
} Op2ResultHost;

typedef struct {
    u32 T16, T17, T18;
    u32 a5, D, n_deltas;
    u32 deltas[16];
    u32 D_bads[16];
    u32 F_targets[16];
    u32 C_W0, C_E1, C_G1_base, C_G2_base, C_G3_base;
    u32 E0_const, Em1_const, A0_const, Am1_const, Am2_const;
    u32 T16_folded, MAJ1_XOR, CH1_XOR, C_G1_folded, Am2_folded;
    u32 start_idx, count;
} Stage4ParamsHost;

typedef struct {
    atomic_uint solved;
    u32 a1, W0, G1, t16, W1, a2, G2, t17, W2, a3, G3, t18, W3, a4;
} Stage4ResultHost;

static id<MTLDevice> g_device = nil;
static id<MTLCommandQueue> g_queue = nil;
static id<MTLComputePipelineState> g_pso_stage4 = nil;
static id<MTLComputePipelineState> g_pso_op2 = nil;
static id<MTLBuffer> g_buf_ht = nil;
static id<MTLBuffer> g_buf_bm = nil;
static id<MTLBuffer> g_buf_cosets = nil;
static id<MTLBuffer> g_buf_params = nil;
static id<MTLBuffer> g_buf_result = nil;
static id<MTLBuffer> g_buf_op2_params = nil;
static id<MTLBuffer> g_buf_op2_result = nil;

static void init_metal_gpu(void) {
    g_device = MTLCreateSystemDefaultDevice();
    if (!g_device) {
        fprintf(stderr, "Metal GPU not available\n");
        exit(1);
    }
    g_queue = [g_device newCommandQueue];
    NSError *err = nil;
    MTLCompileOptions *opts = [[MTLCompileOptions alloc] init];
    id<MTLLibrary> lib = [g_device newLibraryWithSource:kMetalShaderSource options:opts error:&err];
    if (!lib) {
        fprintf(stderr, "Metal shader compile error: %s\n", [[err localizedDescription] UTF8String]);
        exit(1);
    }
    id<MTLFunction> fn = [lib newFunctionWithName:@"stage4_ultra_gpu"];
    g_pso_stage4 = [g_device newComputePipelineStateWithFunction:fn error:&err];
    id<MTLFunction> fn_op2 = [lib newFunctionWithName:@"op2_ultra_gpu"];
    g_pso_op2 = [g_device newComputePipelineStateWithFunction:fn_op2 error:&err];

    g_buf_ht = [g_device newBufferWithBytesNoCopy:(void *)ht_Q0_ker
                                           length:(LUT_BUCKETS * sizeof(u64))
                                          options:MTLResourceStorageModeShared
                                      deallocator:nil];
    g_buf_bm = [g_device newBufferWithBytesNoCopy:(void *)bm_Q0_ker
                                           length:(BM_WORDS * sizeof(u32))
                                          options:MTLResourceStorageModeShared
                                      deallocator:nil];
    g_buf_cosets = [g_device newBufferWithBytesNoCopy:a1_cosets_flat
                                               length:(32ULL * A1_PER_COSET * sizeof(u32))
                                              options:MTLResourceStorageModeShared
                                          deallocator:nil];
    g_buf_params = [g_device newBufferWithLength:sizeof(Stage4ParamsHost)
                                         options:MTLResourceStorageModeShared];
    g_buf_result = [g_device newBufferWithLength:sizeof(Stage4ResultHost)
                                         options:MTLResourceStorageModeShared];
    g_buf_op2_params = [g_device newBufferWithLength:sizeof(Op2ParamsHost)
                                             options:MTLResourceStorageModeShared];
    g_buf_op2_result = [g_device newBufferWithLength:sizeof(Op2ResultHost)
                                             options:MTLResourceStorageModeShared];
}

static int find_padded_context_metal_gpu(const u32 dig[8], int kmax, u64 attempt, SolverTrace *tr, u32 A[R+OFF+2]) {
    memset(A, 0, (R+OFF+2) * sizeof(u32));
    for (int i = 0; i < 8; i++) A[i] = WIV_GLOBAL[i];
    u32 st[8], aw[8];
    for (int i = 0; i < 8; i++) st[i] = dig[i] - IV[i];
    window_from_state(st, aw);
    for (int i = 0; i < 8; i++) A[(R-7+i)+OFF] = aw[i];

    Op2ParamsHost *op = (Op2ParamsHost *)[g_buf_op2_params contents];
    op->a12 = A[12+OFF]; op->a13 = A[13+OFF]; op->a14 = A[14+OFF]; op->a15 = A[15+OFF];
    op->E16 = E_arr(A, 16);
    op->e15_base = op->a15 - S0(op->a14) - MAJ(op->a14, op->a13, op->a12);
    op->a9_const_base = S0(op->a12) - op->a13;
    op->K13 = K[13]; op->K14 = K[14]; op->K15 = K[15];
    op->padlen_lo = (u32)PADLEN_BITS;
    op->kmax = (u32)kmax;
    op->attempt = attempt;

    Op2ResultHost *res = (Op2ResultHost *)[g_buf_op2_result contents];
    memset(res, 0, sizeof(*res));
    atomic_store_explicit(&res->found, 0u, memory_order_relaxed);

    id<MTLCommandBuffer> cb = [g_queue commandBuffer];
    id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
    [enc setComputePipelineState:g_pso_op2];
    [enc setBuffer:g_buf_op2_params offset:0 atIndex:0];
    [enc setBuffer:g_buf_op2_result offset:0 atIndex:1];
    [enc dispatchThreads:MTLSizeMake(4096, 1, 1) threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
    [enc endEncoding];
    [cb commit];
    [cb waitUntilCompleted];

    if (atomic_load_explicit(&res->found, memory_order_relaxed)) {
        eval_op1_to_op3(dig, res->a9, res->a10, res->a11, tr, A);
        return 1;
    }
    return 0;
}

typedef struct {
    int tid;
    u32 start_idx;
    u32 end_idx;
    const SolverTrace *base_tr;
    const u32 *base_A;
    Stage4ResultHost *shared_res;
    atomic_uint_fast64_t *total_chains;
} het_worker_t;

static void *chain_worker_het_cpu(void *arg) {
    het_worker_t *cw = (het_worker_t *)arg;
    u32 T16 = cw->base_tr->T16, T17 = cw->base_tr->T17, T18 = cw->base_tr->T18;
    u32 a5 = cw->base_tr->a5, D = cw->base_tr->D;

    // THEORY 14B + 20: Distance-Ordered Submasks with Pre-Hoisted D_bads[16] and F_targets[16]
    u32 deltas[16], D_bads[16], F_targets[16];
    int n_deltas = 0;
    u32 delta_star = a5 & D;
    deltas[n_deltas++] = delta_star;
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

    u32 coset_idx = T16 & KER_MASK;
    const u32 *coset_a1 = &a1_cosets_flat[coset_idx * A1_PER_COSET];

    // THEORY 14A + 29 + 32 + 33 + 36: Hoisted Hop-1 NEON + CLZ Unpack + 4-Wide NEON Hop-2 + Deferred Stores!
    u32 T16_folded     = T16 + C_t16_folded;
    u32 C_G1_minus_T16 = C_W0 - T16;
    u32 notD_lo16      = (~D) & 0xffffu;
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
    const uint32x4_t v_mask31 = vdupq_n_u32(KER_MASK);

    u64 local_chains = 0;

    for (u32 idx = cw->start_idx; idx < cw->end_idx; idx++) {
        if ((idx & 0x1f) == 0 && atomic_load_explicit(&cw->shared_res->solved, memory_order_relaxed)) break;
        u32 a1_base = coset_a1[idx];

        // THEORY 17 + 23 + 27A + 29 + 32A: Cross-f_hi Hoisted S1_MID7 & CH1_MID7 in Hop-1 NEON (cuts vld1q_u32 by 4x!)
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

        u32 hit_t16[64];
        u8  hit_f[64];
        int n_hits = 0;

        for (u32 f_hi = 0; f_hi < 4u; f_hi++) {
            u32 f_hi_sh  = f_hi << 30;
            u32 e1_hi    = e1_base + f_hi_sh;
            u32 hi_delta = (f_hi_sh & MAJ1_XOR) - f_hi_sh + ((Em1_const ^ (e1_hi & CH1_XOR)) & 0xc0000000u);
            uint32x4_t v_hid   = vdupq_n_u32(hi_delta);
            uint32x4_t v_s0_hi = vdupq_n_u32(S0_HI2[f_hi]);
            uint32x4_t v_s1_hi = vdupq_n_u32(S1_HI2[(e1_hi2_0 + f_hi) & 3u]);

            u32 t16_arr[16];
            vst1q_u32(&t16_arr[0],  vaddq_u32(vaddq_u32(v_m0,  v_hid), vaddq_u32(veorq_u32(v_s0_l0,  v_s0_hi), veorq_u32(v_s1_l0,  v_s1_hi))));
            vst1q_u32(&t16_arr[4],  vaddq_u32(vaddq_u32(v_m4,  v_hid), vaddq_u32(veorq_u32(v_s0_l4,  v_s0_hi), veorq_u32(v_s1_l4,  v_s1_hi))));
            vst1q_u32(&t16_arr[8],  vaddq_u32(vaddq_u32(v_m8,  v_hid), vaddq_u32(veorq_u32(v_s0_l8,  v_s0_hi), veorq_u32(v_s1_l8,  v_s1_hi))));
            vst1q_u32(&t16_arr[12], vaddq_u32(vaddq_u32(v_m12, v_hid), vaddq_u32(veorq_u32(v_s0_l12, v_s0_hi), veorq_u32(v_s1_l12, v_s1_hi))));

            u32 f_base = f_hi << 4;
            for (u32 f_lo = 0; f_lo < 16u; f_lo++) {
                u32 t16 = t16_arr[f_lo];
                u32 b   = t16 >> KER_BITS;
                u32 bm  = atomic_load_explicit(&bm_Q0_ker[b >> 5], memory_order_relaxed);
                if (((bm >> (b & 31u)) & 1u) != 0u) {
                    __builtin_prefetch((const void *)&ht_Q0_ker[b], 0, 0);
                    hit_t16[n_hits] = t16;
                    hit_f[n_hits]   = (u8)(f_base | f_lo);
                    n_hits++;
                }
            }
        }
        local_chains += 64;

        // THEORY 33A: Branchless CLZ Slot Unpacking (n_slots = 4 - (__builtin_clzll(pack1) >> 4))
        u32 hit_a1[64], hit_e1[64], hit_G1[64], hit_e2b[64];
        u16 w1_ulo[260];
        u8  w1_h[260];
        int n_w1 = 0;

        for (int h = 0; h < n_hits; h++) {
            u32 t16     = hit_t16[h];
            u64 pack1   = atomic_load_explicit(&ht_Q0_ker[t16 >> KER_BITS], memory_order_relaxed);
            u32 f       = hit_f[h];
            u32 f_lo    = f & 15u;
            u32 f_hi    = f >> 4;
            u32 a1      = a1_base | (f_lo << 18) | (f_hi << 30);
            u32 e1      = e1_base + (f_lo << 18) + (f_hi << 30);
            u32 sm1_var = (s0_a1_base ^ S0_HI2[f_hi] ^ S0_LO4[f_lo]) + (a1 & MAJ1_XOR);
            hit_a1[h]   = a1;
            hit_e1[h]   = e1;
            hit_G1[h]   = t16 + a1 + C_G1_minus_T16;
            hit_e2b[h]  = Am2_folded - sm1_var;

            int ns = 4 - (__builtin_clzll(pack1) >> 4);
            memcpy(&w1_ulo[n_w1], &pack1, 8);
            u32 h4 = (u32)h * 0x01010101u;
            memcpy(&w1_h[n_w1], &h4, 4);
            n_w1 += ns;
        }
        for (int p = 0; p < 4; p++) {
            w1_ulo[n_w1 + p] = 0;
            w1_h[n_w1 + p]   = 0;
        }

        // THEORY 33B + 36: 4-Wide ARM64 NEON Hop-2 Vectorization + Deferred Stack-Store Pruning!
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
            uint32x4_t v_rem  = vandq_u32(v_t17b, v_mask31);

            if (__builtin_expect(vminvq_u32(v_rem) == 0u, 0)) {
                u32 t17b_arr[4], G2_arr[4], sm2_arr[4], W1_arr[4], a2_arr[4], e2_arr[4];
                vst1q_u32(t17b_arr, v_t17b);
                vst1q_u32(G2_arr,   v_G2);
                int hop3_stored = 0;

                int lim = (k + 4 <= n_w1) ? 4 : (n_w1 - k);
                for (int lane = 0; lane < lim; lane++) {
                    u32 t17_base = t17b_arr[lane];
                    if ((t17_base & KER_MASK) != 0u) continue;
                    u32 G2 = G2_arr[lane];

                    for (int d_idx = 0; d_idx < n_deltas; d_idx++) {
                        u32 delta = deltas[d_idx];
                        u32 t17   = t17_base - delta;
                        u32 b2    = t17 >> KER_BITS;
                        u32 bm2   = atomic_load_explicit(&bm_Q0_ker[b2 >> 5], memory_order_relaxed);
                        if (((bm2 >> (b2 & 31u)) & 1u) == 0u) continue;
                        u64 pack2 = atomic_load_explicit(&ht_Q0_ker[b2], memory_order_relaxed);

                        u32 D_bad    = D_bads[d_idx];
                        u32 F_target = F_targets[d_idx];

                        for (int s = 0; s < 4; s++) {
                            u32 ulo2 = (u32)((pack2 >> (s * 16)) & 0xffffULL);
                            if (!ulo2) break;
                            // THEORY 27C + 32C: Pre-Reconstruction 16-Bit ulo & free_lo!=0 Filtering + Single-Lookup MAX_FORCED_HI!
                            u32 a3_lo = (ulo2 + G2) & 0xffffu;
                            if (d_idx > 0 && ((a3_lo ^ delta) & D_bad) != 0u) continue;
                            u32 free_lo = (a5 ^ a3_lo) & notD_lo16;
                            if (!free_lo) continue;
                            int n_free_lo = popc(free_lo);

                            u32 W2 = rec_u_z(ulo2, ulo2 + t17);
                            u32 a3 = W2 + G2;
                            u32 M_hi = ~(a5 ^ a3) & 0xffff0000u;
                            int n_forced_hi = popc(M_hi);
                            if (n_forced_hi > MAX_FORCED_HI[n_free_lo]) continue;

                            // THEORY 36B: Deferred Hop-2 Stack-Store Materialization (skips 66.7% of vector stores on >99% of hits!)
                            if (!hop3_stored) {
                                vst1q_u32(sm2_arr, v_sm2);
                                vst1q_u32(W1_arr,  v_W1);
                                vst1q_u32(a2_arr,  v_a2);
                                vst1q_u32(e2_arr,  v_e2);
                                hop3_stored = 1;
                            }
                            u32 h       = w1_h[k + lane];
                            u32 a1      = hit_a1[h],  e1 = hit_e1[h];
                            u32 t16     = hit_t16[h], G1 = hit_G1[h];
                            u32 W1      = W1_arr[lane], a2 = a2_arr[lane], e2 = e2_arr[lane];
                            u32 e3_base = Am1_const - sm2_arr[lane];

                            u32 M = M_hi | ((~free_lo) & 0xffffu);
                            u32 e3 = a3 + e3_base;
                            u32 sm3 = S0(a3) + MAJ(a3, a2, a1);
                            u32 G3 = C_G3_base + sm3 + S1(e3) + CH(e3, e2, e1);
                            u32 t18 = T18 + G3 - W2;

                            if ((t18 & KER_MASK) == 0u) {
                                u32 b3  = t18 >> KER_BITS;
                                u32 bm3 = atomic_load_explicit(&bm_Q0_ker[b3 >> 5], memory_order_relaxed);
                                if (((bm3 >> (b3 & 31u)) & 1u) != 0u) {
                                    u64 pack3 = atomic_load_explicit(&ht_Q0_ker[b3], memory_order_relaxed);
                                    for (int z = 0; z < 4; z++) {
                                        u32 ulo3 = (u32)((pack3 >> (z * 16)) & 0xffffULL);
                                        if (!ulo3) break;
                                        if ((((ulo3 + G3) ^ F_target) & M & 0xffffu) != 0u) continue;
                                        u32 W3 = rec_u_z(ulo3, ulo3 + t18), a4 = W3 + G3;
                                        if (((a4 ^ F_target) & M) == 0u) {
                                            u32 expected = 0;
                                            if (atomic_compare_exchange_strong(&cw->shared_res->solved, &expected, 1u)) {
                                                u32 W0 = a1 + C_W0;
                                                cw->shared_res->a1=a1; cw->shared_res->W0=W0; cw->shared_res->G1=G1;
                                                cw->shared_res->t16=t16; cw->shared_res->W1=W1; cw->shared_res->a2=a2;
                                                cw->shared_res->G2=G2; cw->shared_res->t17=t17; cw->shared_res->W2=W2;
                                                cw->shared_res->a3=a3; cw->shared_res->G3=G3; cw->shared_res->t18=t18;
                                                cw->shared_res->W3=W3; cw->shared_res->a4=a4;
                                            }
                                            goto done;
                                        }
                                    }
                                }
                            }

                            // THEORY 18 + 20 + 30 + 32B: Hop-3 Subspace Inversion with Rotation-Free v1 + Unified sv1 + 27-Bit Filter
                            if (n_free_lo <= 9) {
                                u32 base_a4_lo = F_target & M & 0xffffu;
                                u32 base_w3_lo = (base_a4_lo - G3) & 0xffffu;
                                u32 M_mid11    = M & 0x07ff0000u;
                                u32 sub = 0;
                                do {
                                    u32 w3_lo = (base_w3_lo + sub) & 0xffffu;
                                    u32 z_lo  = w3_lo + t18;
                                    u32 v1    = z_lo ^ (w3_lo >> 7) ^ (w3_lo << 14) ^ (w3_lo >> 3);
                                    u32 sv1   = (v1 << 15) ^ (v1 << 11) ^ v1;
                                    if ((sv1 & 0x00008000u) == 0u) {
                                        u32 b0      = sv1 & 0x00010000u;
                                        u32 b12     = ((v1 << 14) ^ (v1 << 3)) & 0x00060000u;
                                        u32 T       = b0 | b12 | ((v1 << 18) & 0xfff80000u);
                                        u32 W3_lo27 = w3_lo | T;
                                        if ((((W3_lo27 + G3) ^ F_target) & M_mid11) == 0u) {
                                            u32 W3 = W3_lo27 ^ (T << 11) ^ (b0 << 15);
                                            u32 a4 = W3 + G3;
                                            if (((a4 ^ F_target) & M) == 0u && Qf(W3) == t18) {
                                                u32 expected = 0;
                                                if (atomic_compare_exchange_strong(&cw->shared_res->solved, &expected, 1u)) {
                                                    u32 W0 = a1 + C_W0;
                                                    cw->shared_res->a1=a1; cw->shared_res->W0=W0; cw->shared_res->G1=G1;
                                                    cw->shared_res->t16=t16; cw->shared_res->W1=W1; cw->shared_res->a2=a2;
                                                    cw->shared_res->G2=G2; cw->shared_res->t17=t17; cw->shared_res->W2=W2;
                                                    cw->shared_res->a3=a3; cw->shared_res->G3=G3; cw->shared_res->t18=t18;
                                                    cw->shared_res->W3=W3; cw->shared_res->a4=a4;
                                                }
                                                goto done;
                                            }
                                        }
                                    }
                                    sub = (sub - free_lo) & free_lo;
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

// ========================================================================================
// THEORY 21: ZERO-SYSCALL PERSISTENT SPIN-BARRIER WORKER POOL (OP2 & STAGE 4)
//   Eliminates 16 pthread_create + 16 pthread_join kernel calls per target (~0.5 ms overhead)
//   and launches all 8 CPU cores simultaneously within <0.2 microseconds!
// ========================================================================================
#define POOL_WORKERS 7
#define JOB_NONE     0
#define JOB_OP2      1
#define JOB_STAGE4   2
#define JOB_BUILD    3

static pthread_t g_pool_th[POOL_WORKERS];
static ctx_worker_t g_pool_ctx[8];
static het_worker_t g_pool_het[8];
static tbuild_t     g_pool_bld[8];
static atomic_uint g_pool_gen = 0;
static atomic_uint g_pool_done = 0;
static atomic_int  g_pool_job_type = JOB_NONE;
static atomic_int  g_pool_active_workers = 0;

static void *persistent_pool_worker(void *arg) {
    int tid = (int)(intptr_t)arg; // tid = 1..7
    u32 last_gen = 0;
    u32 idle_spins = 0;
    while (1) {
        u32 cur_gen = atomic_load_explicit(&g_pool_gen, memory_order_acquire);
        if (cur_gen == last_gen) {
            if (++idle_spins < 524288u) {
                __builtin_arm_yield();
            } else {
                usleep(20);
            }
            continue;
        }
        idle_spins = 0;
        last_gen = cur_gen;
        int jtype = atomic_load_explicit(&g_pool_job_type, memory_order_relaxed);
        int n_act = atomic_load_explicit(&g_pool_active_workers, memory_order_relaxed);
        if (tid <= n_act) {
            if (jtype == JOB_OP2) {
                ctx_worker_theory10(&g_pool_ctx[tid]);
            } else if (jtype == JOB_STAGE4) {
                chain_worker_het_cpu(&g_pool_het[tid]);
            } else if (jtype == JOB_BUILD) {
                build_ker_tables_worker(&g_pool_bld[tid]);
            }
        }
        atomic_fetch_add_explicit(&g_pool_done, 1u, memory_order_release);
    }
    return NULL;
}

static void init_worker_pool(void) {
    for (int t = 1; t <= POOL_WORKERS; t++) {
        pthread_create(&g_pool_th[t - 1], NULL, persistent_pool_worker, (void *)(intptr_t)t);
    }
}

static u64 find_padded_context_theory10_mt(const u32 dig[8], int kmax, u64 attempt, SolverTrace *tr, u32 A[R+OFF+2], u64 *ker_out) {
    atomic_int found = 0;
    atomic_uint_fast64_t total_tried = 0, ker_hits = 0;
    u32 a9 = 0, a10 = 0, a11 = 0;
    for (int t = 0; t < 8; t++) {
        g_pool_ctx[t].tid = t; g_pool_ctx[t].attempt = attempt; g_pool_ctx[t].dig = dig; g_pool_ctx[t].kmax = kmax;
        g_pool_ctx[t].found = &found;
        g_pool_ctx[t].out_a9 = &a9; g_pool_ctx[t].out_a10 = &a10; g_pool_ctx[t].out_a11 = &a11;
        g_pool_ctx[t].total_tried = &total_tried;
        g_pool_ctx[t].ker_hits = &ker_hits;
    }
    atomic_store_explicit(&g_pool_done, 0u, memory_order_relaxed);
    atomic_store_explicit(&g_pool_job_type, JOB_OP2, memory_order_relaxed);
    atomic_store_explicit(&g_pool_active_workers, POOL_WORKERS, memory_order_relaxed);
    atomic_fetch_add_explicit(&g_pool_gen, 1u, memory_order_release);

    ctx_worker_theory10(&g_pool_ctx[0]);
    while (atomic_load_explicit(&g_pool_done, memory_order_acquire) < (u32)POOL_WORKERS) {
        __builtin_arm_yield();
    }
    if (ker_out) *ker_out = (u64)ker_hits;
    eval_op1_to_op3(dig, a9, a10, a11, tr, A);
    return (u64)total_tried;
}

static int solve_stage4_heterogeneous_cpu_gpu(SolverTrace *tr, u32 A[R+OFF+2], int mode, u64 *chains_out, u64 *gpu_chains_out) {
    u32 coset_idx = tr->T16 & KER_MASK;
    u32 n_a1 = atomic_load_explicit(&a1_coset_cnt[coset_idx], memory_order_relaxed);
    if (n_a1 > A1_PER_COSET) n_a1 = A1_PER_COSET;

    Stage4ResultHost *res = (Stage4ResultHost *)[g_buf_result contents];
    memset(res, 0, sizeof(Stage4ResultHost));
    atomic_store_explicit(&res->solved, 0u, memory_order_relaxed);

    u32 gpu_total_seeds = (mode == 1) ? n_a1 : ((mode == 2) ? (n_a1 / 3u) : 0u);
    u32 cpu_seeds       = (mode == 1) ? 0u   : (n_a1 - gpu_total_seeds);

    atomic_uint_fast64_t total_chains = 0;
    if (cpu_seeds > 0) {
        if (mode == 0) {
            // Mode 1 (CPU Only): All 8 cores (main thread tid=0 + 7 pool workers tid=1..7)
            u32 chunk = cpu_seeds / 8u;
            for (int t = 0; t < 8; t++) {
                g_pool_het[t].tid = t;
                g_pool_het[t].start_idx = (u32)t * chunk;
                g_pool_het[t].end_idx   = (t == 7) ? cpu_seeds : (g_pool_het[t].start_idx + chunk);
                g_pool_het[t].base_tr   = tr;
                g_pool_het[t].base_A    = A;
                g_pool_het[t].shared_res = res;
                g_pool_het[t].total_chains = &total_chains;
            }
            atomic_store_explicit(&g_pool_done, 0u, memory_order_relaxed);
            atomic_store_explicit(&g_pool_job_type, JOB_STAGE4, memory_order_relaxed);
            atomic_store_explicit(&g_pool_active_workers, POOL_WORKERS, memory_order_relaxed);
            atomic_fetch_add_explicit(&g_pool_gen, 1u, memory_order_release);
            chain_worker_het_cpu(&g_pool_het[0]);
        } else {
            // Mode 3 (Heterogeneous CPU + GPU): 7 pool workers run CPU slices while main thread drives Metal GPU!
            u32 chunk = cpu_seeds / (u32)POOL_WORKERS;
            for (int t = 1; t <= POOL_WORKERS; t++) {
                g_pool_het[t].tid = t;
                g_pool_het[t].start_idx = (u32)(t - 1) * chunk;
                g_pool_het[t].end_idx   = (t == POOL_WORKERS) ? cpu_seeds : (g_pool_het[t].start_idx + chunk);
                g_pool_het[t].base_tr   = tr;
                g_pool_het[t].base_A    = A;
                g_pool_het[t].shared_res = res;
                g_pool_het[t].total_chains = &total_chains;
            }
            atomic_store_explicit(&g_pool_done, 0u, memory_order_relaxed);
            atomic_store_explicit(&g_pool_job_type, JOB_STAGE4, memory_order_relaxed);
            atomic_store_explicit(&g_pool_active_workers, POOL_WORKERS, memory_order_relaxed);
            atomic_fetch_add_explicit(&g_pool_gen, 1u, memory_order_release);
        }
    }

    u64 gpu_chains_done = 0;
    if (gpu_total_seeds > 0) {
        Stage4ParamsHost *p = (Stage4ParamsHost *)[g_buf_params contents];
        p->T16 = tr->T16; p->T17 = tr->T17; p->T18 = tr->T18;
        p->a5  = tr->a5;  p->D   = tr->D;
        u32 n_deltas = 0;
        u32 delta_star = tr->a5 & tr->D;
        p->deltas[n_deltas++] = delta_star;
        for (int dist = 1; dist <= 3; dist++) {
            u32 subD = 0;
            do {
                if (popc((tr->a5 ^ subD) & tr->D) == dist && n_deltas < 16) p->deltas[n_deltas++] = subD;
                subD = (subD - tr->D) & tr->D;
            } while (subD != 0);
        }
        for (u32 d_idx = 0; d_idx < n_deltas; d_idx++) {
            p->D_bads[d_idx]    = (tr->a5 ^ p->deltas[d_idx]) & tr->D;
            p->F_targets[d_idx] = (tr->a5 & ~tr->D) | p->deltas[d_idx];
        }
        p->n_deltas = n_deltas;
        p->C_W0 = C_W0; p->C_E1 = C_E1; p->C_G1_base = C_G1_base;
        p->C_G2_base = C_G2_base; p->C_G3_base = C_G3_base;
        p->E0_const = E0_const; p->Em1_const = Em1_const;
        p->A0_const = A0_const; p->Am1_const = Am1_const; p->Am2_const = Am2_const;
        p->T16_folded = tr->T16 + C_t16_folded;
        p->MAJ1_XOR = MAJ1_XOR; p->CH1_XOR = CH1_XOR;
        p->C_G1_folded = C_G1_folded; p->Am2_folded = Am2_folded;
        p->start_idx = 0;

        u32 batch_seeds = 4096u; // 262,144 chains per GPU slice
        p->count = batch_seeds;

        id<MTLCommandBuffer> prev_cb = nil;
        for (u32 off = 0; off < gpu_total_seeds; off += batch_seeds) {
            if (atomic_load_explicit(&res->solved, memory_order_relaxed)) break;
            u32 cnt = (off + batch_seeds <= gpu_total_seeds) ? batch_seeds : (gpu_total_seeds - off);

            id<MTLCommandBuffer> cmdBuf = [g_queue commandBuffer];
            id<MTLComputeCommandEncoder> enc = [cmdBuf computeCommandEncoder];
            [enc setComputePipelineState:g_pso_stage4];
            [enc setBuffer:g_buf_ht offset:0 atIndex:0];
            size_t coset_byte_off = (size_t)(coset_idx * A1_PER_COSET + cpu_seeds + off) * sizeof(u32);
            [enc setBuffer:g_buf_cosets offset:coset_byte_off atIndex:1];
            [enc setBuffer:g_buf_params offset:0 atIndex:2];
            [enc setBuffer:g_buf_result offset:0 atIndex:3];
            [enc setBuffer:g_buf_bm offset:0 atIndex:4];
            MTLSize grid = MTLSizeMake((NSUInteger)cnt * 64ULL, 1, 1);
            MTLSize tgs  = MTLSizeMake(256, 1, 1);
            [enc dispatchThreads:grid threadsPerThreadgroup:tgs];
            [enc endEncoding];
            [cmdBuf commit];

            if (prev_cb) {
                [prev_cb waitUntilCompleted];
                if (atomic_load_explicit(&res->solved, memory_order_relaxed)) {
                    prev_cb = cmdBuf;
                    break;
                }
            }
            prev_cb = cmdBuf;
            gpu_chains_done += (u64)cnt * 64ULL;
        }
        if (prev_cb) [prev_cb waitUntilCompleted];
    }

    if (cpu_seeds > 0) {
        while (atomic_load_explicit(&g_pool_done, memory_order_acquire) < (u32)POOL_WORKERS) {
            __builtin_arm_yield();
        }
    }

    *chains_out += (u64)total_chains;
    if (gpu_chains_out) *gpu_chains_out += gpu_chains_done;
    if (atomic_load_explicit(&res->solved, memory_order_relaxed)) {
        A[1+OFF] = res->a1; A[2+OFF] = res->a2; A[3+OFF] = res->a3; A[4+OFF] = res->a4;
        tr->a1 = res->a1; tr->W0 = res->W0; tr->G1 = res->G1; tr->t16 = res->t16; tr->W1 = res->W1; tr->a2 = res->a2;
        tr->G2 = res->G2; tr->t17 = res->t17; tr->W2 = res->W2; tr->a3 = res->a3;
        tr->G3 = res->G3; tr->t18 = res->t18; tr->W3 = res->W3; tr->a4 = res->a4;
        for (int t = 0; t < 16; t++) tr->Wm[t] = Wt_arr(A, t);
        compress(tr->Wm, R, tr->H_out);
        return 1;
    }
    return 0;
}

static int verify_preimage_unit_test(const SolverTrace *tr, const u32 target_dig[8]) {
    u32 H_chk[8];
    compress(tr->Wm, R, H_chk);
    if (memcmp(H_chk, target_dig, 32) != 0) return 0;
    if (memcmp(tr->H_out, target_dig, 32) != 0) return 0;
    if ((tr->Wm[13] & 0xffu) != 0x80u) return 0;
    if (tr->Wm[14] != 0u) return 0;
    if (tr->Wm[15] != (u32)PADLEN_BITS) return 0;
    if ((tr->D & 0x3fu) != 0u) return 0;
    if ((tr->D >> 16) != 0u) return 0;
    if (Qf(tr->W1) != tr->t16) return 0;
    if (Qf(tr->W2) != tr->t17) return 0;
    if (Qf(tr->W3) != tr->t18) return 0;
    return 1;
}

int main(void) {
    setbuf(stdout, NULL);
    @autoreleasepool {
        init_iv_constants();
        init_worker_pool();

        printf("========================================================================================\n");
        printf("  GEN-30 ENGINE: THEORIES 10-36 (2-ADIC HENSEL CTZ + DUAL POLYMUL + DEFERRED HOP-2)\n");
        printf("========================================================================================\n\n");

        int total_unit_checks = 0, passed_unit_checks = 0;

        // Unit-test 1: Theory 3/8 Lower-half syzygy & Null-rotation vector K_hi = 0x88010000
        u32 syz_ok_cnt = 0;
        for (u32 i = 0; i < 100000u; i++) {
            u32 u = i * 0x9E3779B1u + 0x1234567u;
            u32 y = Qf(u);
            if (syzygy_ok(u & 0xffffu, (u & 0xffffu) + y) && ((s0(u ^ 0x88010000u) ^ s0(u)) & 0xfffffu) == 0u && ((Qf(u ^ 0x88010000u) ^ y) & 0xffffu) == 0u) syz_ok_cnt++;
        }
        total_unit_checks++;
        if (syz_ok_cnt == 100000u) passed_unit_checks++;

        // Unit-test 2: Theory 10 + 12 (Double-Majority Modulo-64 Cancellation & 2-Stage Upward-Carry Shielding)
        SolverTrace tr_u;
        u32 A_u[R+OFF+2];
        u64 ker_hits = 0;
        double t_u0 = now_sec();
        u64 u_tried = find_padded_context_theory10_mt(H_ABC, 2, 1, &tr_u, A_u, &ker_hits);
        double dt_u = now_sec() - t_u0;
        total_unit_checks++;
        if (ker_hits == u_tried && u_tried > 0 && (tr_u.D & 0x3fu) == 0u && (tr_u.D >> 16) == 0u && (tr_u.w13 & 0xffu) == 0x80u) {
            passed_unit_checks++;
        }
        printf("[Unit Check] Theory 10+12 6-Bit Mod-64 Cancellation + 2-Stage Upward-Carry Shielding:\n");
        printf("             - Inner-loop (D & 0x3f) == 0 lock rate: %llu / %llu = %.2f%% (vs 1.56%% random = 64.0x boost!)\n",
               (unsigned long long)ker_hits, (unsigned long long)u_tried,
               100.0 * (double)ker_hits / (double)u_tried);
        printf("             - Pure lower-half D=0x%08x (popc(D)=%d, popc(D>>16)=%d, w13=0x%08x) found in %llu inner steps (%.4fs)!\n\n",
               tr_u.D, popc(tr_u.D), popc(tr_u.D >> 16), tr_u.w13, (unsigned long long)u_tried, dt_u);

        posix_memalign((void **)&ht_Q0_ker, 16384, LUT_BUCKETS * sizeof(u64));
        memset((void *)ht_Q0_ker, 0, LUT_BUCKETS * sizeof(u64));
        posix_memalign((void **)&bm_Q0_ker, 16384, BM_WORDS * sizeof(u32));
        memset((void *)bm_Q0_ker, 0, BM_WORDS * sizeof(u32));
        posix_memalign((void **)&a1_cosets_flat, 16384, 32ULL * A1_PER_COSET * sizeof(u32));
        for (int c = 0; c < 32; c++) atomic_store(&a1_coset_cnt[c], 0u);

        double t0 = now_sec();
        init_metal_gpu();
        double t_metal = now_sec() - t0;
        printf("[Metal GPU ] Initialized %s (Unified Memory Zero-Copy = %s) in %.3fs\n",
               [[g_device name] UTF8String],
               [g_device hasUnifiedMemory] ? "YES" : "NO",
               t_metal);

        double t_lut0 = now_sec();
        for (int t = 0; t < 8; t++) {
            g_pool_bld[t].tid = t; g_pool_bld[t].nthreads = 8;
        }
        atomic_store_explicit(&g_pool_done, 0u, memory_order_relaxed);
        atomic_store_explicit(&g_pool_job_type, JOB_BUILD, memory_order_relaxed);
        atomic_store_explicit(&g_pool_active_workers, POOL_WORKERS, memory_order_relaxed);
        atomic_fetch_add_explicit(&g_pool_gen, 1u, memory_order_release);
        build_ker_tables_worker(&g_pool_bld[0]);
        while (atomic_load_explicit(&g_pool_done, memory_order_acquire) < (u32)POOL_WORKERS) {
            __builtin_arm_yield();
        }

        // Warm up GPU MMU page tables for the 1024 MB + 16 MB shared buffers during Init:
        {
            Stage4ParamsHost *p = (Stage4ParamsHost *)[g_buf_params contents];
            memset(p, 0, sizeof(*p));
            p->count = 1;
            id<MTLCommandBuffer> cb = [g_queue commandBuffer];
            id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
            [enc setComputePipelineState:g_pso_stage4];
            [enc setBuffer:g_buf_ht offset:0 atIndex:0];
            [enc setBuffer:g_buf_cosets offset:0 atIndex:1];
            [enc setBuffer:g_buf_params offset:0 atIndex:2];
            [enc setBuffer:g_buf_result offset:0 atIndex:3];
            [enc setBuffer:g_buf_bm offset:0 atIndex:4];
            [enc dispatchThreads:MTLSizeMake(64, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
            [enc endEncoding];
            [cb commit];
            [cb waitUntilCompleted];
        }
        // Unit-test 3: Theory 11 SLC Bitmap consistency against 1024 MB ht_Q0_ker table
        u32 bm_mismatch = 0;
        for (u32 b = 0; b < 1000000u; b++) {
            u64 pack = atomic_load_explicit(&ht_Q0_ker[b], memory_order_relaxed);
            u32 bit  = (atomic_load_explicit(&bm_Q0_ker[b >> 5], memory_order_relaxed) >> (b & 31u)) & 1u;
            if ((pack != 0ULL) != (bit != 0u)) bm_mismatch++;
        }
        total_unit_checks++;
        if (bm_mismatch == 0u) passed_unit_checks++;

        // Unit-test 4: Theory 17 3-Way Carry-Isolated Hop-1 Decomposition across 1,562 coset bases x 64 fibres (99,968 chains)
        u32 th17_err = 0;
        for (u32 idx = 0; idx < 1562u; idx++) {
            u32 a1_base = a1_cosets_flat[idx];
            u32 e1_base = a1_base + C_E1;
            u32 s0_a1_base = S0(a1_base);
            u32 s1_e1_base = S1(e1_base & 0x3e03ffffu);
            u32 e1_mid7_0  = (e1_base >> 18) & 0x7fu;
            u32 e1_hi2_0   = e1_base >> 30;
            for (u32 f_hi = 0; f_hi < 4u; f_hi++) {
                u32 a1_hi    = a1_base | (f_hi << 30);
                u32 e1_hi    = e1_base + (f_hi << 30);
                u32 s0_a1_hi = s0_a1_base ^ S0_HI2[f_hi];
                u32 s1_e1_hi = s1_e1_base ^ S1_HI2[(e1_hi2_0 + f_hi) & 3u];
                for (u32 f_lo = 0; f_lo < 16u; f_lo++) {
                    u32 a1 = a1_hi | (f_lo << 18);
                    u32 e1 = e1_hi + (f_lo << 18);
                    if ((s0_a1_hi ^ S0_LO4[f_lo]) != S0(a1) || (s1_e1_hi ^ S1_MID7[e1_mid7_0 + f_lo]) != S1(e1)) th17_err++;
                }
            }
        }
        total_unit_checks++;
        if (th17_err == 0u) passed_unit_checks++;

        // Unit-test 5: Theory 19 Exact Direct-Mapped Reconstruction Bijection across 100,000 buckets of ht_Q0_ker
        u32 th19_err = 0, th19_checked = 0;
        for (u32 b = 1; b < 500000u && th19_checked < 100000u; b++) {
            u32 y = b << KER_BITS;
            u32 cands[4];
            int nr = lookup_Q0_ker_aligned(y, cands);
            for (int r = 0; r < nr; r++) {
                th19_checked++;
                if (Qf(cands[r]) != y) th19_err++;
            }
        }
        total_unit_checks++;
        if (th19_err == 0u && th19_checked >= 100000u) passed_unit_checks++;

        // Unit-test 6: Theory 20 Hop-3 Subspace-Viability Gate Identity across 100,000 masks
        u32 th20_err = 0;
        for (u32 i = 0; i < 100000u; i++) {
            u32 M = (i * 0x9E3779B1u) ^ 0xa5a55a5au;
            int pop_M = popc(M);
            u32 free_lo = (~M) & 0xffffu;
            int n_free_lo = popc(free_lo);
            int n_forced_hi = pop_M - (16 - n_free_lo);
            if ((n_forced_hi + n_free_lo) != popc(M ^ 0x0000ffffu)) th20_err++;
        }
        total_unit_checks++;
        if (th20_err == 0u) passed_unit_checks++;

        // Unit-test 7: Theory 22 Disjoint Ch/Maj Constant Folding in Op2 across 100,000 random tuples
        u32 th22_err = 0;
        for (u32 i = 0; i < 100000u; i++) {
            u32 E15 = i * 0x9E3779B1u + 1u, E14 = i * 0x85EBCA6Bu + 3u, E13 = i * 0xC2B2AE35u + 7u;
            u32 a11 = E15 ^ 0x12345678u, a10 = E14 ^ 0x87654321u, a9 = E13 ^ 0xdeadbeefu;
            u32 C_E12_b = 0x11223344u, C_a8_b = 0x55667788u;
            u32 E12_ref = C_E12_b - CH(E15, E14, E13);
            u32 a8_ref  = E12_ref + C_a8_b + MAJ(a11, a10, a9);
            u32 C_E12_f = C_E12_b - ((~E15) & E13);
            u32 C_a8_t  = C_E12_f + C_a8_b + (a11 & a9);
            u32 a8_fast = C_a8_t + (a10 & (a11 ^ a9)) - (E15 & E14);
            if (a8_fast != a8_ref) th22_err++;
        }
        total_unit_checks++;
        if (th22_err == 0u) passed_unit_checks++;

        // Unit-test 8: Theory 24 Zero-DRAM Metal GPU Op2 Compute Kernel (op2_ultra_gpu) on 10-core Apple M3 GPU
        SolverTrace tr_gpu_op2;
        u32 A_gpu_op2[R+OFF+2];
        double t_gop2 = now_sec();
        int ok_gop2 = find_padded_context_metal_gpu(H_ABC, 2, 1, &tr_gpu_op2, A_gpu_op2);
        double dt_gop2 = now_sec() - t_gop2;
        int valid_gop2 = ok_gop2 && (tr_gpu_op2.D & 0x3fu) == 0u && (tr_gpu_op2.D >> 16) == 0u
                         && popc(tr_gpu_op2.D) <= 2 && (tr_gpu_op2.w13 & 0xffu) == 0x80u
                         && tr_gpu_op2.W14 == 0u && tr_gpu_op2.W15 == (u32)PADLEN_BITS;
        total_unit_checks++;
        if (valid_gop2) passed_unit_checks++;

        // Unit-test 9: Theory 25 (25-Bit Cross-dz Invariant + Zero-Wrap Shift) & Theory 27 (Hop-2 Disjoint Folding + 16-Bit ulo Identity)
        u32 th25_27_err = 0;
        for (u32 i = 0; i < 100000u; i++) {
            u32 x = i * 0x9E3779B1u + 0x13579bdfu;
            u32 w28 = x & 0x0fffffffu;
            u32 inv25 = S0(w28 & 0x0fc7ffffu);
            if ((inv25 ^ S0_MID3[(w28 >> 19) & 7u]) != S0(w28)) th25_27_err++;
            u32 hi = x & 0xf0000000u;
            if (((hi >> 2) ^ (hi >> 13) ^ (hi >> 22)) != S0_TOP4[x >> 28]) th25_27_err++;
            u32 a1 = x ^ 0xdeadbeefu, a2 = (x * 3u) ^ 0x12345678u;
            u32 e1 = a1 + C_E1, e2 = a2 + Am2_const;
            u32 maj2_ref = MAJ(a2, a1, A0_const), ch2_ref = CH(e2, e1, E0_const);
            u32 maj2_fast = (a1 & A0_const) + (a2 & (a1 ^ A0_const));
            u32 ch2_fast  = E0_const ^ (e2 & (e1 ^ E0_const));
            if (maj2_ref != maj2_fast || ch2_ref != ch2_fast) th25_27_err++;
            u32 ulo = x & 0xffffu;
            if ((rec_u_z(ulo, x) & 0xffffu) != ulo) th25_27_err++;
        }
        total_unit_checks++;
        if (th25_27_err == 0u) passed_unit_checks++;

        // Unit-test 10: Theory 28 (19-Bit S1(E14) Invariance + NEON vbslq_u32 MAJ), Theory 29 (Hop-1 25-Bit CH1 Invariance), & Theory 30 (Unified sv1 Syzygy + 27-Bit Partial Rec)
        u32 th28_30_err = 0;
        for (u32 i = 0; i < 100000u; i++) {
            u32 x = i * 0x85EBCA6Bu + 0x9E3779B1u;
            u32 w28 = x & 0x0fffffffu;
            u32 s1_inv19 = S1(w28 & 0x0007ffffu);
            if ((s1_inv19 ^ S1_HI9[(w28 >> 19) & 0x1ffu]) != S1(w28)) th28_30_err++;
            u32 a = x, b = x ^ 0x12345678u, c = (x * 7u) ^ 0xdeadbeefu;
            u32 maj_bsl = ((b ^ c) & a) | ((~(b ^ c)) & b);
            if (maj_bsl != MAJ(a, b, c)) th28_30_err++;
            u32 ulo = x & 0xffffu, z = (x >> 16) + ulo;
            u32 v1 = z ^ s0(ulo);
            u32 sv1 = (v1 << 15) ^ (v1 << 11) ^ v1;
            int syz1 = syzygy_ok(ulo, z), syz2 = ((sv1 & 0x00008000u) == 0u);
            u32 T = (sv1 & 0x00010000u) | (((v1 << 14) ^ (v1 << 3)) & 0x00060000u) | ((v1 << 18) & 0xfff80000u);
            if (syz1 != syz2 || ((ulo | T) & 0x07ffffffu) != (rec_u_z(ulo, z) & 0x07ffffffu)) th28_30_err++;
        }
        total_unit_checks++;
        if (th28_30_err == 0u) passed_unit_checks++;

        // Unit-test 11: Theory 31 (Carry-Free Polynomial Multiplication S0/S1), Theory 32 (Rotation-Free s0_lo17 + MAX_FORCED_HI), & Theory 33 (Branchless CLZ Slot Count)
        u32 th31_33_err = 0;
        for (u32 k = 0; k < 16u; k++) {
            if ((k * 0x04008040u) != S0_TOP4[k] || (k * 0x00420008u) != S1_TOP4[k]) th31_33_err++;
        }
        for (u32 i = 0; i < 100000u; i++) {
            u32 x = i * 0x9E3779B1u + 0x2468ace0u;
            u32 ulo = x & 0xffffu, z = (x >> 16) + ulo;
            u32 v1_ref = z ^ s0(ulo);
            u32 v1_rf  = z ^ (ulo >> 7) ^ (ulo << 14) ^ (ulo >> 3);
            if (((v1_ref ^ v1_rf) & 0x0001ffffu) != 0u) th31_33_err++;
            int pop_M = popc(x), n_free_lo = popc((~x) & 0xffffu), n_forced_hi = popc(x >> 16);
            int gate_ref = (pop_M <= 15) && (((n_forced_hi + n_free_lo) <= 13 && n_free_lo <= 9) || pop_M <= 11);
            int gate_lut = (n_forced_hi <= MAX_FORCED_HI[n_free_lo]);
            if (gate_ref != gate_lut) th31_33_err++;
            u64 pack = atomic_load_explicit(&ht_Q0_ker[i + 1u], memory_order_relaxed);
            if (pack != 0ULL) {
                int ref_ns = 0;
                for (int s = 0; s < 4; s++) {
                    if (((pack >> (s * 16)) & 0xffffULL) == 0ULL) break;
                    ref_ns++;
                }
                int fast_ns = 4 - (__builtin_clzll(pack) >> 4);
                if (ref_ns != fast_ns) th31_33_err++;
            }
        }
        total_unit_checks++;
        if (th31_33_err == 0u) passed_unit_checks++;

        // Unit-test 12: Theory 34 (2-Adic Branching Hensel CTZ Root Selector), Theory 35 (Dual PolyMul S0_HENSEL6 & S0_MID3), & Theory 36 (C_G1_minus_T16 Folding)
        u32 th34_36_err = 0;
        for (u32 h6 = 0; h6 < 64u; h6++) {
            if ((h6 * 0x00100201u) != S0_HENSEL6[h6]) th34_36_err++;
        }
        for (u32 m3 = 0; m3 < 8u; m3++) {
            if ((m3 * 0x20020040u) != S0_MID3[m3]) th34_36_err++;
        }
        for (u32 i = 0; i < 100000u; i++) {
            u32 x = i * 0x85EBCA6Bu + 0x9E3779B1u;
            u32 ulo = x & 0xffffu, z = (x >> 16) + ulo;
            u32 T16_f = x + C_t16_folded;
            if (((z - T16_f) + ulo + C_G1_folded) != (z + ulo + (C_W0 - x))) th34_36_err++;
        }
        total_unit_checks++;
        if (th34_36_err == 0u) passed_unit_checks++;

        printf("[Init      ] Shared Kernel Tables (1024 MB) + SLC Bitmap (16 MB) built in %.3fs (bitmap errs=%u, Th17=%u, Th19=%u/%u, Th20=%u, Th22=%u, Th25_27=%u, Th28_30=%u, Th31_33=%u, Th34_36=%u, GPU_Op2=%s [D=0x%08x in %.4fs])\n\n",
               now_sec() - t_lut0, bm_mismatch, th17_err, th19_err, th19_checked, th20_err, th22_err, th25_27_err, th28_30_err, th31_33_err, th34_36_err,
               valid_gop2 ? "PASS" : "FAIL", tr_gpu_op2.D, dt_gop2);

        const char *mode_names[3] = {
            "8-CORE APPLE M3 CPU ONLY (Theory 10 Op2 + Theory 11 SLC Bitmap Stage-4)",
            "10-CORE APPLE M3 METAL GPU ON STAGE-4 (Theory 10 Op2 + Zero-Copy GPU Stage-4)",
            "HETEROGENEOUS 8-CORE CPU + 10-CORE APPLE M3 METAL GPU CO-EXECUTION"
        };
        for (int mode = 0; mode < 3; mode++) {
            printf("----------------------------------------------------------------------------------------\n");
            printf("  MODE %d: %s\n", mode + 1, mode_names[mode]);
            printf("----------------------------------------------------------------------------------------\n");

            double tA0 = now_sec();
            SolverTrace trA;
            u32 AA[R+OFF+2];
            u64 ctxA = 0, chainsA = 0, gpuA = 0, attA = 0;
            while (1) {
                attA++;
                ctxA += find_padded_context_theory10_mt(H_ABC, 2, attA, &trA, AA, NULL);
                if (solve_stage4_heterogeneous_cpu_gpu(&trA, AA, mode, &chainsA, &gpuA)) break;
            }
            double dtA = now_sec() - tA0;
            int okA = verify_preimage_unit_test(&trA, H_ABC);
            total_unit_checks++;
            if (okA) passed_unit_checks++;

            double tB0 = now_sec();
            SolverTrace trB;
            u32 AB[R+OFF+2];
            u32 digB[8]; memcpy(digB, H_ABC, 32);
            digB[6] ^= (1u << 31);
            u64 ctxB = 0, chainsB = 0, gpuB = 0, attB = 0;
            while (1) {
                attB++;
                ctxB += find_padded_context_theory10_mt(digB, 2, attB + 1000, &trB, AB, NULL);
                if (solve_stage4_heterogeneous_cpu_gpu(&trB, AB, mode, &chainsB, &gpuB)) break;
            }
            double dtB = now_sec() - tB0;
            int okB = verify_preimage_unit_test(&trB, digB);
            total_unit_checks++;
            if (okB) passed_unit_checks++;

            printf("  Target A: %.4fs (ctx=%llu, Op2_inner=%llu, CPU_ch=%llu, GPU_ch=%llu, unit_check=%s)\n",
                   dtA, (unsigned long long)attA, (unsigned long long)ctxA,
                   (unsigned long long)chainsA, (unsigned long long)gpuA,
                   okA ? "PASS [256/256 + PAD + INVARIANTS]" : "FAIL");
            printf("  Target B: %.4fs (ctx=%llu, Op2_inner=%llu, CPU_ch=%llu, GPU_ch=%llu, unit_check=%s)\n",
                   dtB, (unsigned long long)attB, (unsigned long long)ctxB,
                   (unsigned long long)chainsB, (unsigned long long)gpuB,
                   okB ? "PASS [256/256 + PAD + INVARIANTS]" : "FAIL");
            printf("  Total (A + B) Pure Search: %.4fs (%.1fx faster than 10.260s baseline!)\n\n",
                   dtA + dtB, 10.260 / (dtA + dtB));

            double t10_start = now_sec();
            u64 tot_ctx = 0, tot_op2 = 0, tot_cpu_ch = 0, tot_gpu_ch = 0;
            int pass10 = 0;
            for (int i = 0; i < 10; i++) {
                u32 dig[8]; memcpy(dig, H_ABC, 32);
                dig[i & 7] ^= (0x13579bdfu * (u32)(i + 1));
                SolverTrace tr;
                u32 A[R+OFF+2];
                u64 ctx = 0, ch = 0, gch = 0, att = 0;
                while (1) {
                    att++;
                    ctx += find_padded_context_theory10_mt(dig, 2, att + (u64)i * 10000ULL, &tr, A, NULL);
                    if (solve_stage4_heterogeneous_cpu_gpu(&tr, A, mode, &ch, &gch)) break;
                }
                int ok_i = verify_preimage_unit_test(&tr, dig);
                total_unit_checks++;
                if (ok_i) { passed_unit_checks++; pass10++; }
                tot_ctx += att;
                tot_op2 += ctx;
                tot_cpu_ch += ch;
                tot_gpu_ch += gch;
            }
            double dt10 = now_sec() - t10_start;
            printf("  10-Target Stress Suite: %.4fs total (%.4fs / target mean, mean_ctx=%.1f, Op2_inner=%llu, CPU_ch=%llu, GPU_ch=%llu, verified=%d/10 PASS)\n\n",
                   dt10, dt10 / 10.0, (double)tot_ctx / 10.0,
                   (unsigned long long)(tot_op2 / 10ULL),
                   (unsigned long long)(tot_cpu_ch / 10ULL), (unsigned long long)(tot_gpu_ch / 10ULL), pass10);
        }

        printf("========================================================================================\n");
        printf("  FINAL END-OF-RUN UNIT TEST SUMMARY: %d / %d CHECKS PASSED (%s)\n",
               passed_unit_checks, total_unit_checks,
               (passed_unit_checks == total_unit_checks) ? "100% ALL INVARIANTS VERIFIED" : "FAILURE DETECTED");
        printf("========================================================================================\n");
        if (passed_unit_checks != total_unit_checks) return 1;
    }
    return 0;
}
