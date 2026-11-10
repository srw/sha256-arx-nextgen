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

## Pass 2 — `R = 20` Next-Gen Carbon Fusion (`IN PROGRESS`)

### Pre-Fusion Baseline (`exp_r20_distant_theories.c`, Parts 1–4 Verified)
- **Right-Context Carry-Sudoku (`Part 2`):** `1,155.8 ctx/s` on 8 cores (`3.01x` over Legacy `384.6 ctx/s`, `4.33x` over paper `266.8 ctx/s`), `0.00%` hit loss (`38/38` exact hits across 500 seeds).
- **Left-to-Right `sol19` + `W4` Streaming (`Part 3`):** `2,543–3,160 sol19/s` on 8 cores (`3.35x–11.30x` over Legacy `solve20_padded_fast.c`), currently using `KER_BITS=5` CAS table build (`~8.0s`), 32 MB `a1_cosets`, and 4 GB `H_pack4.bin` at Hop 3 (`47.3%` $Q_0^{-1}(t_{18})$ coverage).
- **Target of Pass 2:** Transplant `KB=4` 0-CAS slice builder (`ht_dense`), zero-table 9-bit $\Sigma_1$ blind-spot `{a1[22..24], a1[16]}` coset lock (`2x` faster co-residue bucket fill, `0 MB` `a1_cosets`), 4-wide NEON Hop-3 active subspace inversion (`100%` $Q_0^{-1}(t_{18})$ coverage -> `~2x` higher `sol19` yield per Hop-2 survivor!), and calibrated 8-core `SHA20-eq` $\log_2$ complexity measurement.

---

## Pass 3 — `R = 21` & `R = 22` First-Principles Mathematical Obstruction Diagnosis (`QUEUED`)
- Pending completion of Pass 2.

---

## Pass 4 — `R = 21` & `R = 22` Next-Gen Algebraic Attack Implementation & Benchmark (`QUEUED`)
- Pending completion of Pass 3.
