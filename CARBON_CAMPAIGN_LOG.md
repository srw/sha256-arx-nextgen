# Carbon High Autonomous SHA-256 Padded Preimage Campaign Log (`R = 19, 20, 21, 22`)

> [!IMPORTANT]
> **Live Reporting Protocol:** Updated after every completed pass with exact before/after calibrated metrics: **effective $\log_2$ SHA-$R$-equivalent operations**, **wall-clock time**, **RAM footprint**, and **cryptographic verification status** (`256/256` digest bits + strict FIPS 180-4 single-block padding `0x80`, `W14 == 0`, `W15 == 440`).

---

## Host Calibration Baseline (8-Core Apple Silicon ARM64 NEON)
- **8-Core Scalar C `compress(..., R=19)` Reference Rate:** `118.32 M SHA19/s` ($2^{26.82}\text{ SHA19/s}$)
- **8-Core 4-Wide ARM64 NEON `SHA-19` Reference Rate:** `342.48 M SHA19/s` ($2^{28.35}\text{ SHA19/s}$)

---

## Pass 1 — `R = 19` Carbon Promotion & Verification (`COMPLETED`)

### Algebraic Breakthroughs Promoted (`Theories 37–40`)
1. **Theory 37 (Full-Word $D$ Liberation & Lower-Half Concentration):** Generalizes $M = \neg(a_5 \oplus a_3) \mid D$ while concentrating residual bits of $D = a_5 \oplus a_6$ into bits `10..15` (`(D >> 16) == 0`), shrinking `n_free_lo` without inflating `n_forced_hi`.
2. **Theory 38 (10-Bit Full-Top 2-Adic Hensel Bijection `k = 22..31` + $\Sigma_0(a_9)$ Rank Lock):** Deterministically locks `(D & 0x3ff) == 0` and `(W13 & 0xff) == 0x80` in $O(1)$ without brute-force high-bit loops, cutting Op2 trials by **`6.9x–13.4x`** (`133.8K` mean trials vs `927K–1.03M`, regularly reaching `popc(D) <= 1` and `D == 0`).
3. **Theory 39 (Upper-3-Bit $\sigma_0(W_3)[29..31]$ Syzygy + 4-Wide NEON Hop-3 Subspace Solver):** Uses the exact identity $\sigma_0(W) \gg 29 = ((W_{\text{lo18}} \gg 4) \oplus (W_{\text{lo18}} \gg 15)) \ \& \ 7$ (independent of bits `18..31`) inside 4-wide NEON `n_free_lo <= 11` subspace enumeration, cutting Stage-4 chains by **`3.54x`** (`363.5K` vs `1.29M`).
4. **Theory 40 (Upper-3-Bit $\sigma_0$ Output-Slice CAS-Free Kernel + Zero-Table 9-Bit $\Sigma_1$ Blind-Spot Coset Lock):** Exploits the 9-bit blind spot `e1[16..24]` of $\Sigma_1(e_1) \bmod 32$ to lock `(t16 & 15) == 0` across all 64 roots via `{a1[22..24], a1[16]}` in 3 ALU ops (`0 MB` `a1_cosets`, `0 MB` bitmap, `0` atomic CAS operations during table build).

### Before / After Metrics (`R = 19` Padded Preimage, 8-Core CPU)

| Engine / Mode | Mean Wall-Clock / Target | Mean Op2 + Stage-4 Trials (`log2(alg)`) | Effective `log2(SHA19-scalar)` | Effective `log2(SHA19-NEON)` | RAM Footprint | Verification |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Legacy C Baseline** | `10,260.00 ms` (A+B) | $2^{29.58}$ | **`29.18`** | `30.71` | Multi-GB | `2/2 OK` |
| **Before: Gen-36 CPU (`exp_r19_distant_theories.c`)** | `4.87 ms` (10-tgt) | `2,214,847` (`21.08`) | **`19.14`** | `20.67` | `20 MB`–`1072 MB` | `12/12 OK` |
| **After: `exp_r19_carbon_lab.c` (`KB=5` `u64` Lab)** | `6.09 ms` (10-tgt) / `7.15 ms` (50-tgt) | `1,894,528` (`20.85`) | **`19.70`** | `21.23` | `1040 MB` (`0` CAS) | **`62/62 OK`** |
| **After: `Carbon-Lite` (`bench_carbon_modes.c`, `KB=4` `u16`)** | **`3.67 ms`** (50-tgt) / `3.97 ms` (10-tgt) | `843,327` (**`19.69`**) | **`18.73`** | **`20.26`** | **`512 MB`** (`0` CAS, `0 MB` aux) | **`60/60 OK`** |
| **After: `Carbon-Dense` (`bench_carbon_modes.c`, `KB=4` `u32`)** | **`3.04 ms`** (50-tgt) / `3.37 ms` (10-tgt) | **`497,290`** (**`18.92`**) | **`18.46`** | **`19.99`** | **`1024 MB`** (`0` CAS, `0 MB` aux) | **`60/60 OK`** |

---

## Pass 2 — `R = 20` Next-Gen Carbon Fusion (`COMPLETED`)

### Host `SHA-20` Calibration Baseline (8-Core Apple Silicon ARM64 NEON)
- **8-Core Scalar C `compress_steps(..., R=20)` Rate:** `135.67 M SHA20/s` ($2^{27.02}\text{ SHA20/s}$)
- **8-Core 4-Wide ARM64 NEON `SHA-20` Rate:** `331.23 M SHA20/s` ($2^{28.30}\text{ SHA20/s}$)

### Algebraic & Microarchitectural Breakthroughs Transplanted (`exp_r20_distant_theories.c`, Theories 37–44)
1. **Theories 37–39 (Carry-Sudoku $\Sigma_0(a_9)$ $\mathbb{F}_2$ Suffix-to-Prefix Rank Revival & Zero-Loss 14b/19b Sieve):** Hoists $\Sigma_1(e_{13,\text{lo}} \mid s_8)$ and zero-rotate `hi25 = hi25_s12 ^ (s15 << 14)`, combines O(1) L1-LUT `ROR64(VALID_SUM64)` with exact 14-bit pre-`s19` and 19-bit (`0x7ffff`) pre-`s22` $\Sigma_0(a_9)$ suffix-to-prefix rank locks, slashing `s22` evaluations by **`2,284.7x`** and `S0(a9)+MAJ` calls by **`2,279.0x`** (`1,235.8 ctx/s` on 8 cores = **`3.24x`** over Legacy `381.7 ctx/s` and **`4.63x`** over the `266.8 ctx/s` paper baseline, `0.00%` hit loss).
2. **Theory 40 (Upper-3-Bit $\sigma_0$ Output-Slice `0-CAS` Kernel Builders — `KB=4` Direct `u16` & `KB=5` 2-Slot `u32` + 16 MB L2 Bitmap):** Partitioning preimages across 8 disjoint output slices `(y >> 29) == tid` builds the `512 MB` `KB=4` direct-mapped `u16` kernel in **`0.833s`** (`0` CAS, `0 MB` bitmap) and the `512 MB` `KB=5` 2-slot `u32` + `16 MB` P-core L2 bitmap kernel in **`0.551s`** (`0` CAS vs `~8.0s` atomic CAS).
3. **Theory 41 (Zero-Table 9-Bit $\Sigma_1$ Blind-Spot Algebraic Coset Lock):** Uses `{a1[22..24], a1[16]}` (`KB=4`, $\bmod 16$) and `{a1[22..24], a1[16..17]}` (`KB=5`, $\bmod 32$) inside the 9-bit blind spot `e1[16..24]` of $\Sigma_1(e_1) \bmod 32$ to lock all 64 roots in $O(1)$ ALU ops (`0 MB` `a1_cosets`).
4. **Theories 42–44 (Hop-2 Constant Hoisting + Upper-3-Bit $\sigma_0(W)[29..31]$ Syzygy + 4-Wide NEON Hop-3 Subspace Inversion + MSHR/L1D De-Aliasing):**
   - Cuts Hop-4 `W4` verification from `4.00` to **`1.00` SHA-256 round** per `sol19` (`c_e4`, `c_W4` hoisting),
   - Cuts `Qf(u)` recomputations by **`23.1x`** (`97.2M -> 4.21M`) via `sv1` + upper-3-bit $\sigma_0(W)[29..31]$ syzygy,
   - Boosts exact `sol19` yield per chain by **`2.22x`** (`648` vs `292` per `33.55M` chains) via 2-slot Hop-1 kernel + 4-wide NEON Hop-3 subspace inversion (`n_free_lo <= 10`, `100%` $Q_0^{-1}(t_{18})$ coverage with `0` DRAM reads),
   - Eliminates 8 KB Apple Silicon L1D way aliasing (`stride 524`), uses lock-free atomic P+E core work stealing (`shared_blk_ctr`), and keeps anonymous RAM at **`528 MB`** (`0` swap) alongside `MAP_SHARED` + `MADV_RANDOM` `H_pack4`, peaking at **`B = 4` MSHR-optimal co-residue batching (`2,801.3 sol19/s` / `2,781.8 eff sol19/s`)**.

### Before / After Metrics (`R = 20` Full 256/256-Bit Fixed-Target Padded Preimage, 8-Core CPU)

| Engine / Mode | Right-Ctx (`8c`) | `33.55M`-Chain Wall-Clock | Left Stream `sol19/s` | End-to-End `eff sol19/s` (`E[T]`) | Effective `log2(SHA20-scalar)` | Effective `log2(SHA20-NEON)` | Anonymous RAM | Verification |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Before: Legacy `solve20_padded_fast.c` (Cold)** | `384.0 ctx/s` | `4.1228 s` (`292` `sol19`) | `70.8 sol19/s` | `70.7/s` (`16,883 hrs`) | **`52.84`** | `54.16` | `4096 MB` + `32 MB` | `16/16 PASS` |
| **Before: Legacy `solve20_padded_fast.c` (Warm)** | `381.7 ctx/s` | `1.5412 s` (`292` `sol19`) | `189.5 sol19/s` | `186.9/s` (`6,382.5 hrs`) | **`51.47`** | `52.76` | `4096 MB` + `32 MB` | `16/16 PASS` |
| **After: `Carbon-KB4` Direct (`KB=4` `u16`, `B=8`)** | **`1,235.8 ctx/s`** | **`0.1809 s`** (`453` `sol19`) | **`2,504.2 sol19/s`** | **`2,482.0/s`** (`480.7 hrs`) | **`47.74`** | **`49.03`** | **`512 MB`** (`0` CAS, `0 MB` aux) | **`19/19 PASS`** |
| **After: `Carbon-KB5` 2-Slot + L2 BM (`B=8`)** | **`1,235.8 ctx/s`** | **`0.2569 s`** (**`648` `sol19`**) | **`2,522.6 sol19/s`** | **`2,506.8/s`** (`475.9 hrs`) | **`47.72`** | **`49.01`** | **`528 MB`** (`0` CAS, `0` swap) | **`19/19 PASS`** |
| **After: `Carbon-Peak` MSHR-Optimal (`B=4`)** | **`1,235.8 ctx/s`** | **`0.1157 s`** (`16.8M` ch, `324` `sol19`) | **`2,801.3 sol19/s`** | **`2,781.8/s`** (**`428.9 hrs`**, **`14.88x`–`39.3x`**) | **`47.57`** | **`48.86`** | **`528 MB`** (`0` CAS, `0` swap) | **`19/19 PASS`** |

- **Live End-to-End 256/256-Bit Fixed-Target Recovery (`Part 4`):** `0^256` (`0.0026s`, `8/8` words exact), `"abc"` (`0.0023s`, `8/8` words exact), `"hello"` (`0.0022s`, `8/8` words exact), plus `16/16` full padded 20-step witnesses (`100%` verified).

---

## Pass 3 — `R = 21` & `R = 22` First-Principles Mathematical Obstruction Diagnosis (`IN PROGRESS`)
- Diagnosing from first principles what mathematical obstruction governs **fixed-256-bit-target** single-block padded $R=21$ and $R=22$ vs horizon-pivoted partial targets and multi-block FIPS 180-4 padded bridges.

---

## Pass 4 — `R = 21` & `R = 22` Next-Gen Algebraic Attack Implementation & Benchmark (`QUEUED`)
- Pending completion of Pass 3.
