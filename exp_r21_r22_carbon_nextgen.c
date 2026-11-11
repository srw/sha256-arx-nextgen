// ====================================================================================================
// exp_r21_r22_carbon_nextgen.c
//
// Pass 4: Next-Generation Carbon Algebraic Preimage Engine for Reduced-Round FIPS 180-4 Padded
// SHA-256 at R = 21 and R = 22 (informed by Pass 3 First-Principles Obstruction Diagnosis).
//
// Combines:
//   1. Calibrated 8-Core Scalar C & 4-Wide ARM64 NEON SHA-21 / SHA-22 Reference Throughput.
//   2. Exact Fixed-Target Sigma0^-1 Top Insulation + Carbon Theories 37-39 Carry-Sudoku:
//      - R=21: (a13, a12) free -> a13 = S0^-1(C_{14,15} - e14*) with (e14* & 0x01ffff00) == 0,
//              preserving sparse e14* across the entire M_sub = ~(a14 ^ a13) & ~(a13 ^ a11) submanifold.
//      - R=22: (a14, a13, a12) free -> a13 = S0^-1(a14 + S0(a14) + 0xffffffff - a15), enforcing
//              e14 == 0x00000000 IDENTICALLY across 100% of all 2^32 seeds a14 and M_sub!
//   3. Carbon MSHR-Optimal (512 MB KB=4 Direct u16 & 528 MB KB=5 2-Slot u32 + 16 MB L2 Bitmap,
//      0-CAS slice builders, 0 MB cosets, 0 swap) + Neyman-Pearson Hop-2/3 Threshold Ablation +
//      1-Round Hop-4 (W4) & Gated 1-Round Hop-5 (W5, +a2-a2 cancelled) Verification.
//   4. Strict Scientific Separation & Live Cryptographic Witness Recovery:
//      - Full 1-Block 256/256-Bit Fixed-Target Complexity (calibrated log2(SHA21-eq) & log2(SHA22-eq)),
//      - Sub-millisecond Live Causal-Horizon Witness Recovery on 0^256, "abc", "hello"
//        (128/256 exact fixed digest bits at R=21; 64/256 exact fixed digest bits at R=22),
//      - Live 8-Core 2-for-1 ctz(rho19) Prefix-Zeroed Witness Harvester on "abc" (R=21 & R=22).
//
// Compile & Run:
//   clang -O3 -march=native -pthread exp_r21_r22_carbon_nextgen.c -lm -o exp_r21_r22_carbon_nextgen && ./exp_r21_r22_carbon_nextgen
// ====================================================================================================

#include <arm_neon.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef uint32_t u32;
typedef uint64_t u64;
typedef uint16_t u16;
typedef uint8_t  u8;

#define ROR(x, n)   (((x) >> ((n) & 31u)) | ((x) << ((32u - ((n) & 31u)) & 31u)))
#define ROR64(x, n) (((n) & 63u) ? (((x) >> ((n) & 63u)) | ((x) << (64u - ((n) & 63u)))) : (x))
static inline u32 S0(u32 x)  { return ROR(x, 2) ^ ROR(x, 13) ^ ROR(x, 22); }
static inline u32 S1(u32 x)  { return ROR(x, 6) ^ ROR(x, 11) ^ ROR(x, 25); }
static inline u32 s0(u32 x)  { return ROR(x, 7) ^ ROR(x, 18) ^ (x >> 3); }
static inline u32 s1(u32 x)  { return ROR(x, 17) ^ ROR(x, 19) ^ (x >> 10); }
static inline u32 CH(u32 e, u32 f, u32 g)  { return g ^ (e & (f ^ g)); }
static inline u32 MAJ(u32 a, u32 b, u32 c) { return (a & (b ^ c)) ^ (b & c); }
static inline u32 Qf(u32 u)  { return s0(u) - u; }

static const u32 K[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u
};
static const u32 IV[8] = {
    0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
    0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u
};
static const u32 H_ZERO[8]  = {0,0,0,0,0,0,0,0};
static const u32 H_ABC[8]   = {0xba7816bfu,0x8f01cfeau,0x414140deu,0x5dae2223u,0xb00361a3u,0x96177a9cu,0xb410ff61u,0xf20015adu};
static const u32 H_HELLO[8] = {0x2cf24dbau,0x5fb0a30eu,0x26e83b2au,0xc5b9e29eu,0x1b161e5cu,0x1fa7425eu,0x73043362u,0x938b9824u};

#define PADLEN_BITS 440u
#define OFF         8

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static inline u32 perm32(u32 x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}

static inline u32 E_arr(const u32 *A, int s) {
    return A[s + OFF] - S0(A[s - 1 + OFF]) - MAJ(A[s - 1 + OFF], A[s - 2 + OFF], A[s - 3 + OFF]) + A[s - 4 + OFF];
}
static inline u32 Wt_arr(const u32 *A, int t) {
    u32 et = E_arr(A, t);
    return E_arr(A, t + 1) - A[t - 3 + OFF] - E_arr(A, t - 3) - S1(et) - CH(et, E_arr(A, t - 1), E_arr(A, t - 2)) - K[t];
}

static void compress_steps(const u32 iv_in[8], const u32 Wm[16], int r, u32 out[8]) {
    u32 W[24];
    memcpy(W, Wm, 16 * sizeof(u32));
    for (int t = 16; t < r; t++) W[t] = s1(W[t - 2]) + W[t - 7] + s0(W[t - 15]) + W[t - 16];
    u32 a = iv_in[0], b = iv_in[1], c = iv_in[2], d = iv_in[3];
    u32 e = iv_in[4], f = iv_in[5], g = iv_in[6], h = iv_in[7];
    for (int t = 0; t < r; t++) {
        u32 T1 = h + S1(e) + CH(e, f, g) + K[t] + W[t];
        u32 T2 = S0(a) + MAJ(a, b, c);
        h = g; g = f; f = e; e = d + T1; d = c; c = b; b = a; a = T1 + T2;
    }
    u32 st[8] = {a, b, c, d, e, f, g, h};
    for (int i = 0; i < 8; i++) out[i] = st[i] + iv_in[i];
}

static void init_state_r(u32 A[36], const u32 H_targ[8], int r) {
    memset(A, 0, 36 * sizeof(u32));
    for (int i = 0; i < 4; i++) A[(0 - i) + OFF] = IV[i];
    for (int k = 0; k < 4; k++) {
        int t = -k;
        A[(t - 4) + OFF] = IV[4 + k] - A[t + OFF] + S0(A[t - 1 + OFF]) +
                           MAJ(A[t - 1 + OFF], A[t - 2 + OFF], A[t - 3 + OFF]);
    }
    for (int i = 0; i < 4; i++) A[(r - i) + OFF] = H_targ[i] - IV[i];
    for (int k = 0; k < 4; k++) {
        int t = r - k;
        u32 et = H_targ[4 + k] - IV[4 + k];
        A[(t - 4) + OFF] = et - A[t + OFF] + S0(A[t - 1 + OFF]) +
                           MAJ(A[t - 1 + OFF], A[t - 2 + OFF], A[t - 3 + OFF]);
    }
}

// ====================================================================================================
// EXACT O(1) GF(2) INVERSE LUT FOR S0^-1 (4 KB) + THEORY 38 L1-LUTS (64 KB)
// ====================================================================================================
static u32 INV_S0_LUT[4][256];
static u64 VALID_SUM64[64][64];
static u64 MASK8_LUT[64][64];

static void init_gf2_inv_s0_and_luts(void) {
    u64 aug[32];
    for (int r = 0; r < 32; r++) {
        u32 row_m = 0;
        for (int c = 0; c < 32; c++) if ((S0(1u << c) >> r) & 1u) row_m |= (1u << c);
        aug[r] = (u64)row_m | ((1ULL << r) << 32);
    }
    for (int col = 0; col < 32; col++) {
        int piv = col;
        while (piv < 32 && !((aug[piv] >> col) & 1ULL)) piv++;
        u64 tmp = aug[col]; aug[col] = aug[piv]; aug[piv] = tmp;
        for (int r = 0; r < 32; r++) if (r != col && ((aug[r] >> col) & 1ULL)) aug[r] ^= aug[col];
    }
    u32 inv_cols[32];
    for (int c = 0; c < 32; c++) inv_cols[c] = 0;
    for (int r = 0; r < 32; r++) {
        u32 rhs = (u32)(aug[r] >> 32);
        for (int c = 0; c < 32; c++) if ((rhs >> c) & 1u) inv_cols[c] |= (1u << r);
    }
    for (int byte_i = 0; byte_i < 4; byte_i++) {
        for (u32 v = 0; v < 256u; v++) {
            u32 out = 0;
            for (int b = 0; b < 8; b++) if ((v >> b) & 1u) out ^= inv_cols[byte_i * 8 + b];
            INV_S0_LUT[byte_i][v] = out;
        }
    }
    for (u32 t = 0; t < 64u; t++) {
        for (u32 m = 0; m < 64u; m++) {
            u64 mask = 0ULL;
            for (u32 u = 0; u < 64u; u++) if ((u & m) == (t & m)) mask |= (1ULL << u);
            VALID_SUM64[t][m] = mask;
        }
    }
    for (u32 r5 = 0; r5 < 64u; r5++) {
        for (u32 q = 0; q < 64u; q++) {
            u64 m8 = 0ULL;
            for (u32 k = 0; k < 8u; k++) m8 |= (1ULL << (r5 ^ ((q + k) & 63u)));
            MASK8_LUT[r5][q] = m8;
        }
    }
}

static inline u32 inv_S0(u32 y) {
    return INV_S0_LUT[0][y & 0xffu] ^ INV_S0_LUT[1][(y >> 8) & 0xffu] ^
           INV_S0_LUT[2][(y >> 16) & 0xffu] ^ INV_S0_LUT[3][y >> 24];
}

// ====================================================================================================
// PART 1: 8-CORE SCALAR C & 4-WIDE NEON CALIBRATION FOR R = 21 AND R = 22
// ====================================================================================================
typedef struct { int tid; int rounds; u32 iters; volatile u32 sink; } ShaCalArg;

static void *cal_scalar_worker(void *arg) {
    ShaCalArg *ca = (ShaCalArg *)arg;
    u32 W[16] = {0}, out[8], acc = 0;
    W[0] = 0x61626380u ^ ((u32)ca->tid << 24); W[13] = 0x80u; W[15] = PADLEN_BITS;
    for (u32 i = 0; i < ca->iters; i++) { W[1] = i; compress_steps(IV, W, ca->rounds, out); acc ^= out[0]; }
    ca->sink = acc;
    return NULL;
}

static void *cal_neon_worker(void *arg) {
    ShaCalArg *ca = (ShaCalArg *)arg;
    #define VROTR(v, n) vsriq_n_u32(vshlq_n_u32((v), 32 - (n)), (v), (n))
    #define VS0_M(v) veorq_u32(veorq_u32(VROTR((v), 2), VROTR((v), 13)), VROTR((v), 22))
    #define VS1_M(v) veorq_u32(veorq_u32(VROTR((v), 6), VROTR((v), 11)), VROTR((v), 25))
    #define vs0_w(v) veorq_u32(veorq_u32(VROTR((v), 7), VROTR((v), 18)), vshrq_n_u32((v), 3))
    #define vs1_w(v) veorq_u32(veorq_u32(VROTR((v), 17), VROTR((v), 19)), vshrq_n_u32((v), 10))
    uint32x4_t W[24];
    for (int i = 0; i < 16; i++) W[i] = vdupq_n_u32(0);
    W[0] = (uint32x4_t){0u, 1u, 2u, 3u}; W[13] = vdupq_n_u32(0x80u); W[15] = vdupq_n_u32(PADLEN_BITS);
    uint32x4_t acc = vdupq_n_u32((u32)ca->tid);
    int R = ca->rounds;
    for (u32 i = 0; i < ca->iters; i++) {
        W[1] = vdupq_n_u32(i);
        for (int t = 16; t < R; t++)
            W[t] = vaddq_u32(vaddq_u32(vs1_w(W[t - 2]), W[t - 7]), vaddq_u32(vs0_w(W[t - 15]), W[t - 16]));
        uint32x4_t a = vdupq_n_u32(IV[0]), b = vdupq_n_u32(IV[1]), c = vdupq_n_u32(IV[2]), d = vdupq_n_u32(IV[3]);
        uint32x4_t e = vdupq_n_u32(IV[4]), f = vdupq_n_u32(IV[5]), g = vdupq_n_u32(IV[6]), h = vdupq_n_u32(IV[7]);
        for (int t = 0; t < R; t++) {
            uint32x4_t ch = vbslq_u32(e, f, g), maj = vbslq_u32(veorq_u32(b, c), a, b);
            uint32x4_t T1 = vaddq_u32(vaddq_u32(h, VS1_M(e)), vaddq_u32(ch, vaddq_u32(vdupq_n_u32(K[t]), W[t])));
            uint32x4_t T2 = vaddq_u32(VS0_M(a), maj);
            h = g; g = f; f = e; e = vaddq_u32(d, T1); d = c; c = b; b = a; a = vaddq_u32(T1, T2);
        }
        acc = veorq_u32(acc, a);
    }
    ca->sink = vgetq_lane_u32(acc, 0);
    #undef VROTR
    #undef VS0_M
    #undef VS1_M
    #undef vs0_w
    #undef vs1_w
    return NULL;
}

static void calibrate_sha(int R, double *sc_s, double *ne_s) {
    pthread_t th[8]; ShaCalArg ca[8];
    const u32 WARM_IT = 1u << 18, SC_IT = 1u << 21, NE_IT = 1u << 21;
    for (int t = 0; t < 8; t++) { ca[t] = (ShaCalArg){t, R, WARM_IT, 0}; pthread_create(&th[t], NULL, cal_scalar_worker, &ca[t]); }
    for (int t = 0; t < 8; t++) pthread_join(th[t], NULL);
    double t0 = now_sec();
    for (int t = 0; t < 8; t++) { ca[t] = (ShaCalArg){t, R, SC_IT, 0}; pthread_create(&th[t], NULL, cal_scalar_worker, &ca[t]); }
    for (int t = 0; t < 8; t++) pthread_join(th[t], NULL);
    *sc_s = (8.0 * (double)SC_IT) / (now_sec() - t0);
    t0 = now_sec();
    for (int t = 0; t < 8; t++) { ca[t] = (ShaCalArg){t, R, NE_IT, 0}; pthread_create(&th[t], NULL, cal_neon_worker, &ca[t]); }
    for (int t = 0; t < 8; t++) pthread_join(th[t], NULL);
    *ne_s = (32.0 * (double)NE_IT) / (now_sec() - t0);
}

// ====================================================================================================
// PART 2: RIGHT-CONTEXT CARRY-SUDOKU ENGINE FOR R = 21 & R = 22 (SIGMA0^-1 INSULATION + THEORIES 37-39)
// ====================================================================================================
typedef struct {
    u32 a14, a13, a12, a9, a5;
    u32 T16, T17, T18, T19, T20;
    u32 c_W5_base; // a5 - S0(a5) - K[5] (for 1-round +a2-a2 cancelled Hop-5 W5 check!)
} compact_ctx21_22_t;

static inline void emit_ctx21_22(const u32 A_tmpl[36], int R, u32 a14, u32 a13, u32 a12, u32 a11,
                                 u32 a9, u32 a8, u32 a7, u32 a5, compact_ctx21_22_t *out) {
    u32 A[36];
    memcpy(A, A_tmpl, sizeof(A));
    A[14 + OFF] = a14; A[13 + OFF] = a13; A[12 + OFF] = a12;
    A[11 + OFF] = a11; A[10 + OFF] = a11; A[9 + OFF]  = a9;
    A[8 + OFF]  = a8;  A[7 + OFF]  = a7;  A[6 + OFF]  = a5; A[5 + OFF] = a5;
    A[4 + OFF]  = 0;   A[3 + OFF]  = 0;   A[2 + OFF]  = 0;  A[1 + OFF] = 0;

    u32 s0_a5 = S0(a5);
    u32 e11 = E_arr(A, 11), e12 = E_arr(A, 12);
    u32 c9  = S0(a7) - a8 - K[9] + s0_a5 - a5;
    u32 c10 = e11 - 2u * a7 + s0_a5 + a5 + 2u - K[10];
    u32 c11 = e12 - S1(e11) - 2u * a8 + S0(a7) + a5 + 1u - K[11];
    u32 W12 = Wt_arr(A, 12);
    u32 W13 = Wt_arr(A, 13);
    u32 W16 = Wt_arr(A, 16);
    u32 W17 = Wt_arr(A, 17);
    u32 W18 = Wt_arr(A, 18);
    u32 W19 = Wt_arr(A, 19);
    u32 W20 = (R >= 21) ? Wt_arr(A, 20) : 0u;

    *out = (compact_ctx21_22_t){
        a14, a13, a12, a9, a5,
        W16 - c9,
        W17 - s1(PADLEN_BITS) - c10,
        W18 - s1(W16) - c11,
        W19 - s1(W17) - W12,
        W20 - s1(W18) - W13,
        a5 - s0_a5 - K[5]
    };
}

// Legacy Uninsulated Carry-Sudoku Evaluator (random a14/a13/a12_seed, no Theories 37-39)
static int eval_ctx_legacy_r(const u32 A_tmpl[36], int R, u32 a14, u32 a13, u32 a12_seed,
                             compact_ctx21_22_t *out_hits, int max_hits, u64 *p_s22) {
    const u32 a15 = A_tmpl[15 + OFF], a16 = A_tmpl[16 + OFF];
    const u32 e15 = 0xffffffffu;
    const u32 c_e16_base  = a16 - S0(a15) - MAJ(a15, a14, a13);
    const u32 c_e12_const = c_e16_base - 0xffffffffu - K[15] - PADLEN_BITS;

    u32 a11 = e15 - a15 + S0(a14) + MAJ(a14, a13, a12_seed);
    u32 a10 = a11;
    u32 e14 = a14 - S0(a13) - MAJ(a13, a12_seed, a11) + a10;
    u32 M_sub = ~(a14 ^ a13) & ~(a13 ^ a11);
    u32 s0_a11 = S0(a11);
    u32 c_e11  = e15 - a11 - S1(e14) - K[14];
    u32 c_W13  = e14 - a10 - 0xffffffffu - K[13];
    u32 c_a7   = s0_a11 - a11;
    u32 c_a6   = 0xffffffffu - a10;
    u32 e12    = c_e12_const - e14;
    u32 c_e11_base = c_e11 - (~e14 & e12);
    u32 c_a7_inv   = c_e11_base + c_a7;
    u32 c_a8_inv   = e12 + s0_a11 + a11;

    u32 sub_b[4]; int n_sub_b = 0; u32 m_tmp = M_sub;
    while (m_tmp && n_sub_b < 4) { u32 lsb = m_tmp & (~m_tmp + 1u); sub_b[n_sub_b++] = lsb; m_tmp ^= lsb; }
    int n_sub = 1 << n_sub_b, n_hits = 0;

    for (int sub_i = 0; sub_i < n_sub; sub_i++) {
        u32 delta = ((sub_i & 1) ? sub_b[0] : 0u) | ((sub_i & 2) ? sub_b[1] : 0u) |
                    ((sub_i & 4) ? sub_b[2] : 0u) | ((sub_i & 8) ? sub_b[3] : 0u);
        u32 a12 = a12_seed ^ delta;
        u32 c_e13 = a13 - S0(a12) - a11;
        u32 a8 = c_a8_inv - a12;
        u32 c_a5_s0a8 = 0xffffffffu + S0(a8);
        u32 m_10_8 = a10 ^ a8;
        u32 c_a7_full = c_a7_inv + (a10 & a8);

        for (u32 s0_v = 0; s0_v < 16u; s0_v++) {
            u32 a9_0a = s0_v - c_e13, X_0a = c_a5_s0a8 - a9_0a;
            u32 a7_b0a = c_a7_full - (e14 & s0_v), a7_0a = a7_b0a + (m_10_8 & a9_0a), D_0a = a8 ^ a7_0a;
            if ((((X_0a + CH(D_0a, X_0a, a8)) ^ X_0a) & D_0a & 0xfu) != 0u) continue;
            for (u32 s4 = 0; s4 < (16u << 4); s4 += (1u << 4)) {
                u32 a9_0 = a9_0a + s4, X_0 = X_0a - s4;
                u32 a7_b0 = a7_b0a - (e14 & s4), a7_0 = a7_b0 + (m_10_8 & a9_0), D_0 = a8 ^ a7_0;
                if ((((X_0 + CH(D_0, X_0, a8)) ^ X_0) & D_0 & 0xffu) != 0u) continue;
                u32 e13_lo = s0_v | s4;
                u32 e11_lo = (c_e11_base - (e14 & e13_lo)) & 0xffu;
                u32 req_S1_lo = (c_W13 - CH(e13_lo, e12, e11_lo) - 0x80u) & 0xffu;
                u32 req_b7_xor_lo0 = ((req_S1_lo >> 7) ^ e13_lo) & 1u;
                for (u32 s8 = 0; s8 < (16u << 8); s8 += (1u << 8)) {
                    u32 a9_1 = a9_0 + s8, X_1 = X_0 - s8;
                    u32 a7_b1 = a7_b0 - (e14 & s8), a7_1 = a7_b1 + (m_10_8 & a9_1), D_1 = a8 ^ a7_1;
                    if ((((X_1 + CH(D_1, X_1, a8)) ^ X_1) & D_1 & 0xfffu) != 0u) continue;
                    for (u32 s12 = 0; s12 < (8u << 12); s12 += (1u << 12)) {
                        u32 s12_b = s12 | (((req_b7_xor_lo0 ^ (s12 >> 13)) & 1u) << 18);
                        u32 a9_2 = a9_1 + s12_b, X_2 = X_1 - s12_b;
                        u32 a7_b2 = a7_b1 - (e14 & s12_b), a7_2 = a7_b2 + (m_10_8 & a9_2), D_2 = a8 ^ a7_2;
                        if ((((X_2 + CH(D_2, X_2, a8)) ^ X_2) & D_2 & 0x7fffu) != 0u) continue;
                        u32 e13_14 = e13_lo | s8 | s12;
                        for (u32 s15 = 0; s15 < (8u << 15); s15 += (1u << 15)) {
                            u32 a9_5 = a9_2 + s15, X_5 = X_2 - s15;
                            u32 a7_b5 = a7_b2 - (e14 & s15), a7_5 = a7_b5 + (m_10_8 & a9_5), D_5 = a8 ^ a7_5;
                            if ((((X_5 + CH(D_5, X_5, a8)) ^ X_5) & D_5 & 0x7ffffu) != 0u) continue;
                            u32 hi25 = ((req_S1_lo ^ S1(e13_14 | s15)) & 0x7fu) << 25;
                            u32 a9_base = a9_5 + hi25, X0 = X_5 - hi25, a7_base = a7_b5 - (e14 & hi25);
                            for (u32 s19 = 0; s19 < (8u << 19); s19 += (1u << 19)) {
                                u32 a9_i1 = a9_base + s19, X_i1 = X0 - s19;
                                u32 a7_bi1 = a7_base - (e14 & s19), a7_i1 = a7_bi1 + (m_10_8 & a9_i1), D_i1 = a8 ^ a7_i1;
                                if ((((X_i1 + CH(D_i1, X_i1, a8)) ^ X_i1) & D_i1 & 0x3fffffu) != 0u) continue;
                                for (u32 s22 = 0; s22 < (8u << 22); s22 += (1u << 22)) {
                                    if (p_s22) (*p_s22)++;
                                    u32 a9 = a9_i1 + s22, X = X_i1 - s22;
                                    u32 a7 = a7_bi1 - (e14 & s22) + (m_10_8 & a9), D = a8 ^ a7, S = X + CH(D, X, a8);
                                    if (__builtin_expect(((S ^ X) & D) != 0u, 1)) continue;
                                    u32 a6 = c_a6 + S0(a9) + MAJ(a9, a8, a7);
                                    if (__builtin_expect(((a6 ^ S) & ~D) == 0u, 0)) {
                                        if (out_hits && n_hits < max_hits)
                                            emit_ctx21_22(A_tmpl, R, a14, a13, a12, a11, a9, a8, a7, a6, &out_hits[n_hits]);
                                        n_hits++;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return n_hits;
}

// Carbon Next-Gen Carry-Sudoku Evaluator with Sigma0^-1 Top Insulation ((e14 & 0x01ffff00) == 0) + Theories 37-39!
// Because (e14 & 0x01ffff00) == 0, ALL FIVE inner-stage carry perturbations (e14 & s8), (e14 & s12_b),
// (e14 & s15), (e14 & s19), and (e14 & s22) VANISH IDENTICALLY (0 ops!), and at R=22 e14 == 0 vanishes everywhere!
static int eval_ctx_carbon_insulated(const u32 A_tmpl[36], int R, u32 a14, u32 a13, u32 a12_seed,
                                     compact_ctx21_22_t *out_hits, int max_hits, u64 *p_s22) {
    const u32 a15 = A_tmpl[15 + OFF], a16 = A_tmpl[16 + OFF];
    const u32 e15 = 0xffffffffu;
    const u32 c_e16_base  = a16 - S0(a15) - MAJ(a15, a14, a13);
    const u32 c_e12_const = c_e16_base - 0xffffffffu - K[15] - PADLEN_BITS;

    u32 a11 = e15 - a15 + S0(a14) + MAJ(a14, a13, a12_seed);
    u32 a10 = a11;
    u32 e14 = a14 - S0(a13) - MAJ(a13, a12_seed, a11) + a10;
    u32 M_sub = ~(a14 ^ a13) & ~(a13 ^ a11);
    u32 s0_a11 = S0(a11);
    u32 c_e11  = e15 - a11 - S1(e14) - K[14];
    u32 c_W13  = e14 - a10 - 0xffffffffu - K[13];
    u32 c_a7   = s0_a11 - a11;
    u32 c_a6   = 0xffffffffu - a10;
    u32 e12    = c_e12_const - e14;
    u32 c_e11_base = c_e11 - (~e14 & e12);
    u32 c_a7_inv   = c_e11_base + c_a7;
    u32 c_a8_inv   = e12 + s0_a11 + a11;

    u32 sub_b[4]; int n_sub_b = 0; u32 m_tmp = M_sub;
    while (m_tmp && n_sub_b < 4) { u32 lsb = m_tmp & (~m_tmp + 1u); sub_b[n_sub_b++] = lsb; m_tmp ^= lsb; }
    int n_sub = 1 << n_sub_b, n_hits = 0;

    for (int sub_i = 0; sub_i < n_sub; sub_i++) {
        u32 delta = ((sub_i & 1) ? sub_b[0] : 0u) | ((sub_i & 2) ? sub_b[1] : 0u) |
                    ((sub_i & 4) ? sub_b[2] : 0u) | ((sub_i & 8) ? sub_b[3] : 0u);
        u32 a12 = a12_seed ^ delta;
        u32 c_e13 = a13 - S0(a12) - a11;
        u32 a8 = c_a8_inv - a12;
        u32 c_a5_s0a8 = 0xffffffffu + S0(a8);
        u32 m_10_8 = a10 ^ a8;
        u32 c_a7_full = c_a7_inv + (a10 & a8);

        for (u32 s0_v = 0; s0_v < 16u; s0_v++) {
            u32 a9_0a = s0_v - c_e13, X_0a = c_a5_s0a8 - a9_0a;
            u32 a7_b0a = c_a7_full - (e14 & s0_v), a7_0a = a7_b0a + (m_10_8 & a9_0a), D_0a = a8 ^ a7_0a;
            if ((((X_0a + CH(D_0a, X_0a, a8)) ^ X_0a) & D_0a & 0xfu) != 0u) continue;

            for (u32 s4 = 0; s4 < (16u << 4); s4 += (1u << 4)) {
                u32 a9_0 = a9_0a + s4, X_0 = X_0a - s4;
                u32 a7_b0 = a7_b0a - (e14 & s4), a7_0 = a7_b0 + (m_10_8 & a9_0), D_0 = a8 ^ a7_0;
                u32 S_0 = X_0 + CH(D_0, X_0, a8);
                if (((S_0 ^ X_0) & D_0 & 0xffu) != 0u) continue;

                u32 B_lo6 = (c_a6 + MAJ(a9_0, a8, a7_0)) & 0x3fu;
                u32 S_lo6 = S_0 & 0x3fu, nD_lo6 = (~D_0) & 0x3fu, r2_lo6 = (a9_0 >> 2) & 0x3fu;
                u64 valid_z64 = ROR64(VALID_SUM64[S_lo6 & nD_lo6][nD_lo6], B_lo6);
                if (valid_z64 == 0ULL) continue;

                u32 e13_lo = s0_v | s4;
                u32 e11_lo = (c_e11_base - (e14 & e13_lo)) & 0xffu;
                u32 req_S1_lo = (c_W13 - CH(e13_lo, e12, e11_lo) - 0x80u) & 0xffu;
                u32 req_b7_xor_lo0 = ((req_S1_lo >> 7) ^ e13_lo) & 1u;

                // Insulated inner tree: (e14 & 0x01ffff00) == 0 -> a7_b0 is invariant across s8, s12_b, s15!
                for (u32 s8 = 0; s8 < (16u << 8); s8 += (1u << 8)) {
                    u32 a9_1 = a9_0 + s8, X_1 = X_0 - s8;
                    u32 a7_1 = a7_b0 + (m_10_8 & a9_1), D_1 = a8 ^ a7_1;
                    if ((((X_1 + CH(D_1, X_1, a8)) ^ X_1) & D_1 & 0xfffu) != 0u) continue;
                    u32 diff_s8 = (req_S1_lo ^ S1(e13_lo | s8)) & 0x7fu;

                    for (u32 s12 = 0; s12 < (8u << 12); s12 += (1u << 12)) {
                        u32 s12_b = s12 | (((req_b7_xor_lo0 ^ (s12 >> 13)) & 1u) << 18);
                        u32 a9_2 = a9_1 + s12_b, X_2 = X_1 - s12_b;
                        u32 a7_2 = a7_b0 + (m_10_8 & a9_2), D_2 = a8 ^ a7_2;
                        if ((((X_2 + CH(D_2, X_2, a8)) ^ X_2) & D_2 & 0x7fffu) != 0u) continue;
                        u32 hi25_s12 = (diff_s8 ^ ((s12 >> 6) & 0x40u) ^ (s12 >> 11)) << 25;

                        for (u32 s15 = 0; s15 < (8u << 15); s15 += (1u << 15)) {
                            u32 a9_5 = a9_2 + s15, X_5 = X_2 - s15;
                            u32 a7_5 = a7_b0 + (m_10_8 & a9_5), D_5 = a8 ^ a7_5;
                            u32 S_5  = X_5 + CH(D_5, X_5, a8);
                            if (((S_5 ^ X_5) & D_5 & 0x7ffffu) != 0u) continue;

                            u32 hi25    = hi25_s12 ^ (s15 << 14);
                            u32 a9_base = a9_5 + hi25;
                            u32 R5_lo6  = r2_lo6 ^ ((a9_5 >> 13) & 0x3fu);
                            u32 q0      = (a9_base >> 22) & 0x3fu;
                            if (((valid_z64 & MASK8_LUT[R5_lo6][q0]) |
                                 (valid_z64 & MASK8_LUT[R5_lo6][(q0 + 1u) & 63u])) == 0ULL) continue;

                            u32 B_lo19  = (c_a6 + MAJ(a9_5, a8, a7_5)) & 0x7ffffu;
                            u32 S_lo19  = S_5 & 0x7ffffu, nD_lo19 = (~D_5) & 0x7ffffu;
                            u32 a9_lo19 = a9_5 & 0x7ffffu;
                            u32 R5_mid8 = ((a9_lo19 >> 11) ^ (a9_lo19 << 1)) & 0xffu;
                            u32 B_mid8  = (B_lo19 >> 9) & 0xffu, S_mid8 = (S_lo19 >> 9) & 0xffu, nD_mid8 = (nD_lo19 >> 9) & 0xffu;
                            u32 u22_0   = a9_base >> 22;

                            u32 valid_u9 = 0u;
                            #pragma clang loop unroll(full)
                            for (u32 u = 0; u < 9u; u++) {
                                u32 z6    = R5_lo6 ^ ((q0 + u) & 63u);
                                u32 ok_lo = (u32)((valid_z64 >> z6) & 1ULL);
                                u32 u22   = (u22_0 + u) & 0x3ffu;
                                u32 sum0  = B_mid8 + (R5_mid8 ^ ((u22 ^ (u22 >> 9)) & 0xffu));
                                u32 ok_m8 = ((((sum0 ^ S_mid8) & nD_mid8) == 0u) |
                                             ((((sum0 + 1u) ^ S_mid8) & nD_mid8) == 0u));
                                valid_u9 |= (ok_lo & ok_m8) << u;
                            }
                            if (!valid_u9) continue;

                            u32 j_split = 8u - ((a9_base >> 19) & 7u);
                            u32 km_c0   = valid_u9 & 0xffu, km_c1 = (valid_u9 >> 1) & 0xffu;
                            u32 j_lo    = km_c0 ? 0u : j_split, j_hi = km_c1 ? 8u : j_split;
                            u32 X0      = X_5 - hi25;
                            u32 a7_base = a7_b0 - (e14 & hi25);

                            for (u32 j = j_lo; j < j_hi; j++) {
                                u32 s19 = j << 19, a9_i1 = a9_base + s19, X_i1 = X0 - s19;
                                u32 a7_i1 = a7_base + (m_10_8 & a9_i1), D_i1 = a8 ^ a7_i1;
                                if ((((X_i1 + CH(D_i1, X_i1, a8)) ^ X_i1) & D_i1 & 0x3fffffu) != 0u) continue;

                                u32 a9_lo22  = a9_i1 & 0x3fffffu;
                                u32 R_lo19   = ((a9_lo22 >> 2) ^ (a9_lo22 >> 13) ^ (a9_lo22 << 10)) & 0x7ffffu;
                                u32 u22_base = a9_i1 >> 22;
                                u32 km       = (j < j_split) ? km_c0 : km_c1;

                                for (; km; km &= km - 1u) {
                                    u32 k = (u32)__builtin_ctz(km);
                                    u32 u22 = (u22_base + k) & 0x3ffu;
                                    if ((((B_lo19 + (R_lo19 ^ u22 ^ ((u22 << 9) & 0x7ffffu))) ^ S_lo19) & nD_lo19) != 0u) continue;
                                    if (p_s22) (*p_s22)++;
                                    u32 s22 = k << 22, a9 = a9_i1 + s22, X = X_i1 - s22;
                                    u32 a7 = a7_base + (m_10_8 & a9), D = a8 ^ a7, S = X + CH(D, X, a8);
                                    if (__builtin_expect(((S ^ X) & D) != 0u, 1)) continue;
                                    u32 a6 = c_a6 + S0(a9) + MAJ(a9, a8, a7);
                                    if (__builtin_expect(((a6 ^ S) & ~D) == 0u, 0)) {
                                        if (out_hits && n_hits < max_hits)
                                            emit_ctx21_22(A_tmpl, R, a14, a13, a12, a11, a9, a8, a7, a6, &out_hits[n_hits]);
                                        n_hits++;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return n_hits;
}

typedef struct {
    const u32 *A_tmpl;
    int R;
    int insulated;
    u32 n_seeds;
    _Atomic u32 *seed_ctr;
    _Atomic u64 *tot_hits;
    _Atomic u64 *tot_s22;
} Ctx21BenchArg;

static inline void synth_seed_r21_r22(const u32 A_tmpl[36], int R, int insulated, u32 idx,
                                      u32 *a14_out, u32 *a13_out, u32 *a12_out) {
    u32 a15 = A_tmpl[15 + OFF];
    if (!insulated) {
        u32 a14 = (R == 21) ? A_tmpl[14 + OFF] : perm32(idx * 3u + 0x11111111u);
        u32 a13 = perm32(idx * 5u + 0x22222222u);
        u32 a12 = perm32(idx * 7u + 0x33333333u);
        *a14_out = a14; *a13_out = a13; *a12_out = a12;
    } else if (R == 21) {
        // Exact Fixed-Target R=21 Sigma0^-1 Sparse-e14* Bijection ((e14* & 0x01ffff00) == 0):
        u32 a14  = A_tmpl[14 + OFF];
        u32 idx15 = idx & 0x7fffu;
        u32 e14_star = (idx15 & 0xffu) | ((idx15 >> 8) << 25);
        u32 C_14_15  = a14 + S0(a14) + 0xffffffffu - a15;
        u32 a13 = inv_S0(C_14_15 - e14_star);
        *a14_out = a14; *a13_out = a13; *a12_out = a13;
    } else {
        // Exact Fixed-Target R=22 Sigma0^-1 Domain-Wall Annihilator (e14 == 0x00000000 IDENTICALLY):
        u32 a14 = perm32(idx + 0x9e3779b9u);
        u32 C_14_15 = a14 + S0(a14) + 0xffffffffu - a15;
        u32 a13 = inv_S0(C_14_15);
        *a14_out = a14; *a13_out = a13; *a12_out = a13;
    }
}

static void *ctx21_22_bench_worker(void *arg) {
    Ctx21BenchArg *ba = (Ctx21BenchArg *)arg;
    u64 loc_hits = 0, loc_s22 = 0;
    for (;;) {
        u32 base = atomic_fetch_add_explicit(ba->seed_ctr, 4u, memory_order_relaxed);
        if (base >= ba->n_seeds) break;
        u32 end = base + 4u; if (end > ba->n_seeds) end = ba->n_seeds;
        for (u32 idx = base; idx < end; idx++) {
            u32 a14, a13, a12;
            synth_seed_r21_r22(ba->A_tmpl, ba->R, ba->insulated, idx, &a14, &a13, &a12);
            if (ba->insulated) loc_hits += (u64)eval_ctx_carbon_insulated(ba->A_tmpl, ba->R, a14, a13, a12, NULL, 0, &loc_s22);
            else               loc_hits += (u64)eval_ctx_legacy_r(ba->A_tmpl, ba->R, a14, a13, a12, NULL, 0, &loc_s22);
        }
    }
    atomic_fetch_add_explicit(ba->tot_hits, loc_hits, memory_order_relaxed);
    atomic_fetch_add_explicit(ba->tot_s22,  loc_s22,  memory_order_relaxed);
    return NULL;
}

// ====================================================================================================
// PART 3: CARBON LEFT-STREAM ENGINE + HOP-4 (W4) & GATED HOP-5 (W5) + NEYMAN-PEARSON FILTER ABLATION
// ====================================================================================================
#define KER_BITS     5
#define KER_MASK     ((1u << KER_BITS) - 1u)
#define LUT_BUCKETS  (1ULL << (32 - KER_BITS))
#define BM_WORDS     (LUT_BUCKETS >> 5)
#define KER4_BITS    4
#define KER4_MASK    ((1u << KER4_BITS) - 1u)
#define LUT4_BUCKETS (1ULL << (32 - KER4_BITS))
#define PF_BATCH     64
#define B_COSET_MAX  16

static const int NEON_MAX_KHI[17] = {
    6,  6,  6,  5,  5,  5,  4,  4,
    4,  3,  2, -1, -1, -1, -1, -1, -1
};

static u32 C_W0, C_E1, C_G1_base, E0_const, Em1_const, A0_const, Am1_const, Am2_const;
static u32 C_G2_base, C_G3_base, MAJ1_CONST, MAJ1_XOR, CH1_XOR, C_G1_folded, Am2_folded, C_t16_folded;
static u32 S0_LO4[16], FLO_MAJ_SUB[16], S0_HI2[4], S1_HI2[4], S1_MID7[128], CH1_MID7[128];

static u32 *H_pack4   = NULL; // 4.0 GB MAP_SHARED + MADV_RANDOM read-only table (0 MB dirty anon RAM)
static u16 *ht_Q0_kb4 = NULL; // 512 MB KB=4 direct-mapped u16 kernel (0-CAS, 0 MB bitmap)
static u32 *ht_Q0_ker = NULL; // 512 MB KB=5 2-slot u32 kernel (0-CAS)
static u32 *bm_Q0_ker = NULL; // 16 MB P-core L2 bitmap (Total anon = 528 MB, 0 swap!)

typedef struct { int tid; } SliceArg;

static void init_left_constants_and_hpack4(void) {
    u32 A_iv[36];
    init_state_r(A_iv, H_ZERO, 21);
    C_W0 = Wt_arr(A_iv, 0); C_E1 = E_arr(A_iv, 1); E0_const = E_arr(A_iv, 0); Em1_const = E_arr(A_iv, -1);
    u32 Em2_const = E_arr(A_iv, -2);
    A0_const = A_iv[0 + OFF]; Am1_const = A_iv[-1 + OFF]; Am2_const = A_iv[-2 + OFF];
    C_G1_base = Em2_const + K[1]; C_G2_base = Em1_const + K[2]; C_G3_base = E0_const + K[3];
    MAJ1_CONST = A0_const & Am1_const; MAJ1_XOR = A0_const ^ Am1_const; CH1_XOR = E0_const ^ Em1_const;
    C_G1_folded = C_G1_base + MAJ1_CONST; Am2_folded = Am2_const - MAJ1_CONST; C_t16_folded = C_G1_folded - C_W0;

    for (u32 s = 0; s < 16u; s++) { S0_LO4[s] = S0(s << 18); FLO_MAJ_SUB[s] = ((s << 18) & MAJ1_XOR) - (s << 18); }
    for (u32 f = 0; f < 4u; f++)  { S0_HI2[f] = S0(f << 30); S1_HI2[f] = S1(f << 30); }
    for (u32 m = 0; m < 128u; m++) { S1_MID7[m] = S1(m << 18); CH1_MID7[m] = (Em1_const ^ ((m << 18) & CH1_XOR)) & 0x01fc0000u; }

    int fd = open("/tmp/q0_u16_pack2.bin", O_RDONLY);
    if (fd >= 0) {
        struct stat st;
        if (fstat(fd, &st) == 0 && (u64)st.st_size == (1ULL << 32)) {
            void *p = mmap(NULL, 1ULL << 32, PROT_READ, MAP_SHARED, fd, 0);
            if (p != MAP_FAILED) { H_pack4 = (u32 *)p; madvise(H_pack4, 1ULL << 32, MADV_RANDOM); }
        }
        close(fd);
    }
    if (!H_pack4) { fprintf(stderr, "Missing /tmp/q0_u16_pack2.bin (run exp_r20_distant_theories once first)\n"); exit(1); }
}

static void *build_kb4_slice(void *arg) {
    const u32 tid = (u32)((SliceArg *)arg)->tid;
    u32 d_tbl[1024], s_tbl[1024];
    for (u32 u = 0; u < 1024u; u++) { d_tbl[u] = u << 22; s_tbl[u] = s0(u << 22); }
    for (u32 ulo18 = 1u; ulo18 < (1u << 18); ulo18++) {
        u16 sv = (u16)(ulo18 & 0xffffu); if (!sv) continue;
        u32 base_u = ulo18 | (((ulo18 ^ (ulo18 >> 3) ^ (ulo18 >> 7)) & KER4_MASK) << 18);
        u32 s0_b = s0(base_u), c3 = s0_b >> 29;
        u32 b0 = ((c3 - tid) & 7u) << 7, b1 = ((c3 - tid - 1u) & 7u) << 7;
        for (u32 u = b0; u < b0 + 128u; u++) { u32 y = (s0_b ^ s_tbl[u]) - (base_u | d_tbl[u]); if ((y >> 29) == tid) ht_Q0_kb4[y >> KER4_BITS] = sv; }
        for (u32 u = b1; u < b1 + 128u; u++) { u32 y = (s0_b ^ s_tbl[u]) - (base_u | d_tbl[u]); if ((y >> 29) == tid) ht_Q0_kb4[y >> KER4_BITS] = sv; }
    }
    return NULL;
}

static void *build_kb5_slice(void *arg) {
    const u32 tid = (u32)((SliceArg *)arg)->tid;
    u32 d_tbl[512], s_tbl[512];
    for (u32 u = 0; u < 512u; u++) { d_tbl[u] = u << 23; s_tbl[u] = s0(u << 23); }
    for (u32 ulo18 = 1u; ulo18 < (1u << 18); ulo18++) {
        u32 sv = ulo18 & 0xffffu; if (!sv) continue;
        u32 base_u = ulo18 | (((ulo18 ^ (ulo18 >> 3) ^ (ulo18 >> 7)) & KER_MASK) << 18);
        u32 s0_b = s0(base_u), c3 = s0_b >> 29;
        u32 b0 = ((c3 - tid) & 7u) << 6, b1 = ((c3 - tid - 1u) & 7u) << 6;
        for (u32 u = b0; u < b0 + 64u; u++) {
            u32 y = (s0_b ^ s_tbl[u]) - (base_u | d_tbl[u]);
            if ((y >> 29) == tid) { u32 idx = y >> KER_BITS; ht_Q0_ker[idx] = (ht_Q0_ker[idx] << 16) | sv; bm_Q0_ker[idx >> 5] |= (1u << (idx & 31u)); }
        }
        for (u32 u = b1; u < b1 + 64u; u++) {
            u32 y = (s0_b ^ s_tbl[u]) - (base_u | d_tbl[u]);
            if ((y >> 29) == tid) { u32 idx = y >> KER_BITS; ht_Q0_ker[idx] = (ht_Q0_ker[idx] << 16) | sv; bm_Q0_ker[idx >> 5] |= (1u << (idx & 31u)); }
        }
    }
    return NULL;
}

static inline u32 synth_a1_coset_base_kb(u32 stream_step, u32 target_rem, u32 ker_mask, u32 *next_step_out) {
    u32 step = stream_step;
    for (;;) {
        step++;
        u32 idx21 = (step * 1332533u + 0x10579BDFu) & 0x001fffffu;
        u32 a1_0  = (idx21 & 0x0000ffffu) | ((idx21 >> 16) << 25);
        u32 e1_0  = a1_0 + C_E1, s0_0 = S0(a1_0);
        u32 t16_0 = target_rem - (a1_0 + C_W0) + C_G1_base + s0_0 + MAJ(a1_0, A0_const, Am1_const)
                  + S1(e1_0) + CH(e1_0, E0_const, Em1_const);
        u32 diff     = (s0_0 ^ (s0_0 - t16_0)) & ker_mask;
        u32 delta_a1 = ((diff & 7u) << 22) | (((diff >> 3) & 3u) << 16);
        if (((e1_0 & 0x01ffffffu) + delta_a1 + (15u << 18)) < 0x02000000u) {
            *next_step_out = step;
            return a1_0 | delta_a1;
        }
    }
}

static inline u32 rec_u_z(u32 ulo, u32 z) {
    u32 v1  = z ^ (ulo >> 7) ^ (ulo << 14) ^ (ulo >> 3);
    u32 b0  = ((v1 << 15) ^ (v1 << 11) ^ v1) & 0x00010000u;
    u32 b12 = ((v1 << 14) ^ (v1 << 3)) & 0x00060000u;
    u32 T   = b0 | b12 | ((v1 << 18) & 0xfff80000u);
    return ulo | (T ^ (T << 11) ^ (b0 << 15));
}

typedef struct { u32 a1, e1, a2, e2, sm2; } q2_cold_t;
typedef struct { u32 t18, G3, M3; u16 free_lo; u8 n_free_lo, use_neon; } q3_hot_t;
typedef struct { u32 a1, a2, a3, e2, e3, c_e4, c_W4; } q3_cold_t;

typedef struct {
    int found;
    int max_ctz_rho19;
    int max_match_bits;
    u32 best_rho19;
    u32 best_W[16];
    u32 best_H[8];
} HarvestState;

typedef struct {
    int tid;
    int R;
    int b_coset;
    int w_thresh;
    u32 n_blocks_per_thread;
    u32 coset_idx;
    const compact_ctx21_22_t *rctx;
    const u32 *A_tmpl;
    const u32 *H_targ;
    _Atomic u64 *tot_chains;
    _Atomic u64 *tot_sol19;
    _Atomic u32 *shared_blk_ctr;
    pthread_mutex_t *harv_mtx;
    HarvestState *harv;
    double deadline_sec;
} Left21Arg;

static void record_sol19_harvest(u32 W3, u32 G3, u32 W4, u32 rho19, const q3_cold_t *cold,
                                 const compact_ctx21_22_t *ctx, Left21Arg *ba) {
    int ctz = rho19 ? __builtin_ctz(rho19) : 32;
    pthread_mutex_lock(ba->harv_mtx);
    if (!ba->harv->found || ctz > ba->harv->max_ctz_rho19) {
        u32 A[36];
        memcpy(A, ba->A_tmpl, sizeof(A));
        u32 a14 = ctx->a14, a13 = ctx->a13, a12 = ctx->a12, a15 = A[15 + OFF], a16 = A[16 + OFF];
        u32 a11 = 0xffffffffu - a15 + S0(a14) + MAJ(a14, a13, a12), a10 = a11, a9 = ctx->a9;
        u32 e14 = a14 - S0(a13) - MAJ(a13, a12, a11) + a10;
        u32 e16 = a16 - S0(a15) - MAJ(a15, a14, a13) + a12;
        u32 e13 = a13 - S0(a12) - a11 + a9;
        u32 e12 = e16 - a12 - 0xffffffffu - K[15] - PADLEN_BITS - e14;
        u32 e11 = 0xffffffffu - a11 - S1(e14) - K[14] - CH(e14, e13, e12);
        u32 a8  = e12 + S0(a11) - a12 + a11;
        u32 a7  = e11 + S0(a10) - a11 + MAJ(a10, a9, a8);
        A[14 + OFF] = a14; A[13 + OFF] = a13; A[12 + OFF] = a12; A[11 + OFF] = a11;
        A[10 + OFF] = a10; A[9 + OFF]  = a9;  A[8 + OFF]  = a8;  A[7 + OFF]  = a7;
        A[6 + OFF]  = ctx->a5; A[5 + OFF] = ctx->a5;
        A[4 + OFF]  = W3 + G3; A[3 + OFF] = cold->a3; A[2 + OFF] = cold->a2; A[1 + OFF] = cold->a1;
        u32 Wm[16], H_out[8];
        for (int t = 0; t < 16; t++) Wm[t] = Wt_arr(A, t);
        (void)W4;
        compress_steps(IV, Wm, ba->R, H_out);
        int mbits = 0;
        for (int i = 0; i < 8; i++) mbits += 32 - __builtin_popcount(H_out[i] ^ ba->H_targ[i]);
        if (!ba->harv->found || ctz > ba->harv->max_ctz_rho19 ||
            (ctz == ba->harv->max_ctz_rho19 && mbits > ba->harv->max_match_bits)) {
            ba->harv->found = 1;
            ba->harv->max_ctz_rho19 = ctz;
            ba->harv->max_match_bits = mbits;
            ba->harv->best_rho19 = rho19;
            memcpy(ba->harv->best_W, Wm, sizeof(Wm));
            memcpy(ba->harv->best_H, H_out, sizeof(H_out));
        }
    }
    pthread_mutex_unlock(ba->harv_mtx);
}

// 1-Round Hop-4 (W4 -> rho19) + Gated 1-Round Hop-5 (W5 -> rho20, +a2-a2 cancelled!) Verifier
__attribute__((noinline))
static void check_sol19_hop4_hop5(u32 W3, u32 G3, const q3_cold_t *cold, const compact_ctx21_22_t *ctx,
                                  u64 *loc_sol19, Left21Arg *ba) {
    (*loc_sol19)++;
    u32 a4  = W3 + G3;
    u32 e4  = a4 + cold->c_e4;
    u32 sm4 = S0(a4) + MAJ(a4, cold->a3, cold->a2);
    u32 W4  = cold->c_W4 - sm4 - S1(e4) - CH(e4, cold->e3, cold->e2);
    u32 rho19 = ctx->T19 - (s0(W4) + W3);

    if (ba->harv && (!ba->harv->found || (rho19 == 0u) || (__builtin_ctz(rho19) > ba->harv->max_ctz_rho19))) {
        record_sol19_harvest(W3, G3, W4, rho19, cold, ctx, ba);
    }
    // Gated Hop-5 check: executes ONLY when rho19 == 0 (probability 2^-32 -> 0.000% overhead on rejected sol19s!)
    if (__builtin_expect(rho19 == 0u, 0)) {
        u32 e5 = ctx->a5 - sm4 + cold->a1;
        u32 W5 = ctx->c_W5_base - a4 - cold->e2 - S1(e5) - CH(e5, e4, cold->e3);
        if (__builtin_expect(s0(W5) + W4 == ctx->T20, 0) && ba->harv) {
            record_sol19_harvest(W3, G3, W4, 0u, cold, ctx, ba);
        }
    }
}

typedef struct {
    u32       q2_t17[B_COSET_MAX][524];
    u32       q2_G2[B_COSET_MAX][524];
    q2_cold_t q2_cold[B_COSET_MAX][524];
    q3_hot_t  q3_hot[B_COSET_MAX][524];
    q3_cold_t q3_cold[B_COSET_MAX][524];
    u32       hit_t16[B_COSET_MAX][68];
    u8        hit_f[B_COSET_MAX][68];
} CarbonScratch21;

static inline void drain_q3_r21(const q3_hot_t *q3_hot, const q3_cold_t *q3_cold, int nq3,
                                const compact_ctx21_22_t *ctx, u64 *loc_sol19, Left21Arg *ba) {
    const u32 a5 = ctx->a5;
    const uint32x4_t v_ffff = vdupq_n_u32(0xffffu), v_8000 = vdupq_n_u32(0x00008000u);
    const uint32x4_t v_m_b0 = vdupq_n_u32(0x00010000u), v_m_b12 = vdupq_n_u32(0x00060000u);
    const uint32x4_t v_m_hi13 = vdupq_n_u32(0xfff80000u), v_e0 = vdupq_n_u32(0xe0000000u), v_a5 = vdupq_n_u32(a5);

    for (int i = 0; i < nq3; i++) {
        u32 t18 = q3_hot[i].t18, G3 = q3_hot[i].G3, M3 = q3_hot[i].M3, M3_lo = M3 & 0xffffu;
        if (ht_Q0_ker && (t18 & KER_MASK) == 0u) {
            u32 idx3 = t18 >> KER_BITS;
            if ((bm_Q0_ker[idx3 >> 5] >> (idx3 & 31u)) & 1u) {
                for (u32 p3 = ht_Q0_ker[idx3]; p3 != 0u; p3 >>= 16) {
                    u32 ulo3 = p3 & 0xffffu;
                    if (ulo3 && (((ulo3 + G3) ^ a5) & M3_lo) == 0u) {
                        u32 W3 = rec_u_z(ulo3, ulo3 + t18);
                        if ((((W3 + G3) ^ a5) & M3) == 0u) check_sol19_hop4_hop5(W3, G3, &q3_cold[i], ctx, loc_sol19, ba);
                    }
                }
            }
            continue;
        }
        if (q3_hot[i].use_neon) {
            u32 free_lo = q3_hot[i].free_lo, nfl = q3_hot[i].n_free_lo;
            u32 base_w3_lo = ((a5 & M3_lo) - G3) & 0xffffu;
            if (nfl <= 1u) {
                int n_pts = (nfl == 0u) ? 1 : 2;
                for (int s = 0; s < n_pts; s++) {
                    u32 w3_lo = (base_w3_lo + (s ? free_lo : 0u)) & 0xffffu;
                    if (!w3_lo) continue;
                    u32 v1 = (w3_lo + t18) ^ (w3_lo >> 7) ^ (w3_lo << 14) ^ (w3_lo >> 3);
                    u32 sv1 = (v1 << 15) ^ (v1 << 11) ^ v1;
                    if ((sv1 & 0x8000u) == 0u) {
                        u32 b0 = sv1 & 0x10000u, b12 = ((v1 << 14) ^ (v1 << 3)) & 0x60000u;
                        u32 T = b0 | b12 | ((v1 << 18) & 0xfff80000u), W3_l27 = w3_lo | T;
                        u32 W3 = W3_l27 ^ (T << 11) ^ (b0 << 15);
                        u32 s0h3 = ((W3_l27 >> 4) ^ (W3_l27 >> 15)) << 29;
                        if (((((W3 + G3) ^ a5) & M3) | (((W3 + t18) ^ s0h3) & 0xe0000000u)) == 0u && Qf(W3) == t18)
                            check_sol19_hop4_hop5(W3, G3, &q3_cold[i], ctx, loc_sol19, ba);
                    }
                }
            } else {
                u32 fb0 = free_lo & (0u - free_lo), rem1 = free_lo ^ fb0;
                u32 fb1 = rem1 & (0u - rem1), free_rest = rem1 ^ fb1;
                const uint32x4_t v_sub_lo4 = {0u, fb0, fb1, fb0 | fb1};
                const uint32x4_t v_t18 = vdupq_n_u32(t18), v_G3 = vdupq_n_u32(G3), v_M3 = vdupq_n_u32(M3);
                u32 sub = 0;
                do {
                    uint32x4_t v_w = vandq_u32(vaddq_u32(vdupq_n_u32(base_w3_lo + sub), v_sub_lo4), v_ffff);
                    uint32x4_t v_v1 = veorq_u32(veorq_u32(vaddq_u32(v_w, v_t18), vshrq_n_u32(v_w, 7)),
                                                veorq_u32(vshlq_n_u32(v_w, 14), vshrq_n_u32(v_w, 3)));
                    uint32x4_t v_sv1 = veorq_u32(veorq_u32(vshlq_n_u32(v_v1, 15), vshlq_n_u32(v_v1, 11)), v_v1);
                    uint32x4_t v_b0  = vandq_u32(v_sv1, v_m_b0);
                    uint32x4_t v_b12 = vandq_u32(veorq_u32(vshlq_n_u32(v_v1, 14), vshlq_n_u32(v_v1, 3)), v_m_b12);
                    uint32x4_t v_T   = vorrq_u32(vorrq_u32(v_b0, v_b12), vandq_u32(vshlq_n_u32(v_v1, 18), v_m_hi13));
                    uint32x4_t v_Wl  = vorrq_u32(v_w, v_T);
                    uint32x4_t v_W3  = veorq_u32(v_Wl, veorq_u32(vshlq_n_u32(v_T, 11), vshlq_n_u32(v_b0, 15)));
                    uint32x4_t v_s0h = vshlq_n_u32(veorq_u32(vshrq_n_u32(v_Wl, 4), vshrq_n_u32(v_Wl, 15)), 29);
                    uint32x4_t v_err = vorrq_u32(vandq_u32(v_sv1, v_8000),
                                       vorrq_u32(vandq_u32(veorq_u32(vaddq_u32(v_W3, v_G3), v_a5), v_M3),
                                                 vandq_u32(veorq_u32(vaddq_u32(v_W3, v_t18), v_s0h), v_e0)));
                    if (__builtin_expect(vminvq_u32(v_err) == 0u, 0)) {
                        u32 wc[4], ec[4]; vst1q_u32(wc, v_W3); vst1q_u32(ec, v_err);
                        for (int q = 0; q < 4; q++)
                            if (ec[q] == 0u && Qf(wc[q]) == t18) check_sol19_hop4_hop5(wc[q], G3, &q3_cold[i], ctx, loc_sol19, ba);
                    }
                    sub = (sub - free_rest) & free_rest;
                } while (sub != 0);
            }
            continue;
        }
        for (u32 p3 = H_pack4[t18 >> 2]; p3 != 0u; p3 >>= 16) {
            u32 ulo3 = p3 & 0xffffu;
            if (ulo3 && __builtin_expect((((ulo3 + G3) ^ a5) & M3_lo) == 0u, 0)) {
                u32 v1 = (ulo3 + t18) ^ (ulo3 >> 7) ^ (ulo3 << 14) ^ (ulo3 >> 3);
                u32 sv1 = (v1 << 15) ^ (v1 << 11) ^ v1;
                if ((sv1 & 0x8000u) == 0u) {
                    u32 b0 = sv1 & 0x10000u, b12 = ((v1 << 14) ^ (v1 << 3)) & 0x60000u;
                    u32 T = b0 | b12 | ((v1 << 18) & 0xfff80000u), W3_l27 = ulo3 | T;
                    u32 W3 = W3_l27 ^ (T << 11) ^ (b0 << 15);
                    u32 s0h3 = ((W3_l27 >> 4) ^ (W3_l27 >> 15)) << 29;
                    if (((((W3 + G3) ^ a5) & M3) | (((W3 + t18) ^ s0h3) & 0xe0000000u)) == 0u && Qf(W3) == t18)
                        check_sol19_hop4_hop5(W3, G3, &q3_cold[i], ctx, loc_sol19, ba);
                }
            }
        }
    }
}

static inline void compact_q2_r21(const u32 *q2_t17, const u32 *q2_G2, const q2_cold_t *q2_cold, int nq2,
                                  q3_hot_t *q3_hot, q3_cold_t *q3_cold, int *p_nq3,
                                  const compact_ctx21_22_t *ctx, int w_thresh) {
    const u32 a5 = ctx->a5, inv_a5 = ~a5, T18 = ctx->T18;
    int nq3 = *p_nq3;
    for (int i = 0; i < nq2; i++) {
        u32 t17 = q2_t17[i], G2 = q2_G2[i];
        if (ht_Q0_ker && __builtin_expect((t17 & KER_MASK) == 0u, 0)) {
            u32 idx2 = t17 >> KER_BITS;
            if (!((bm_Q0_ker[idx2 >> 5] >> (idx2 & 31u)) & 1u)) continue;
            for (u32 p2 = ht_Q0_ker[idx2]; p2 != 0u; p2 >>= 16) {
                u32 ulo2 = p2 & 0xffffu; if (!ulo2) continue;
                u32 W2 = rec_u_z(ulo2, ulo2 + t17), a3 = W2 + G2, M3 = a3 ^ inv_a5;
                int k_lo = __builtin_popcount(M3 & 0xffffu), k_hi = __builtin_popcount(M3 >> 16), nfl = 16 - k_lo;
                int neon = (k_hi <= NEON_MAX_KHI[nfl]);
                if (neon || (k_lo + k_hi <= w_thresh)) {
                    u32 a1 = q2_cold[i].a1, e1 = q2_cold[i].e1, a2 = q2_cold[i].a2, e2 = q2_cold[i].e2;
                    u32 e3 = a3 - q2_cold[i].sm2 + Am1_const, sm3 = S0(a3) + MAJ(a3, a2, a1);
                    u32 G3 = sm3 + S1(e3) + CH(e3, e2, e1) + C_G3_base, t18 = T18 + G3 - W2;
                    if (!neon && !(ht_Q0_ker && (t18 & KER_MASK) == 0u)) __builtin_prefetch(&H_pack4[t18 >> 2], 0, 0);
                    q3_hot[nq3]  = (q3_hot_t){t18, G3, M3, (u16)((~M3) & 0xffffu), (u8)nfl, (u8)neon};
                    q3_cold[nq3] = (q3_cold_t){a1, a2, a3, e2, e3, A0_const - sm3, a5 - e1 - K[4]};
                    nq3++;
                }
            }
            continue;
        }
        for (u32 p2 = H_pack4[t17 >> 2]; p2 != 0u; p2 >>= 16) {
            u32 ulo2 = p2 & 0xffffu; if (!ulo2) continue;
            u32 v1 = (ulo2 + t17) ^ (ulo2 >> 7) ^ (ulo2 << 14) ^ (ulo2 >> 3);
            u32 sv1 = (v1 << 15) ^ (v1 << 11) ^ v1;
            if ((sv1 & 0x8000u) != 0u) continue;
            u32 b0 = sv1 & 0x10000u, b12 = ((v1 << 14) ^ (v1 << 3)) & 0x60000u;
            u32 T = b0 | b12 | ((v1 << 18) & 0xfff80000u), W2_l27 = ulo2 | T;
            u32 W2 = W2_l27 ^ (T << 11) ^ (b0 << 15);
            u32 s0h2 = ((W2_l27 >> 4) ^ (W2_l27 >> 15)) << 29;
            if ((((W2 + t17) ^ s0h2) & 0xe0000000u) != 0u) continue;
            u32 a3 = W2 + G2, M3 = a3 ^ inv_a5;
            int k_lo = __builtin_popcount(M3 & 0xffffu), k_hi = __builtin_popcount(M3 >> 16), nfl = 16 - k_lo;
            int neon = (k_hi <= NEON_MAX_KHI[nfl]);
            if (__builtin_expect(neon || (k_lo + k_hi <= w_thresh), 0) && Qf(W2) == t17) {
                u32 a1 = q2_cold[i].a1, e1 = q2_cold[i].e1, a2 = q2_cold[i].a2, e2 = q2_cold[i].e2;
                u32 e3 = a3 - q2_cold[i].sm2 + Am1_const, sm3 = S0(a3) + MAJ(a3, a2, a1);
                u32 G3 = sm3 + S1(e3) + CH(e3, e2, e1) + C_G3_base, t18 = T18 + G3 - W2;
                if (!neon && !(ht_Q0_ker && (t18 & KER_MASK) == 0u)) __builtin_prefetch(&H_pack4[t18 >> 2], 0, 0);
                q3_hot[nq3]  = (q3_hot_t){t18, G3, M3, (u16)((~M3) & 0xffffu), (u8)nfl, (u8)neon};
                q3_cold[nq3] = (q3_cold_t){a1, a2, a3, e2, e3, A0_const - sm3, a5 - e1 - K[4]};
                nq3++;
            }
        }
    }
    *p_nq3 = nq3;
}

static void *left_worker_carbon21(void *arg) {
    Left21Arg *ba = (Left21Arg *)arg;
    const compact_ctx21_22_t *rctx = ba->rctx;
    const int B = ba->b_coset, w_thresh = ba->w_thresh;
    const u32 total_blks = 8u * ba->n_blocks_per_thread;
    u64 loc_chains = 0, loc_sol19 = 0;

    CarbonScratch21 *ws = (CarbonScratch21 *)calloc(1, sizeof(CarbonScratch21));
    int n_q2[B_COSET_MAX] = {0}, n_q3[B_COSET_MAX] = {0};

    const uint32x4_t v_flo0 = vld1q_u32(&FLO_MAJ_SUB[0]), v_flo4 = vld1q_u32(&FLO_MAJ_SUB[4]);
    const uint32x4_t v_flo8 = vld1q_u32(&FLO_MAJ_SUB[8]), v_flo12 = vld1q_u32(&FLO_MAJ_SUB[12]);
    const uint32x4_t v_s0l0 = vld1q_u32(&S0_LO4[0]), v_s0l4 = vld1q_u32(&S0_LO4[4]);
    const uint32x4_t v_s0l8 = vld1q_u32(&S0_LO4[8]), v_s0l12 = vld1q_u32(&S0_LO4[12]);
    const uint32x4_t v_A0_c = vdupq_n_u32(A0_const), v_E0_c = vdupq_n_u32(E0_const), v_CG2_b = vdupq_n_u32(C_G2_base);
    const uint32x4_t v_m_b0 = vdupq_n_u32(0x10000u), v_m_b12 = vdupq_n_u32(0x60000u), v_m_hi13 = vdupq_n_u32(0xfff80000u);

    for (;;) {
        if (ba->deadline_sec > 0.0 && now_sec() >= ba->deadline_sec) break;
        u32 blk_base = atomic_fetch_add_explicit(ba->shared_blk_ctr, 32u, memory_order_relaxed);
        if (blk_base >= total_blks) break;
        u32 blk_end = blk_base + 32u; if (blk_end > total_blks) blk_end = total_blks;
        u32 stream_step = (blk_base * 3u) + (blk_base << 1);

        for (u32 blk = blk_base; blk < blk_end; blk++) {
            const int use_kb4 = (ht_Q0_kb4 != NULL);
            u32 a1_base = synth_a1_coset_base_kb(stream_step, ba->coset_idx, use_kb4 ? KER4_MASK : KER_MASK, &stream_step);
            u32 e1_base = a1_base + C_E1, s0_a1_base = S0(a1_base), s1_e1_base = S1(e1_base & 0x3e03ffffu);
            u32 e1_mid7_0 = (e1_base >> 18) & 0x7fu, e1_hi2_0 = e1_base >> 30;
            u32 f1_base_f = C_t16_folded - a1_base + (a1_base & MAJ1_XOR) + ((Em1_const ^ (e1_base & CH1_XOR)) & 0x3e03ffffu);

            uint32x4_t v_f1_b = vdupq_n_u32(f1_base_f), v_s0_b = vdupq_n_u32(s0_a1_base), v_s1_b = vdupq_n_u32(s1_e1_base);
            uint32x4_t v_m0  = vaddq_u32(v_f1_b, vaddq_u32(v_flo0,  vld1q_u32(&CH1_MID7[e1_mid7_0 + 0])));
            uint32x4_t v_m4  = vaddq_u32(v_f1_b, vaddq_u32(v_flo4,  vld1q_u32(&CH1_MID7[e1_mid7_0 + 4])));
            uint32x4_t v_m8  = vaddq_u32(v_f1_b, vaddq_u32(v_flo8,  vld1q_u32(&CH1_MID7[e1_mid7_0 + 8])));
            uint32x4_t v_m12 = vaddq_u32(v_f1_b, vaddq_u32(v_flo12, vld1q_u32(&CH1_MID7[e1_mid7_0 + 12])));
            uint32x4_t v_s0_l0 = veorq_u32(v_s0_b, v_s0l0), v_s0_l4 = veorq_u32(v_s0_b, v_s0l4);
            uint32x4_t v_s0_l8 = veorq_u32(v_s0_b, v_s0l8), v_s0_l12 = veorq_u32(v_s0_b, v_s0l12);
            uint32x4_t v_s1_l0 = veorq_u32(v_s1_b, vld1q_u32(&S1_MID7[e1_mid7_0 + 0]));
            uint32x4_t v_s1_l4 = veorq_u32(v_s1_b, vld1q_u32(&S1_MID7[e1_mid7_0 + 4]));
            uint32x4_t v_s1_l8 = veorq_u32(v_s1_b, vld1q_u32(&S1_MID7[e1_mid7_0 + 8]));
            uint32x4_t v_s1_l12 = veorq_u32(v_s1_b, vld1q_u32(&S1_MID7[e1_mid7_0 + 12]));

            u32 f1_arr[64];
            for (u32 f_hi = 0; f_hi < 4u; f_hi++) {
                u32 f_hi_sh = f_hi << 30, e1_hi = e1_base + f_hi_sh;
                u32 hi_delta = (f_hi_sh & MAJ1_XOR) - f_hi_sh + ((Em1_const ^ (e1_hi & CH1_XOR)) & 0xc0000000u);
                uint32x4_t v_hid = vdupq_n_u32(hi_delta), v_s0_hi = vdupq_n_u32(S0_HI2[f_hi]), v_s1_hi = vdupq_n_u32(S1_HI2[(e1_hi2_0 + f_hi) & 3u]);
                u32 *dst = &f1_arr[f_hi << 4];
                vst1q_u32(&dst[0],  vaddq_u32(vaddq_u32(v_m0,  v_hid), vaddq_u32(veorq_u32(v_s0_l0,  v_s0_hi), veorq_u32(v_s1_l0,  v_s1_hi))));
                vst1q_u32(&dst[4],  vaddq_u32(vaddq_u32(v_m4,  v_hid), vaddq_u32(veorq_u32(v_s0_l4,  v_s0_hi), veorq_u32(v_s1_l4,  v_s1_hi))));
                vst1q_u32(&dst[8],  vaddq_u32(vaddq_u32(v_m8,  v_hid), vaddq_u32(veorq_u32(v_s0_l8,  v_s0_hi), veorq_u32(v_s1_l8,  v_s1_hi))));
                vst1q_u32(&dst[12], vaddq_u32(vaddq_u32(v_m12, v_hid), vaddq_u32(veorq_u32(v_s0_l12, v_s0_hi), veorq_u32(v_s1_l12, v_s1_hi))));
            }
            loc_chains += 64ULL * (u64)B;

            int n_hits_c[B_COSET_MAX];
            if (use_kb4) {
                for (int c = 0; c < B; c++) {
                    u32 T16 = rctx[c].T16;
                    for (u32 f = 0; f < 64u; f++) { u32 t16 = T16 + f1_arr[f]; __builtin_prefetch(&ht_Q0_kb4[t16 >> KER4_BITS], 0, 0); ws->hit_t16[c][f] = t16; }
                    n_hits_c[c] = 64;
                }
            } else {
                for (int c = 0; c < B; c++) {
                    u32 T16 = rctx[c].T16; int nh = 0;
                    for (u32 f = 0; f < 64u; f++) {
                        u32 t16 = T16 + f1_arr[f], b = t16 >> KER_BITS;
                        if ((bm_Q0_ker[b >> 5] >> (b & 31u)) & 1u) {
                            __builtin_prefetch(&ht_Q0_ker[b], 0, 0);
                            ws->hit_t16[c][nh] = t16; ws->hit_f[c][nh] = (u8)f; nh++;
                        }
                    }
                    n_hits_c[c] = nh;
                }
            }

            for (int c = 0; c < B; c++) {
                if (n_q3[c] >= PF_BATCH) { drain_q3_r21(ws->q3_hot[c], ws->q3_cold[c], n_q3[c], &rctx[c], &loc_sol19, ba); n_q3[c] = 0; }
                if (n_q2[c] >= PF_BATCH) { compact_q2_r21(ws->q2_t17[c], ws->q2_G2[c], ws->q2_cold[c], n_q2[c], ws->q3_hot[c], ws->q3_cold[c], &n_q3[c], &rctx[c], w_thresh); n_q2[c] = 0; }

                u32 T16 = rctx[c].T16, T17 = rctx[c].T17, C_G1_m_T16 = C_W0 - T16;
                u32 hit_a1[64], hit_e1[64], hit_G1[64], hit_e2b[64];
                u16 w1_ulo[260]; u8 w1_h[260]; int n_w1 = 0;

                if (use_kb4) {
                    int ho = 0;
                    for (u32 f = 0; f < 64u; f++) {
                        u32 t16 = ws->hit_t16[c][f]; u16 ulo1 = ht_Q0_kb4[t16 >> KER4_BITS]; if (!ulo1) continue;
                        u32 f_lo = f & 15u, f_hi = f >> 4;
                        u32 a1 = a1_base | (f_lo << 18) | (f_hi << 30), e1 = e1_base + (f_lo << 18) + (f_hi << 30);
                        u32 sm1_v = (s0_a1_base ^ S0_HI2[f_hi] ^ S0_LO4[f_lo]) + (a1 & MAJ1_XOR);
                        ws->hit_t16[c][ho] = t16; hit_a1[ho] = a1; hit_e1[ho] = e1;
                        hit_G1[ho] = t16 + a1 + C_G1_m_T16; hit_e2b[ho] = Am2_folded - sm1_v;
                        w1_ulo[n_w1] = ulo1; w1_h[n_w1] = (u8)ho; n_w1++; ho++;
                    }
                } else {
                    for (int h = 0; h < n_hits_c[c]; h++) {
                        u32 t16 = ws->hit_t16[c][h], pack1 = ht_Q0_ker[t16 >> KER_BITS], f = ws->hit_f[c][h];
                        u32 f_lo = f & 15u, f_hi = f >> 4;
                        u32 a1 = a1_base | (f_lo << 18) | (f_hi << 30), e1 = e1_base + (f_lo << 18) + (f_hi << 30);
                        u32 sm1_v = (s0_a1_base ^ S0_HI2[f_hi] ^ S0_LO4[f_lo]) + (a1 & MAJ1_XOR);
                        hit_a1[h] = a1; hit_e1[h] = e1; hit_G1[h] = t16 + a1 + C_G1_m_T16; hit_e2b[h] = Am2_folded - sm1_v;
                        int ns = (pack1 >> 16) ? 2 : 1;
                        memcpy(&w1_ulo[n_w1], &pack1, 4); w1_h[n_w1] = (u8)h; w1_h[n_w1 + 1] = (u8)h; n_w1 += ns;
                    }
                }
                for (int p = 0; p < 4; p++) { w1_ulo[n_w1 + p] = 0; w1_h[n_w1 + p] = 0; }

                #define VROTR_N(v, n) vsriq_n_u32(vshlq_n_u32((v), 32 - (n)), (v), (n))
                #define VS0_N(v) veorq_u32(veorq_u32(VROTR_N((v), 2), VROTR_N((v), 13)), VROTR_N((v), 22))
                #define VS1_N(v) veorq_u32(veorq_u32(VROTR_N((v), 6), VROTR_N((v), 11)), VROTR_N((v), 25))
                uint32x4_t v_T17_v = vdupq_n_u32(T17);
                int nq2 = n_q2[c];
                for (int k = 0; k < n_w1; k += 4) {
                    u32 h0 = w1_h[k], h1 = w1_h[k + 1], h2 = w1_h[k + 2], h3 = w1_h[k + 3];
                    uint32x4_t v_ulo1 = vmovl_u16(vld1_u16(&w1_ulo[k]));
                    uint32x4_t v_t16 = {ws->hit_t16[c][h0], ws->hit_t16[c][h1], ws->hit_t16[c][h2], ws->hit_t16[c][h3]};
                    uint32x4_t v_G1  = {hit_G1[h0], hit_G1[h1], hit_G1[h2], hit_G1[h3]};
                    uint32x4_t v_e2b = {hit_e2b[h0], hit_e2b[h1], hit_e2b[h2], hit_e2b[h3]};
                    uint32x4_t v_a1  = {hit_a1[h0], hit_a1[h1], hit_a1[h2], hit_a1[h3]};
                    uint32x4_t v_e1  = {hit_e1[h0], hit_e1[h1], hit_e1[h2], hit_e1[h3]};
                    uint32x4_t v_v1  = veorq_u32(veorq_u32(vaddq_u32(v_ulo1, v_t16), vshrq_n_u32(v_ulo1, 7)),
                                                 veorq_u32(vshlq_n_u32(v_ulo1, 14), vshrq_n_u32(v_ulo1, 3)));
                    uint32x4_t v_b0  = vandq_u32(veorq_u32(veorq_u32(vshlq_n_u32(v_v1, 15), vshlq_n_u32(v_v1, 11)), v_v1), v_m_b0);
                    uint32x4_t v_b12 = vandq_u32(veorq_u32(vshlq_n_u32(v_v1, 14), vshlq_n_u32(v_v1, 3)), v_m_b12);
                    uint32x4_t v_T1  = vorrq_u32(vorrq_u32(v_b0, v_b12), vandq_u32(vshlq_n_u32(v_v1, 18), v_m_hi13));
                    uint32x4_t v_W1  = vorrq_u32(v_ulo1, veorq_u32(veorq_u32(v_T1, vshlq_n_u32(v_T1, 11)), vshlq_n_u32(v_b0, 15)));
                    uint32x4_t v_a2  = vaddq_u32(v_W1, v_G1), v_e2 = vaddq_u32(v_a2, v_e2b);
                    uint32x4_t v_sm2 = vaddq_u32(VS0_N(v_a2), vbslq_u32(veorq_u32(v_a1, v_A0_c), v_a2, v_a1));
                    uint32x4_t v_G2  = vaddq_u32(vaddq_u32(v_CG2_b, v_sm2), vaddq_u32(VS1_N(v_e2), vbslq_u32(v_e2, v_e1, v_E0_c)));
                    uint32x4_t v_t17 = vsubq_u32(vaddq_u32(v_T17_v, v_G2), v_W1);

                    u32 t17_a[4], G2_a[4], a2_a[4], e2_a[4], sm2_a[4];
                    vst1q_u32(t17_a, v_t17); vst1q_u32(G2_a, v_G2); vst1q_u32(a2_a, v_a2); vst1q_u32(e2_a, v_e2); vst1q_u32(sm2_a, v_sm2);
                    int lim = (k + 4 <= n_w1) ? 4 : (n_w1 - k);
                    for (int lane = 0; lane < lim; lane++) {
                        u32 t17 = t17_a[lane], h = w1_h[k + lane];
                        if (!use_kb4 && (t17 & KER_MASK) == 0u) __builtin_prefetch(&ht_Q0_ker[t17 >> KER_BITS], 0, 0);
                        else                                    __builtin_prefetch(&H_pack4[t17 >> 2], 0, 0);
                        ws->q2_t17[c][nq2] = t17; ws->q2_G2[c][nq2] = G2_a[lane];
                        ws->q2_cold[c][nq2] = (q2_cold_t){hit_a1[h], hit_e1[h], a2_a[lane], e2_a[lane], sm2_a[lane]};
                        nq2++;
                    }
                }
                #undef VROTR_N
                #undef VS0_N
                #undef VS1_N
                n_q2[c] = nq2;
            }
        }
    }
    for (int c = 0; c < B; c++) {
        if (n_q2[c] > 0) compact_q2_r21(ws->q2_t17[c], ws->q2_G2[c], ws->q2_cold[c], n_q2[c], ws->q3_hot[c], ws->q3_cold[c], &n_q3[c], &rctx[c], w_thresh);
        if (n_q3[c] > 0) drain_q3_r21(ws->q3_hot[c], ws->q3_cold[c], n_q3[c], &rctx[c], &loc_sol19, ba);
    }
    free(ws);
    atomic_fetch_add_explicit(ba->tot_chains, loc_chains, memory_order_relaxed);
    atomic_fetch_add_explicit(ba->tot_sol19,  loc_sol19,  memory_order_relaxed);
    return NULL;
}

typedef struct {
    const u32 *A_tmpl;
    int R;
    int target_b;
    _Atomic u32 *seed_ctr;
    _Atomic int *done;
    pthread_mutex_t *mtx;
    compact_ctx21_22_t (*buckets)[B_COSET_MAX];
    int *b_cnt;
    compact_ctx21_22_t *out_ctx;
    u32 *coset_out;
} Fill21Arg;

static void *fill_bucket_worker21(void *arg) {
    Fill21Arg *fa = (Fill21Arg *)arg;
    while (!atomic_load_explicit(fa->done, memory_order_relaxed)) {
        u32 idx = atomic_fetch_add_explicit(fa->seed_ctr, 1u, memory_order_relaxed);
        if (idx >= 1000000u) break;
        u32 a14, a13, a12;
        synth_seed_r21_r22(fa->A_tmpl, fa->R, 1, idx, &a14, &a13, &a12);
        compact_ctx21_22_t hits[32];
        int nh = eval_ctx_carbon_insulated(fa->A_tmpl, fa->R, a14, a13, a12, hits, 32, NULL);
        if (!nh) continue;
        pthread_mutex_lock(fa->mtx);
        if (!atomic_load_explicit(fa->done, memory_order_relaxed)) {
            for (int h = 0; h < nh; h++) {
                int b = (int)(hits[h].T16 & KER_MASK);
                if (fa->b_cnt[b] < fa->target_b) {
                    fa->buckets[b][fa->b_cnt[b]++] = hits[h];
                    if (fa->b_cnt[b] == fa->target_b) {
                        memcpy(fa->out_ctx, fa->buckets[b], (size_t)fa->target_b * sizeof(compact_ctx21_22_t));
                        *fa->coset_out = (u32)b;
                        atomic_store_explicit(fa->done, 1, memory_order_relaxed);
                        break;
                    }
                }
            }
        }
        pthread_mutex_unlock(fa->mtx);
    }
    return NULL;
}

static int fill_insulated_bucket(const u32 A_tmpl[36], int R, int target_b, compact_ctx21_22_t out_ctx[B_COSET_MAX], u32 *coset_out) {
    compact_ctx21_22_t buckets[32][B_COSET_MAX];
    int b_cnt[32] = {0};
    _Atomic u32 seed_ctr = 0;
    _Atomic int done = 0;
    pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    Fill21Arg fa = {A_tmpl, R, target_b, &seed_ctr, &done, &mtx, buckets, b_cnt, out_ctx, coset_out};
    pthread_t th[8];
    for (int t = 0; t < 8; t++) pthread_create(&th[t], NULL, fill_bucket_worker21, &fa);
    for (int t = 0; t < 8; t++) pthread_join(th[t], NULL);
    return atomic_load_explicit(&done, memory_order_relaxed);
}

int main(void) {
    printf("====================================================================================================\n");
    printf("  PASS 4: CARBON HIGH NEXT-GEN R = 21 & R = 22 PADDED SHA-256 ALGEBRAIC ENGINE & BENCHMARK SUITE\n");
    printf("====================================================================================================\n\n");

    init_gf2_inv_s0_and_luts();
    init_left_constants_and_hpack4();

    // Verify inv_S0 bijection on 10,000 random 32-bit words
    int inv_ok = 0;
    for (u32 i = 0; i < 10000u; i++) { u32 y = perm32(i + 0xabcdef01u); if (S0(inv_S0(y)) == y) inv_ok++; }
    if (inv_ok != 10000) { fprintf(stderr, "inv_S0 check failed\n"); return 1; }

    // ------------------------------------------------------------------------------------------------
    // [PART 1] CALIBRATED 8-CORE HOST REFERENCE RATES (SCALAR C & 4-WIDE NEON FOR SHA-21 & SHA-22)
    // ------------------------------------------------------------------------------------------------
    double sha21_sc = 0, sha21_ne = 0, sha22_sc = 0, sha22_ne = 0;
    calibrate_sha(21, &sha21_sc, &sha21_ne);
    calibrate_sha(22, &sha22_sc, &sha22_ne);
    printf("[PART 1] CALIBRATED 8-CORE APPLE SILICON SHA-21 & SHA-22 REFERENCE THROUGHPUT\n");
    printf("  * SHA-21 : Scalar C = %6.2f M/s (2^%.2f/s) | 4-Wide NEON = %6.2f M/s (2^%.2f/s)\n",
           sha21_sc / 1e6, log2(sha21_sc), sha21_ne / 1e6, log2(sha21_ne));
    printf("  * SHA-22 : Scalar C = %6.2f M/s (2^%.2f/s) | 4-Wide NEON = %6.2f M/s (2^%.2f/s)\n\n",
           sha22_sc / 1e6, log2(sha22_sc), sha22_ne / 1e6, log2(sha22_ne));

    // ------------------------------------------------------------------------------------------------
    // [PART 2] RIGHT-CONTEXT CARRY-SUDOKU ABLATION AT R = 21 & R = 22 (SIGMA0^-1 INSULATION + T37-39)
    // ------------------------------------------------------------------------------------------------
    printf("[PART 2] RIGHT-CONTEXT CARRY-SUDOKU ABLATION (8-CORE PARALLEL, FULL 256-BIT FIXED TARGET \"abc\")\n");
    u32 A21_abc[36], A22_abc[36];
    init_state_r(A21_abc, H_ABC, 21);
    init_state_r(A22_abc, H_ABC, 22);

    // Verify 100% cryptographic exactness of insulated contexts at R=21 and R=22
    compact_ctx21_22_t ctx_ver[16];
    u32 c21_b = 0, c22_b = 0;
    if (!fill_insulated_bucket(A21_abc, 21, 8, ctx_ver, &c21_b)) return 1;
    for (int i = 0; i < 8; i++) {
        u32 A[36]; memcpy(A, A21_abc, sizeof(A));
        u32 a14 = ctx_ver[i].a14, a13 = ctx_ver[i].a13, a12 = ctx_ver[i].a12;
        u32 a11 = 0xffffffffu - A[15 + OFF] + S0(a14) + MAJ(a14, a13, a12);
        u32 e14 = a14 - S0(a13) - MAJ(a13, a12, a11) + a11;
        if ((e14 & 0x01ffff00u) != 0u) { fprintf(stderr, "R=21 e14 insulation failed!\n"); return 1; }
    }
    if (!fill_insulated_bucket(A22_abc, 22, 8, ctx_ver, &c22_b)) return 1;
    for (int i = 0; i < 8; i++) {
        u32 A[36]; memcpy(A, A22_abc, sizeof(A));
        u32 a14 = ctx_ver[i].a14, a13 = ctx_ver[i].a13, a12 = ctx_ver[i].a12;
        u32 a11 = 0xffffffffu - A[15 + OFF] + S0(a14) + MAJ(a14, a13, a12);
        u32 e14 = a14 - S0(a13) - MAJ(a13, a12, a11) + a11;
        if (e14 != 0u) { fprintf(stderr, "R=22 e14==0 insulation failed!\n"); return 1; }
    }

    pthread_t th[8];
    const u32 N_CTX_SEEDS = 2000;
    double r21_ctx_ng = 0, r22_ctx_ng = 0;

    for (int r_idx = 0; r_idx < 2; r_idx++) {
        int R = (r_idx == 0) ? 21 : 22;
        const u32 *At = (R == 21) ? A21_abc : A22_abc;
        _Atomic u32 c_leg = 0, c_ng = 0;
        _Atomic u64 h_leg = 0, s22_leg = 0, h_ng = 0, s22_ng = 0;
        Ctx21BenchArg al = {At, R, 0, N_CTX_SEEDS, &c_leg, &h_leg, &s22_leg};
        Ctx21BenchArg an = {At, R, 1, N_CTX_SEEDS, &c_ng,  &h_ng,  &s22_ng};

        double t0 = now_sec();
        for (int t = 0; t < 8; t++) pthread_create(&th[t], NULL, ctx21_22_bench_worker, &al);
        for (int t = 0; t < 8; t++) pthread_join(th[t], NULL);
        double dt_l = now_sec() - t0;

        t0 = now_sec();
        for (int t = 0; t < 8; t++) pthread_create(&th[t], NULL, ctx21_22_bench_worker, &an);
        for (int t = 0; t < 8; t++) pthread_join(th[t], NULL);
        double dt_n = now_sec() - t0;

        double rate_l = (double)h_leg / dt_l, rate_n = (double)h_ng / dt_n;
        if (R == 21) r21_ctx_ng = rate_n;
        else         r22_ctx_ng = rate_n;

        printf("  * R=%d (2,000 seeds) : Legacy = %6.1f ctx/s (%llu hits, s22=%llu) -> Carbon S0^-1 = %7.1f ctx/s (%llu hits, s22=%llu) = %.2fx ctx/s (%.1fx fewer s22!)\n",
               R, rate_l, (unsigned long long)h_leg, (unsigned long long)s22_leg,
               rate_n, (unsigned long long)h_ng, (unsigned long long)s22_ng,
               rate_n / rate_l, (double)s22_leg / (double)(s22_ng ? s22_ng : 1));
    }
    printf("\n");

    // ------------------------------------------------------------------------------------------------
    // [PART 3] LEFT-STREAM ABLATION (KB=4 512MB vs KB=5 528MB + NEYMAN-PEARSON W_THRESH & MSHR B=4/8)
    // ------------------------------------------------------------------------------------------------
    printf("[PART 3] LEFT-TO-RIGHT sol19 + HOP-4/5 STREAMING & FULL 256/256-BIT FIXED-TARGET COMPLEXITY\n");
    compact_ctx21_22_t rctx21[B_COSET_MAX];
    u32 coset21 = 0;
    fill_insulated_bucket(A21_abc, 21, 8, rctx21, &coset21);

    // Stage A: Carbon-KB4 Direct u16 (512 MB anon RAM, 0-CAS, 0 MB bitmap/cosets)
    SliceArg sargs[8];
    double tb0 = now_sec();
    ht_Q0_kb4 = (u16 *)calloc(LUT4_BUCKETS, sizeof(u16));
    for (int t = 0; t < 8; t++) { sargs[t].tid = t; pthread_create(&th[t], NULL, build_kb4_slice, &sargs[t]); }
    for (int t = 0; t < 8; t++) pthread_join(th[t], NULL);
    double dt_kb4_build = now_sec() - tb0;

    // Warmup + Timed KB=4 B=4 run
    for (int pass = 0; pass < 2; pass++) {
        _Atomic u64 ch = 0, sol = 0; _Atomic u32 ctr = 0;
        Left21Arg la[8];
        u32 nblks = (pass == 0) ? 1024u : 8192u;
        double t0 = now_sec();
        for (int t = 0; t < 8; t++) {
            la[t] = (Left21Arg){t, 21, 4, 13, nblks, coset21 & KER4_MASK, rctx21, A21_abc, H_ABC, &ch, &sol, &ctr, NULL, NULL, 0.0};
            pthread_create(&th[t], NULL, left_worker_carbon21, &la[t]);
        }
        for (int t = 0; t < 8; t++) pthread_join(th[t], NULL);
        if (pass == 1) {
            double dt = now_sec() - t0;
            printf("  * Carbon-KB4 Direct (512 MB anon, 0-CAS %.3fs, B=4, w<=13) : %llu sol19s in %.4fs -> %7.1f sol19/s\n",
                   dt_kb4_build, (unsigned long long)sol, dt, (double)sol / dt);
        }
    }
    free(ht_Q0_kb4); ht_Q0_kb4 = NULL; // Free BEFORE allocating KB=5 so peak anon RAM stays <= 528 MB (0 swap)!

    // Stage B: Carbon-KB5 2-Slot u32 + 16 MB P-Core L2 Bitmap (528 MB anon RAM, 0-CAS, 0 swap!)
    tb0 = now_sec();
    ht_Q0_ker = (u32 *)calloc(LUT_BUCKETS, sizeof(u32));
    bm_Q0_ker = (u32 *)calloc(BM_WORDS, sizeof(u32));
    for (int t = 0; t < 8; t++) { sargs[t].tid = t; pthread_create(&th[t], NULL, build_kb5_slice, &sargs[t]); }
    for (int t = 0; t < 8; t++) pthread_join(th[t], NULL);
    double dt_kb5_build = now_sec() - tb0;
    printf("  * Carbon-KB5 2-Slot + L2 BM Build (528 MB anon, 0-CAS)      : built in %.3fs (0 MB swap!)\n", dt_kb5_build);

    // Warmup
    {
        _Atomic u64 ch = 0, sol = 0; _Atomic u32 ctr = 0; Left21Arg la[8];
        for (int t = 0; t < 8; t++) {
            la[t] = (Left21Arg){t, 21, 4, 13, 1024u, coset21, rctx21, A21_abc, H_ABC, &ch, &sol, &ctr, NULL, NULL, 0.0};
            pthread_create(&th[t], NULL, left_worker_carbon21, &la[t]);
        }
        for (int t = 0; t < 8; t++) pthread_join(th[t], NULL);
    }

    int cfg_B[4] = {8, 4, 4, 4};
    int cfg_W[4] = {13, 13, 14, 15};
    double best_sol_s = 0.0, best_sol_per_ch = 0.0;
    int best_B = 4, best_W = 13;

    for (int m = 0; m < 4; m++) {
        _Atomic u64 ch = 0, sol = 0; _Atomic u32 ctr = 0; Left21Arg la[8];
        double t0 = now_sec();
        for (int t = 0; t < 8; t++) {
            la[t] = (Left21Arg){t, 21, cfg_B[m], cfg_W[m], 8192u, coset21, rctx21, A21_abc, H_ABC, &ch, &sol, &ctr, NULL, NULL, 0.0};
            pthread_create(&th[t], NULL, left_worker_carbon21, &la[t]);
        }
        for (int t = 0; t < 8; t++) pthread_join(th[t], NULL);
        double dt = now_sec() - t0;
        double sol_s = (double)sol / dt;
        printf("  * Carbon-KB5 (B=%d, w<=%2d, %4.1fM chains)                     : %llu sol19s in %.4fs -> %7.1f sol19/s\n",
               cfg_B[m], cfg_W[m], (double)ch / 1e6, (unsigned long long)sol, dt, sol_s);
        if (sol_s > best_sol_s) {
            best_sol_s = sol_s;
            best_sol_per_ch = (double)sol / (double)ch;
            best_B = cfg_B[m];
            best_W = cfg_W[m];
        }
    }

    // End-to-End Effective sol19/s and Full 256/256-Bit Fixed-Target log2(SHA-R-eq) at R=21 & R=22
    const double LEG_COLD_SOL_S = 70.7, LEG_WARM_SOL_S = 186.9;
    double sol19_per_ctx = 16777216.0 * best_sol_per_ch;
    double eff_sol_s_r21 = 1.0 / ((1.0 / best_sol_s) + (1.0 / (sol19_per_ctx * r21_ctx_ng)));
    double eff_sol_s_r22 = 1.0 / ((1.0 / best_sol_s) + (1.0 / (sol19_per_ctx * r22_ctx_ng)));

    const double TWO64 = ldexp(1.0, 64), TWO96 = ldexp(1.0, 96);
    double r21_leg_cold_sc = log2((TWO64 / LEG_COLD_SOL_S) * sha21_sc);
    double r21_leg_warm_sc = log2((TWO64 / LEG_WARM_SOL_S) * sha21_sc);
    double r21_carbon_sc   = log2((TWO64 / eff_sol_s_r21)  * sha21_sc);
    double r21_carbon_ne   = log2((TWO64 / eff_sol_s_r21)  * sha21_ne);

    double r22_leg_cold_sc = log2((TWO96 / LEG_COLD_SOL_S) * sha22_sc);
    double r22_leg_warm_sc = log2((TWO96 / LEG_WARM_SOL_S) * sha22_sc);
    double r22_carbon_sc   = log2((TWO96 / eff_sol_s_r22)  * sha22_sc);
    double r22_carbon_ne   = log2((TWO96 / eff_sol_s_r22)  * sha22_ne);

    printf("\n  +--------------------------------------------------------------------------------------------------+\n");
    printf("  | FULL 1-BLOCK 256/256-BIT FIXED-TARGET COMPLEXITY (STRICT FIPS 180-4 PADDED, 8-CORE APPLE SILICON)|\n");
    printf("  +--------------------------------------------------------------------------------------------------+\n");
    printf("  | R = 21 Legacy (Cold / Warm) : 70.7 / 186.9 eff sol19/s -> log2(SHA21-sc) = %.2f / %.2f          |\n", r21_leg_cold_sc, r21_leg_warm_sc);
    printf("  | R = 21 Carbon Peak (B=%d,w=%d): %7.1f eff sol19/s (%.1fx-%.1fx) -> log2(SHA21-sc) = %.2f (NEON %.2f) |\n",
           best_B, best_W, eff_sol_s_r21, eff_sol_s_r21 / LEG_WARM_SOL_S, eff_sol_s_r21 / LEG_COLD_SOL_S, r21_carbon_sc, r21_carbon_ne);
    printf("  | R = 22 Legacy (Cold / Warm) : 70.7 / 186.9 eff sol19/s -> log2(SHA22-sc) = %.2f / %.2f         |\n", r22_leg_cold_sc, r22_leg_warm_sc);
    printf("  | R = 22 Carbon Peak (B=%d,w=%d): %7.1f eff sol19/s (%.1fx-%.1fx) -> log2(SHA22-sc) = %.2f (NEON %.2f)|\n",
           best_B, best_W, eff_sol_s_r22, eff_sol_s_r22 / LEG_WARM_SOL_S, eff_sol_s_r22 / LEG_COLD_SOL_S, r22_carbon_sc, r22_carbon_ne);
    printf("  +--------------------------------------------------------------------------------------------------+\n\n");

    // ------------------------------------------------------------------------------------------------
    // [PART 4] LIVE CRYPTOGRAPHIC WITNESS RECOVERY & 2-FOR-1 CAUSAL-HORIZON / ctz(rho19) HARVESTER
    // ------------------------------------------------------------------------------------------------
    printf("[PART 4] LIVE CRYPTOGRAPHIC WITNESS RECOVERY (EXPLICIT FIXED-DIGEST-BIT ACCOUNTING)\n");
    const char *t_names[3] = {"0^256", "\"abc\"", "\"hello\""};
    const u32  *t_digs[3]  = {H_ZERO, H_ABC, H_HELLO};
    int live_pass = 0;

    // 4A. Sub-second Live Causal-Horizon Witness Recovery on 0^256, "abc", "hello" at R=21 (128/256b) & R=22 (64/256b)
    for (int r_idx = 0; r_idx < 2; r_idx++) {
        int R = (r_idx == 0) ? 21 : 22;
        for (int ti = 0; ti < 3; ti++) {
            double t0 = now_sec();
            u32 At[36]; init_state_r(At, t_digs[ti], R);
            compact_ctx21_22_t c_loc[B_COSET_MAX]; u32 cos_loc = 0;
            fill_insulated_bucket(At, R, 4, c_loc, &cos_loc);
            pthread_mutex_t hmtx = PTHREAD_MUTEX_INITIALIZER;
            HarvestState hs; memset(&hs, 0, sizeof(hs));
            _Atomic u64 ch = 0, sol = 0; _Atomic u32 ctr = 0; Left21Arg la[8];
            for (int t = 0; t < 8; t++) {
                la[t] = (Left21Arg){t, R, 4, best_W, 128u, cos_loc, c_loc, At, t_digs[ti], &ch, &sol, &ctr, &hmtx, &hs, 0.0};
                pthread_create(&th[t], NULL, left_worker_carbon21, &la[t]);
            }
            for (int t = 0; t < 8; t++) pthread_join(th[t], NULL);
            double dt = now_sec() - t0;

            u32 H_chk[8];
            compress_steps(IV, hs.best_W, R, H_chk);
            int pad_ok = ((hs.best_W[13] & 0xffu) == 0x80u) && (hs.best_W[14] == 0u) && (hs.best_W[15] == PADLEN_BITS);
            int hor_ok = (R == 21)
                ? (H_chk[2] == t_digs[ti][2] && H_chk[3] == t_digs[ti][3] && H_chk[6] == t_digs[ti][6] && H_chk[7] == t_digs[ti][7])
                : (H_chk[3] == t_digs[ti][3] && H_chk[7] == t_digs[ti][7]);
            if (hs.found && pad_ok && hor_ok) live_pass++;

            printf("  * R=%d %-8s (0-eq Horizon: %3d/256 exact fixed target bits) : %.4fs | %3llu sol19s | max_ctz(rho19)=%2d | total_match=%3d/256b | %s\n",
                   R, t_names[ti], (R == 21) ? 128 : 64, dt, (unsigned long long)sol,
                   hs.max_ctz_rho19, hs.max_match_bits, (hs.found && pad_ok && hor_ok) ? "PASS" : "FAIL");
        }
    }

    // 4B. Live 0.25s 8-Core Streaming 2-for-1 ctz(rho19) Prefix-Zeroed Harvester on Unmodified "abc" (R=21 & R=22)
    for (int r_idx = 0; r_idx < 2; r_idx++) {
        int R = (r_idx == 0) ? 21 : 22;
        u32 At[36]; init_state_r(At, H_ABC, R);
        compact_ctx21_22_t c_loc[B_COSET_MAX]; u32 cos_loc = 0;
        fill_insulated_bucket(At, R, 4, c_loc, &cos_loc);
        pthread_mutex_t hmtx = PTHREAD_MUTEX_INITIALIZER;
        HarvestState hs; memset(&hs, 0, sizeof(hs));
        _Atomic u64 ch = 0, sol = 0; _Atomic u32 ctr = 0; Left21Arg la[8];
        double t0 = now_sec(), deadline = t0 + 0.25;
        for (int t = 0; t < 8; t++) {
            la[t] = (Left21Arg){t, R, 4, best_W, 65536u, cos_loc, c_loc, At, H_ABC, &ch, &sol, &ctr, &hmtx, &hs, deadline};
            pthread_create(&th[t], NULL, left_worker_carbon21, &la[t]);
        }
        for (int t = 0; t < 8; t++) pthread_join(th[t], NULL);
        double dt = now_sec() - t0;

        u32 H_chk[8];
        compress_steps(IV, hs.best_W, R, H_chk);
        int k = hs.max_ctz_rho19;
        u32 mask_k = (k >= 32) ? 0xffffffffu : ((1u << k) - 1u);
        // Verify the 2-for-1 low-bit theorem: ctz(rho19) == k locks k low bits of BOTH H[1],H[5] (R=21) or H[2],H[6] (R=22)!
        int two_for_one_ok = (R == 21)
            ? ((((H_chk[1] ^ H_ABC[1]) & mask_k) == 0u) && (((H_chk[5] ^ H_ABC[5]) & mask_k) == 0u))
            : ((((H_chk[2] ^ H_ABC[2]) & mask_k) == 0u) && (((H_chk[6] ^ H_ABC[6]) & mask_k) == 0u));
        int pad_ok = ((hs.best_W[13] & 0xffu) == 0x80u) && (hs.best_W[14] == 0u) && (hs.best_W[15] == PADLEN_BITS);
        if (hs.found && two_for_one_ok && pad_ok) live_pass++;

        int locked_struct_bits = ((R == 21) ? 128 : 64) + 2 * k;
        printf("\n  * [LIVE %.2fs STREAMING HARVEST ON \"abc\" AT R = %d]\n", dt, R);
        printf("    -> Evaluated %llu sol19s (%.1fM chains) | Best rho19 = 0x%08x (ctz = %d -> +%d 2-for-1 low digest bits!)\n",
               (unsigned long long)sol, (double)ch / 1e6, hs.best_rho19, k, 2 * k);
        printf("    -> Deterministic Locked Target Bits : %d / 256 bits | Total Bitwise Digest Match vs H_ABC : %d / 256 bits\n",
               locked_struct_bits, hs.max_match_bits);
        printf("    -> Witness W[0..7]   : %08x %08x %08x %08x %08x %08x %08x %08x\n",
               hs.best_W[0], hs.best_W[1], hs.best_W[2], hs.best_W[3], hs.best_W[4], hs.best_W[5], hs.best_W[6], hs.best_W[7]);
        printf("    -> Witness W[8..15]  : %08x %08x %08x %08x %08x %08x %08x %08x\n",
               hs.best_W[8], hs.best_W[9], hs.best_W[10], hs.best_W[11], hs.best_W[12], hs.best_W[13], hs.best_W[14], hs.best_W[15]);
        printf("    -> Output H_%d[0..7] : %08x %08x %08x %08x %08x %08x %08x %08x (%s)\n",
               R, H_chk[0], H_chk[1], H_chk[2], H_chk[3], H_chk[4], H_chk[5], H_chk[6], H_chk[7],
               (two_for_one_ok && pad_ok) ? "2-FOR-1 & FIPS-PAD VERIFIED" : "FAIL");
    }

    free(ht_Q0_ker); free(bm_Q0_ker);
    if (live_pass != 8) { fprintf(stderr, "Live verification failed (%d/8)\n", live_pass); return 1; }
    printf("\nALL 8 / 8 LIVE CRYPTOGRAPHIC VERIFICATIONS & INVARIANT CHECKS PASSED (100%% EXACT)!\n");
    return 0;
}
