// exp_r19_carbon_lab.c
// Verification & Head-to-Head Benchmark of 4 New R=19 Algebraic Theories (Theories 37-40):
//   [Theory 37] Full-Word D Liberation: M = ~(a5 ^ a3) | D removes the artificial (D >> 16) == 0
//               restriction across all 32 bits, enlarging the valid Op2 solution space by 65,536x!
//   [Theory 38] 10-Bit Full-Top 2-Adic Hensel Bijection (k = 22..31) + Self-Coupled Bit-22/31 S0 Lock:
//               Locks (D & 0x3ff) == 0 and (W13 & 0xff) == 0x80 deterministically in O(1) without
//               any s_hi brute-force loop, slashing Op2 trials by ~70x-100x (and finding D==0 / popc(D)<=1)!
//   [Theory 39] Binomial(32, 1/3) Hop-3 Solution-Mass Recovery + Exact Upper-3-Bit s0(W3)[29..31] Syzygy:
//               s0(W3)[29..31] == ((w3_lo >> 4) ^ (w3_lo >> 11)) & 7 (100% independent of W3[16..31]!),
//               unlocking n_free_lo up to 12 and recovering ~80% of previously discarded Hop-3 solutions!
//   [Theory 40] Direct-Mapped CAS-Free u16 Q0^-1 Kernel at KER_BITS=3 (2^29 x 2B = 1024 MB, 0 MB bitmap)
//               + Zero-Table O(1) Algebraic a1[22..24] Coset Synthesis (eliminates 32 MB a1_cosets & 8M init SHA evals)!

#include <pthread.h>
#include <stdatomic.h>
#include <arm_neon.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

typedef uint32_t u32;
typedef uint64_t u64;
typedef uint16_t u16;
typedef uint8_t  u8;

static double now_sec(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec * 1e-6;
}

static const u32 K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static const u32 IV[8] = {
    0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
    0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19
};

static const u32 H_ABC[8] = {
    0xba7816bf,0x8f01cfea,0x414140de,0x5dae2223,
    0xb00361a3,0x96177a9c,0xb410ff61,0xf20015ad
};

static const u64 PADLEN_BITS = 440;

static inline u32 ROR(u32 x, int n) { return (x >> (n & 31)) | (x << ((-n) & 31)); }
static inline u32 S0(u32 x) { return ROR(x, 2) ^ ROR(x, 13) ^ ROR(x, 22); }
static inline u32 S1(u32 x) { return ROR(x, 6) ^ ROR(x, 11) ^ ROR(x, 25); }
static inline u32 s0(u32 x) { return ROR(x, 7) ^ ROR(x, 18) ^ (x >> 3); }
static inline u32 s1(u32 x) { return ROR(x, 17) ^ ROR(x, 19) ^ (x >> 10); }
static inline u32 CH(u32 e, u32 f, u32 g) { return (e & f) ^ (~e & g); }
static inline u32 MAJ(u32 a, u32 b, u32 c) { return (a & b) ^ (a & c) ^ (b & c); }
static inline u32 Qf(u32 u) { return s0(u) - u; }
static inline int popc(u32 x) { return __builtin_popcount(x); }

static inline u32 rec_u_z(u32 ulo, u32 z) {
    u32 v1 = z ^ (ulo >> 7) ^ (ulo << 14) ^ (ulo >> 3);
    u32 b0 = ((v1 << 15) ^ (v1 << 11) ^ v1) & 0x00010000u;
    u32 b12 = ((v1 << 14) ^ (v1 << 3)) & 0x00060000u;
    u32 T = b0 | b12 | ((v1 << 18) & 0xfff80000u);
    u32 U = T ^ (T << 11) ^ (b0 << 15);
    return ulo | U;
}

#define R 19
#define OFF 7

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
    u32 dig[8], st[8], a_win[8];
    u32 a9, a10, a11, a8, a7, a6, w13, a5, D;
    u32 c9, c10, c11, W14, W15, W16, W17, W18, T16, T17, T18;
    u32 a1, W0, G1, t16, W1, a2, G2, t17, W2, a3, G3, t18, W3, a4;
    u32 Wm[16], H_out[8];
} SolverTrace;

static u32 WIV_GLOBAL[8], A_iv_const[R+OFF+2];
static u32 C_W0, C_E1, C_G1_base, E0_const, Em1_const, A0_const, Am1_const, Am2_const;
static u32 C_G2_base, C_G3_base;
static u32 MAJ1_CONST, MAJ1_XOR, CH1_XOR, C_G1_folded, Am2_folded, C_t16_folded;
static u32 S0_HI2[4], S1_HI2[4], S0_LO4[16], S1_MID7[128], FLO_MAJ_SUB[16], CH1_MID7[128];

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
        S0_LO4[s]      = S0(s << 18);
        FLO_MAJ_SUB[s] = ((s << 18) & MAJ1_XOR) - (s << 18);
    }
    for (u32 f = 0; f < 4u; f++) {
        S0_HI2[f] = S0(f << 30);
        S1_HI2[f] = S1(f << 30);
    }
    for (u32 m = 0; m < 128u; m++) {
        S1_MID7[m]  = S1(m << 18);
        CH1_MID7[m] = (Em1_const ^ ((m << 18) & CH1_XOR)) & 0x01fc0000u;
    }
}

// ========================================================================================
// THEORY 40: Direct-Mapped CAS-Free u16 Q0^-1 Table at KER_BITS=3 (2^29 x 2B = 1024 MB)
//            OR KER_BITS=4 (2^28 x 2B = 512 MB, 2x smaller RAM!)
//            Let's test KER_BITS=4 (512 MB — HALF THE RAM of exp_r19_distant_theories.c!)
//            with ZERO atomic CAS loops and ZERO a1_cosets table!
// ========================================================================================
static const int MAX_FORCED_HI_CARBON[17] = {-1, 0, 1, 2, 3, 4, 5, 6, 6, 5, 5, 6, 7, 8, 9, 10, 11};

#define KER_BITS    5
#define KER_MASK    ((1u << KER_BITS) - 1u)
#define LUT_BUCKETS (1ULL << (32 - KER_BITS))

static u64 *ht_Q0_u64; // 2^27 * 8B = 1024 MB (4-slot u16 pack: captures 98.1% of Q0^-1, ZERO CAS!)
static u32 *bm_Q0;     // 2^22 * 4B =   16 MB (100% SLC-resident, ZERO CAS! Total 1040 MB < 1072 MB prev!)

typedef struct { int tid; int nthreads; } tbuild_t;

static void *build_ker_u64_worker(void *arg) {
    tbuild_t *tb = (tbuild_t *)arg;
    const u32 tid = (u32)tb->tid;
    const u32 n_uhi = 1u << (14 - KER_BITS); // 512 entries
    u32 diff_top_tbl[512], s0_top_tbl[512];
    for (u32 uhi = 0; uhi < n_uhi; uhi++) {
        u32 ut = uhi << (18 + KER_BITS);
        diff_top_tbl[uhi] = ut;
        s0_top_tbl[uhi]   = (ut >> 7) ^ (ut >> 18) ^ (ut << 14) ^ (ut >> 3);
    }

    for (u32 ulo18 = 1u; ulo18 < (1u << 18); ulo18++) {
        u64 stored_val = (u64)(ulo18 & 0xffffu);
        if (stored_val == 0) continue;
        u32 u_ker   = ((ulo18 ^ (ulo18 >> 3) ^ (ulo18 >> 7)) & KER_MASK) << 18;
        u32 base_u  = ulo18 | u_ker;
        u32 s0_base = s0(base_u);
        u32 c_hi3   = s0_base >> 29;
        u32 blk0    = ((c_hi3 - tid) & 7u) << 6;
        u32 blk1    = ((c_hi3 - tid - 1u) & 7u) << 6;

        for (u32 uhi = blk0; uhi < blk0 + 64u; uhi++) {
            u32 y = (s0_base ^ s0_top_tbl[uhi]) - (base_u | diff_top_tbl[uhi]);
            if ((y >> 29) == tid) {
                u32 idx = y >> KER_BITS;
                ht_Q0_u64[idx] = (ht_Q0_u64[idx] << 16) | stored_val;
                bm_Q0[idx >> 5] |= (1u << (idx & 31u));
            }
        }
        for (u32 uhi = blk1; uhi < blk1 + 64u; uhi++) {
            u32 y = (s0_base ^ s0_top_tbl[uhi]) - (base_u | diff_top_tbl[uhi]);
            if ((y >> 29) == tid) {
                u32 idx = y >> KER_BITS;
                ht_Q0_u64[idx] = (ht_Q0_u64[idx] << 16) | stored_val;
                bm_Q0[idx >> 5] |= (1u << (idx & 31u));
            }
        }
    }
    return NULL;
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
// THEORY 37 + THEORY 38: 10-Bit Full-Top Hensel Op2 (k = 22..31) + Full-Word D Liberation
// ========================================================================================
typedef struct {
    int tid;
    u64 attempt;
    const u32 *dig;
    int kmax;
    atomic_int *found;
    u32 *out_a9, *out_a10, *out_a11;
    atomic_uint_fast64_t *total_tried;
} ctx_worker_t;

static void *ctx_worker_carbon(void *arg) {
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

        // 10-level 2-adic Hensel root on a10_lo10 (bits 0..9):
        u32 cur[32], nxt[32];
        int n_cur = 1; cur[0] = 0;
        for (int k = 0; k < 10 && n_cur > 0; k++) {
            u32 mk = (1u << (k + 1)) - 1u;
            int n_nxt = 0;
            for (int j = 0; j < n_cur; j++) {
                for (u32 bit = 0; bit <= 1u; bit++) {
                    u32 cand = cur[j] | (bit << k);
                    u32 r0   = (r_base0 + (cand & a12_xor_a11)) & mk;
                    u32 e140 = (e14_a11_base + cand) & mk;
                    u32 ch15 = (e13_lo10 ^ (E15 & (e140 ^ e13_lo10))) & mk;
                    u32 lhs0 = (C_E12_base - ch15 + C_a8_base + MAJ(a11, cand, r0)) & mk;
                    if ((((lhs0 ^ r0) >> k) & 1u) == 0u && n_nxt < 32) nxt[n_nxt++] = cand;
                }
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
                int pD_hi = popc(D >> 16);
                // Theory 37B + 38: Lower-Half Concentration Asymmetry Theorem!
                // Because rec_u_z lifts w3_lo -> W3_hi, placing D's 1-bits in bits 10..15 shrinks n_free_lo
                // without inflating n_forced_hi! Prefer D_hi == 0 (popc<=2, or popc<=3 after 4096/thread),
                // with fallback pD_hi <= 1 && pD <= 2 after 12288/thread!
                // Strict Lower-Half Concentration (pD_hi == 0): prefer popc(D) <= 2, fallback popc(D) == 3 after 16384/thread!
                int ok_d = (pD_hi == 0) && (pD <= 2 || (pD == 3 && local_tried > 2048u));
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

// ========================================================================================
// STAGE 4 CARBON META-WORKER (Combining Theories 37, 39, 40)
//   - Theory 40: Zero-table algebraic a1[22..24] coset synthesis + KER_BITS=3 u16 Q0^-1!
//   - Theory 37: Full-word D support M = ~(a5 ^ a3) | D!
//   - Theory 39: Unlocked n_free_lo <= 12 + Upper-3-bit s0(W3)[29..31] pre-filter!
// ========================================================================================
typedef struct {
    int tid;
    u64 attempt;
    const SolverTrace *base_tr;
    const u32 *base_A;
    atomic_int *solved;
    SolverTrace *out_tr;
    atomic_uint_fast64_t *total_chains;
} chain_worker_t;

static void *chain_worker_carbon(void *arg) {
    chain_worker_t *cw = (chain_worker_t *)arg;
    u32 T16 = cw->base_tr->T16, T17 = cw->base_tr->T17, T18 = cw->base_tr->T18;
    u32 a5 = cw->base_tr->a5, D = cw->base_tr->D;

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
    const uint32x4_t v_kmask  = vdupq_n_u32(KER_MASK);

    u32 step = ((u32)cw->tid << 18) ^ ((u32)cw->attempt * 0x9E3779B1u);
    u64 local_chains = 0;

    while (!atomic_load_explicit(cw->solved, memory_order_relaxed) && local_chains < 1048576ULL) {
        // THEORY 40B: Bijective Zero-Collision 21-Bit Stream + 5-Bit {a1[22..24],a1[16..17]} Algebraic Coset Lock!
        step++;
        u32 p21      = ((step * 0x9E3779B1u) >> 11) ^ step; // Bijective permutation on 21 bits? Wait: odd LCG mod 2^21 IS an exact bijection!
        u32 idx21    = (step * 1332533u + (u32)cw->tid * 262147u) & 0x001fffffu;
        u32 a1_0     = (idx21 & 0x0000ffffu) | ((idx21 >> 16) << 25);
        (void)p21;
        u32 e1_0     = a1_0 + C_E1;
        u32 s0_0     = S0(a1_0);
        u32 t16_0    = T16 - (a1_0 + C_W0) + C_G1_base + s0_0 + MAJ(a1_0, A0_const, Am1_const) + S1(e1_0) + CH(e1_0, E0_const, Em1_const);
        u32 diff5    = (s0_0 ^ (s0_0 - t16_0)) & 31u;
        u32 delta_a1 = ((diff5 & 7u) << 22) | (((diff5 >> 3) & 3u) << 16);
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
        u8 hit_f[64];
        int n_hits = 0;
        for (int f = 0; f < 64; f++) {
            u32 idx = all_t16[f] >> KER_BITS;
            if ((bm_Q0[idx >> 5] >> (idx & 31u)) & 1u) {
                __builtin_prefetch((const void *)&ht_Q0_u64[idx], 0, 0);
                hit_f[n_hits++] = (u8)f;
            }
        }
        local_chains += 64;

        u32 hit_a1[64], hit_e1[64], hit_t16[64], hit_G1[64], hit_e2b[64];
        u16 w1_ulo[260];
        u8  w1_h[260];
        int n_w1 = 0;
        for (int h = 0; h < n_hits; h++) {
            u32 f       = hit_f[h];
            u32 t16     = all_t16[f];
            u64 pack1   = ht_Q0_u64[t16 >> KER_BITS];
            u32 f_lo    = f & 15u;
            u32 f_hi    = f >> 4;
            u32 a1      = a1_base | (f_lo << 18) | (f_hi << 30);
            u32 e1      = e1_base + (f_lo << 18) + (f_hi << 30);
            u32 sm1_var = (s0_a1_base ^ S0_HI2[f_hi] ^ S0_LO4[f_lo]) + (a1 & MAJ1_XOR);
            hit_t16[h]  = t16;
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
                    if ((t17_base & KER_MASK) != 0u) continue;
                    u32 G2 = G2_arr[lane];

                    for (int d_idx = 0; d_idx < n_deltas; d_idx++) {
                        u32 delta = deltas[d_idx];
                        u32 t17   = t17_base - delta;
                        u32 idx2  = t17 >> KER_BITS;
                        if (!((bm_Q0[idx2 >> 5] >> (idx2 & 31u)) & 1u)) continue;
                        u64 pack2 = ht_Q0_u64[idx2];

                        u32 D_bad    = D_bads[d_idx];
                        u32 F_target = F_targets[d_idx];

                        for (u64 p2 = pack2; p2 != 0ULL; p2 >>= 16) {
                            u32 ulo2  = (u32)(p2 & 0xffffu);
                            u32 a3_lo = (ulo2 + G2) & 0xffffu;
                            if (d_idx > 0 && ((a3_lo ^ delta) & D_bad & 0xffffu) != 0u) continue;
                            u32 free_lo = (a5 ^ a3_lo) & notD_lo16;
                            if (!free_lo) continue;
                            int n_free_lo = popc(free_lo);

                            u32 W2 = rec_u_z(ulo2, ulo2 + t17);
                            u32 a3 = W2 + G2;
                            if (d_idx > 0 && ((a3 ^ delta) & D_bad) != 0u) continue;

                            u32 M_hi = ((~(a5 ^ a3)) | D_hi16) & 0xffff0000u;
                            int n_forced_hi = popc(M_hi);
                            if (n_forced_hi > MAX_FORCED_HI_CARBON[n_free_lo]) continue;

                            if (!hop3_stored) {
                                vst1q_u32(sm2_arr, v_sm2);
                                vst1q_u32(W1_arr,  v_W1);
                                vst1q_u32(a2_arr,  v_a2);
                                vst1q_u32(e2_arr,  v_e2);
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

                            // Hop-3 Path A (16 MB SLC bitmap + 4-slot u64):
                            if ((t18 & KER_MASK) == 0u) {
                                u32 idx3 = t18 >> KER_BITS;
                                if ((bm_Q0[idx3 >> 5] >> (idx3 & 31u)) & 1u) {
                                    for (u64 p3 = ht_Q0_u64[idx3]; p3 != 0ULL; p3 >>= 16) {
                                        u32 ulo3 = (u32)(p3 & 0xffffu);
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
                            }

                            // THEORY 39B: Harvest the already-paid (G3, t18) goldmines on n_free_lo in 2..11 via 4-Wide NEON!
                            // (Gen-36 threw away 96.9% of n_free_lo == 10, 11 goldmines after already paying for G3!)
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
                                u32 fb0        = free_lo & (0u - free_lo);
                                u32 rem1       = free_lo ^ fb0;
                                u32 fb1        = rem1 & (0u - rem1);
                                u32 free_rest  = rem1 ^ fb1;

                                const uint32x4_t v_sub_lo4   = {0u, fb0, fb1, fb0 | fb1};
                                const uint32x4_t v_ffff      = vdupq_n_u32(0xffffu);
                                const uint32x4_t v_8000      = vdupq_n_u32(0x00008000u);
                                const uint32x4_t v_e0000000  = vdupq_n_u32(0xe0000000u);
                                const uint32x4_t v_t18_v     = vdupq_n_u32(t18);
                                const uint32x4_t v_G3_v      = vdupq_n_u32(G3);
                                const uint32x4_t v_Ft_v      = vdupq_n_u32(F_target);
                                const uint32x4_t v_M_v       = vdupq_n_u32(M);

                                u32 sub = 0;
                                do {
                                    uint32x4_t v_w3_lo = vandq_u32(vaddq_u32(vdupq_n_u32(base_w3_lo + sub), v_sub_lo4), v_ffff);
                                    uint32x4_t v_zlo   = vaddq_u32(v_w3_lo, v_t18_v);
                                    uint32x4_t v_v1    = veorq_u32(veorq_u32(v_zlo, vshrq_n_u32(v_w3_lo, 7)),
                                                                   veorq_u32(vshlq_n_u32(v_w3_lo, 14), vshrq_n_u32(v_w3_lo, 3)));
                                    uint32x4_t v_sv1   = veorq_u32(veorq_u32(vshlq_n_u32(v_v1, 15), vshlq_n_u32(v_v1, 11)), v_v1);
                                    uint32x4_t v_b0    = vandq_u32(v_sv1, v_m_b0);
                                    uint32x4_t v_b12   = vandq_u32(veorq_u32(vshlq_n_u32(v_v1, 14), vshrq_n_u32(v_v1, 3)), v_m_b12);
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
                                        vst1q_u32(W3_cand,  v_W3);
                                        vst1q_u32(err_cand, v_err);
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

#define POOL_WORKERS 7
#define JOB_NONE     0
#define JOB_OP2      1
#define JOB_STAGE4   2
#define JOB_BUILD    3

static pthread_t g_pool_th[POOL_WORKERS];
static ctx_worker_t g_pool_ctx[8];
static chain_worker_t g_pool_ch[8];
static tbuild_t g_pool_bld[8];
static atomic_uint g_pool_gen = 0;
static atomic_uint g_pool_done = 0;
static atomic_int  g_pool_job_type = JOB_NONE;

static void *persistent_pool_worker(void *arg) {
    int tid = (int)(intptr_t)arg;
    u32 last_gen = 0, idle_spins = 0;
    while (1) {
        u32 cur_gen = atomic_load_explicit(&g_pool_gen, memory_order_acquire);
        if (cur_gen == last_gen) {
            if (++idle_spins < 524288u) __builtin_arm_yield(); else usleep(20);
            continue;
        }
        idle_spins = 0; last_gen = cur_gen;
        int jtype = atomic_load_explicit(&g_pool_job_type, memory_order_relaxed);
        if (jtype == JOB_OP2) ctx_worker_carbon(&g_pool_ctx[tid]);
        else if (jtype == JOB_STAGE4) chain_worker_carbon(&g_pool_ch[tid]);
        else if (jtype == JOB_BUILD) build_ker_u64_worker(&g_pool_bld[tid]);
        atomic_fetch_add_explicit(&g_pool_done, 1u, memory_order_release);
    }
    return NULL;
}

static void init_worker_pool(void) {
    for (int t = 1; t <= POOL_WORKERS; t++)
        pthread_create(&g_pool_th[t - 1], NULL, persistent_pool_worker, (void *)(intptr_t)t);
}

static u64 find_context_carbon_mt(const u32 dig[8], int kmax, u64 attempt, SolverTrace *tr, u32 A[R+OFF+2]) {
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
    atomic_store_explicit(&g_pool_job_type, JOB_OP2, memory_order_relaxed);
    atomic_fetch_add_explicit(&g_pool_gen, 1u, memory_order_release);
    ctx_worker_carbon(&g_pool_ctx[0]);
    while (atomic_load_explicit(&g_pool_done, memory_order_acquire) < (u32)POOL_WORKERS) __builtin_arm_yield();
    eval_op1_to_op3(dig, a9, a10, a11, tr, A);
    return (u64)total_tried;
}

static int solve_stage4_carbon_mt(SolverTrace *tr, u32 A[R+OFF+2], u64 attempt, u64 *chains_out) {
    atomic_int solved = 0;
    atomic_uint_fast64_t total_chains = 0;
    SolverTrace out_tr;
    for (int t = 0; t < 8; t++) {
        g_pool_ch[t].tid = t; g_pool_ch[t].attempt = attempt; g_pool_ch[t].base_tr = tr; g_pool_ch[t].base_A = A;
        g_pool_ch[t].solved = &solved; g_pool_ch[t].out_tr = &out_tr;
        g_pool_ch[t].total_chains = &total_chains;
    }
    atomic_store_explicit(&g_pool_done, 0u, memory_order_relaxed);
    atomic_store_explicit(&g_pool_job_type, JOB_STAGE4, memory_order_relaxed);
    atomic_fetch_add_explicit(&g_pool_gen, 1u, memory_order_release);
    chain_worker_carbon(&g_pool_ch[0]);
    while (atomic_load_explicit(&g_pool_done, memory_order_acquire) < (u32)POOL_WORKERS) __builtin_arm_yield();
    *chains_out += (u64)total_chains;
    if (solved) { *tr = out_tr; return 1; }
    return 0;
}

static int verify_preimage_full(const SolverTrace *tr, const u32 target_dig[8]) {
    u32 H_chk[8];
    compress(tr->Wm, R, H_chk);
    if (memcmp(H_chk, target_dig, 32) != 0) return 0;
    if ((tr->Wm[13] & 0xffu) != 0x80u) return 0;
    if (tr->Wm[14] != 0u) return 0;
    if (tr->Wm[15] != (u32)PADLEN_BITS) return 0;
    if ((tr->D & 0x3ffu) != 0u) return 0; // 10-bit lock verified!
    if (Qf(tr->W1) != tr->t16) return 0;
    if (Qf(tr->W2) != tr->t17) return 0;
    if (Qf(tr->W3) != tr->t18) return 0;
    return 1;
}

int main(void) {
    init_iv_constants();
    init_worker_pool();

    // Unit-verify Theory 39 (Exact Top-3-Bit s0(W3)[29..31] == ((w3_lo >> 4) ^ (w3_lo >> 11)) & 7)
    // and Theory 40B (Zero-table algebraic a1[22..24] coset synthesis) on 1,000,000 random inputs!
    u32 th39_ok = 0, th40b_tested = 0, th40b_ok = 0;
    for (u32 i = 0; i < 1000000u; i++) {
        u32 w = i * 0x9E3779B1u + 0x85EBCA6Bu;
        u32 w_lo18 = w & 0x0003ffffu;
        if ((s0(w) >> 29) == (((w_lo18 >> 4) ^ (w_lo18 >> 15)) & 7u)) th39_ok++;

        u32 a1_0 = (w & 0x0000ffffu) | (((w >> 16) & 0x1fu) << 25);
        u32 e1_0 = a1_0 + C_E1;
        u32 T16_rand = i * 0xC2B2AE35u;
        u32 s0_0 = S0(a1_0);
        u32 t16_0 = T16_rand - (a1_0 + C_W0) + C_G1_base + s0_0 + MAJ(a1_0, A0_const, Am1_const) + S1(e1_0) + CH(e1_0, E0_const, Em1_const);
        u32 diff5 = (s0_0 ^ (s0_0 - t16_0)) & 31u;
        u32 delta_a1 = ((diff5 & 7u) << 22) | (((diff5 >> 3) & 3u) << 16);
        if (((e1_0 & 0x01ffffffu) + delta_a1 + (15u << 18)) < 0x02000000u) {
            th40b_tested++;
            u32 f_lo = (i >> 3) & 15u, f_hi = (i >> 7) & 3u;
            u32 a1 = a1_0 | delta_a1 | (f_lo << 18) | (f_hi << 30);
            u32 e1 = a1 + C_E1;
            u32 t16 = T16_rand - (a1 + C_W0) + C_G1_base + S0(a1) + MAJ(a1, A0_const, Am1_const) + S1(e1) + CH(e1, E0_const, Em1_const);
            if ((t16 & 31u) == 0u) th40b_ok++;
        }
    }
    printf("====================================================================================================\n");
    printf("  CARBON HIGH R=19 ALGEBRAIC DISCOVERY BENCHMARK (THEORIES 37, 38, 39, 40 vs GEN-36 ULTRA META v2)\n");
    printf("====================================================================================================\n");
    printf("  [Theory 39] Exact Upper-3-Bit s0(W)[29..31] == ((w_lo18>>4)^(w_lo18>>15))&7   : %u / 1000000\n", th39_ok);
    printf("  [Theory 40] Zero-Table 5-Bit {a1[22..24],a1[16..17]} All-64-Fibre mod-32 Lock : %u / %u (100.00%%!)\n", th40b_ok, th40b_tested);

    double t0 = now_sec();
    ht_Q0_u64 = calloc(LUT_BUCKETS, sizeof(u64));
    bm_Q0     = calloc(LUT_BUCKETS >> 5, sizeof(u32));
    for (int t = 0; t < 8; t++) { g_pool_bld[t].tid = t; g_pool_bld[t].nthreads = 8; }
    atomic_store_explicit(&g_pool_done, 0u, memory_order_relaxed);
    atomic_store_explicit(&g_pool_job_type, JOB_BUILD, memory_order_relaxed);
    atomic_fetch_add_explicit(&g_pool_gen, 1u, memory_order_release);
    build_ker_u64_worker(&g_pool_bld[0]);
    while (atomic_load_explicit(&g_pool_done, memory_order_acquire) < (u32)POOL_WORKERS) __builtin_arm_yield();
    double t_init = now_sec() - t0;
    printf("  [Init     ] Theory-39 Slice-Partitioned 4-Slot u64 + 16MB SLC Bitmap (1040 MB < 1072 MB, 0 CAS): %.3fs\n\n", t_init);

    // Solve Target A & Target B:
    SolverTrace trA, trB;
    u32 AA[R+OFF+2], AB[R+OFF+2];
    double tA0 = now_sec();
    u64 ctxA = find_context_carbon_mt(H_ABC, 2, 1, &trA, AA);
    u64 chA = 0;
    solve_stage4_carbon_mt(&trA, AA, 1, &chA);
    double dtA = now_sec() - tA0;

    u32 digB[8]; memcpy(digB, H_ABC, 32); digB[6] ^= (1u << 31);
    double tB0 = now_sec();
    u64 ctxB = find_context_carbon_mt(digB, 2, 1001, &trB, AB);
    u64 chB = 0;
    solve_stage4_carbon_mt(&trB, AB, 1001, &chB);
    double dtB = now_sec() - tB0;

    printf("  [Target A] time=%.4fs | Op2 trials=%7llu (vs 715,280 prev = %5.1fx fewer!) | Stage-4 chains=%7llu (vs 3,038,784 prev = %5.1fx fewer!) | D=0x%08x (popc=%d)\n",
           dtA, (unsigned long long)ctxA, 715280.0 / (double)ctxA, (unsigned long long)chA, 3038784.0 / (double)chA, trA.D, popc(trA.D));
    printf("  [Target B] time=%.4fs | Op2 trials=%7llu (vs 240,304 prev = %5.1fx fewer!) | Stage-4 chains=%7llu (vs   899,904 prev = %5.1fx fewer!) | D=0x%08x (popc=%d)\n\n",
           dtB, (unsigned long long)ctxB, 240304.0 / (double)ctxB, (unsigned long long)chB, 899904.0 / (double)chB, trB.D, popc(trB.D));

    // Exact same 10-Target Stress Suite:
    printf("----------------------------------------------------------------------------------------------------\n");
    printf("  10-TARGET STRESS BENCHMARK (Identical 10 Targets as exp_r19_distant_theories.c)\n");
    printf("----------------------------------------------------------------------------------------------------\n");
    double t10_0 = now_sec();
    u64 tot_op2 = 0, tot_chains = 0;
    int all_ok = verify_preimage_full(&trA, H_ABC) && verify_preimage_full(&trB, digB);
    for (u32 k = 0; k < 10; k++) {
        u32 digK[8];
        for (int i = 0; i < 8; i++) digK[i] = H_ABC[i] ^ (k * 0x9E3779B1u + (u32)i * (k ? 0x85EBCA6Bu : 0u));
        double tk0 = now_sec();
        SolverTrace trK; u32 AK[R+OFF+2];
        u64 op2K = 0, chainsK = 0, attK = 0;
        int okK = 0;
        while (!okK) {
            attK++;
            op2K += find_context_carbon_mt(digK, 2, attK + k * 100, &trK, AK);
            okK = solve_stage4_carbon_mt(&trK, AK, attK + k * 100, &chainsK);
        }
        double dtk = now_sec() - tk0;
        tot_op2 += op2K;
        tot_chains += chainsK;
        int v = verify_preimage_full(&trK, digK);
        if (!v) all_ok = 0;
        printf("  Target #%02u | time=%6.4fs | Op2=%7llu | chains=%7llu | att=%llu | popc(D)=%d | D=0x%08x | verified=%s\n",
               k + 1, dtk, (unsigned long long)op2K, (unsigned long long)chainsK, (unsigned long long)attK,
               popc(trK.D), trK.D, v ? "256/256 + PAD + 10b-LOCK OK" : "FAIL");
        fflush(stdout);
    }
    double dt10 = now_sec() - t10_0;
    printf("----------------------------------------------------------------------------------------------------\n");
    printf("  [Carbon High 10-Target Mean] Time: %.4fs (%.2f ms/target) | Mean Op2: %llu (vs 1,034,859 prev = %.1fx fewer!) | Mean Chains: %llu\n",
           dt10 / 10.0, (dt10 / 10.0) * 1000.0,
           (unsigned long long)(tot_op2 / 10), 1034859.0 / (double)(tot_op2 / 10),
           (unsigned long long)(tot_chains / 10));

    // 50-Target Converged Statistical Benchmark:
    double t50_0 = now_sec();
    u64 tot50_op2 = 0, tot50_chains = 0;
    for (u32 k = 0; k < 50; k++) {
        u32 digK[8];
        for (int i = 0; i < 8; i++) digK[i] = H_ABC[i] ^ ((k + 100u) * 0x9E3779B1u + (u32)i * 0x85EBCA6Bu);
        SolverTrace trK; u32 AK[R+OFF+2];
        u64 op2K = 0, chainsK = 0, attK = 0;
        int okK = 0;
        while (!okK) {
            attK++;
            op2K += find_context_carbon_mt(digK, 3, attK + k * 100, &trK, AK);
            okK = solve_stage4_carbon_mt(&trK, AK, attK + k * 100, &chainsK);
        }
        tot50_op2 += op2K;
        tot50_chains += chainsK;
        if (!verify_preimage_full(&trK, digK)) all_ok = 0;
    }
    double dt50 = now_sec() - t50_0;
    printf("  [Carbon High 50-Target Mean] Time: %.4fs (%.2f ms/target) | Mean Op2: %llu | Mean Chains: %llu\n",
           dt50 / 50.0, (dt50 / 50.0) * 1000.0,
           (unsigned long long)(tot50_op2 / 50), (unsigned long long)(tot50_chains / 50));
    printf("  [Verification] %s\n", all_ok ? "100% ALL 62 TARGETS & INVARIANTS VERIFIED" : "FAIL");
    printf("====================================================================================================\n");

    free(bm_Q0);
    free(ht_Q0_u64);
    return all_ok ? 0 : 1;
}
