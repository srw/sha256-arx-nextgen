# `sha256-arx-nextgen` — Next-Generation Symbolic & Heterogeneous ARX Preimage Engine (`R = 19, 20, 21, 22`)

Next-generation experimental mathematics, symbolic ARX analysis, active algebraic multi-round inversion (`R = 19, 20, 21, 22`), and heterogeneous Apple Silicon (8-core ARM64 NEON CPU + 10-core Metal GPU) preimage solver suite for step-reduced FIPS 180-4 padded SHA-256.

---

## 1. Performance Leap Over Legacy Baseline

### A. `R=19` FIPS 180-4 Padded SHA-256

| Engine / Mode | Target A + Target B Pure Search | Speedup vs `10.260s` Legacy Baseline | 10-Target Stress Suite Total (`Mean / Target`) | Table Footprint | Verification |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Legacy Baseline** | `10.260s` | `1.0x` | `~102.6s` (`~10.26s / target`) | Multi-GB | `2/2 PASS` |
| **`exp_r19_distant_theories.c` (8-Core ARM64 NEON CPU)** | **`0.0070s`** (`0.0055s` + `0.0015s`) | **`1,468.9x`** (`6,556.6x` on Target B) | **`0.0501s`** (`5.0 ms / target`; min **`0.5 ms`**) | **20 MB SLC-resident** (`16 MB` bitmap + `4 MB` hash) | `10/10 PASS` (`0` errors) |
| **`exp_r19_metal_gpu.m` — Mode 1 (8-Core CPU Only)** | **`0.0136s`** (`0.0072s` + `0.0064s`) | **`751.7x`** | **`0.0429s`** (`4.3 ms / target`) | **20 MB SLC-resident** | `10/10 PASS` |
| **`exp_r19_metal_gpu.m` — Mode 2 (NEON Op2 + 10-Core Metal GPU Stage-4)** | **`0.0103s`** (`0.0029s` + `0.0073s`) | **`998.1x`** | **`0.0244s`** (`2.4 ms / target`) | **20 MB Unified Memory** | `10/10 PASS` |
| **`exp_r19_metal_gpu.m` — Mode 3 (Heterogeneous CPU + Metal GPU Co-Execution)** | **`0.0055s`** (`0.0045s` + `0.0010s`) | **`1,870.9x`** (`1.0 ms` on Target B) | **`0.0213s`** (**`2.1 ms / target`**) | **20 MB Unified Memory** | **`48 / 48 CHECKS PASSED`** |
| **`bench_carbon_modes.c` — Carbon-Lite (`KER_BITS=4` `u16`, Theories 37–40)** | **`0.0050s`** (A+B) | **`2,795.6x`** (`3.67 ms` 50-tgt mean) | **`0.0397s`** (`3.97 ms` 10-tgt; **`3.67 ms`** 50-tgt; $2^{18.73}$ SHA19-eq) | **512 MB Direct `u16`** (`0 MB` bitmap/cosets, `0` CAS) | **`60 / 60 OK`** |
| **`bench_carbon_modes.c` — Carbon-Dense (`KER_BITS=4` `u32`, Theories 37–40)** | **`0.0050s`** (A+B) | **`3,375.0x`** (`3.04 ms` 50-tgt mean) | **`0.0337s`** (`3.37 ms` 10-tgt; **`3.04 ms`** 50-tgt; $2^{18.46}$ SHA19-eq) | **1024 MB Direct `u32`** (`0 MB` bitmap/cosets, `0` CAS) | **`60 / 60 OK`** |

### B. Multi-Round Active Algebraic Scaling (`R = 20, 21, 22` — `exp_r20_r21_r22_active_algebraic.c`)

| Round $R$ | Active Firewall & Carry-Pinning Mechanism | Residual Entropy $\mathcal{H}_{\text{res}}(R)$ | Inversion Efficiency $\eta_{\text{inv}}(R)$ | Active Chains Tested | Wall-Clock Solve Time | Verification |
| :---: | :--- | :---: | :---: | :---: | :---: | :---: |
| **`R = 20`** | Carry-Pinning Decomposition $D_{56} = (a_6 \oplus S) \ \& \ \neg D$ + `KER_BITS = 4` ($\mathbb{Z}/16\mathbb{Z}$) | **`14.88 bits`** | **`95.15%`** | **`30,255`** (**`34.7x` fewer** than passive) | **`0.0075s` (`7.5 ms`)** | `PASS (100% EXACT)` |
| **`R = 21`** | $\Sigma_0^{-1}$ Sparse-$e_{14}^*$ Bijection (`(e14 & 0x01ffff00) == 0`) + Carry-Pinning Stage-7/4 | **`12.26 bits`** | **`96.38%`** | **`4,895`** (**`214.2x` fewer** than passive) | **`0.0016s` (`1.6 ms`)** | `PASS (100% EXACT)` |
| **`R = 22`** | $\Sigma_0^{-1}$ Domain-Wall Insulator (`e14 == 0`, $\Delta e_{18} = 0$) + Carry-Pinning Stage-7/4 | **`15.15 bits`** | **`95.92%`** | **`36,243`** (**`28.9x` fewer** than passive) | **`0.0038s` (`3.8 ms`)** | `PASS (100% EXACT)` |

---

## 2. Repository Contents

### A. Next-Generation `R=19` Production Engines (Generations 1–30 / Theories 1–36)
- **`exp_r19_distant_theories.c`**: 8-core ARM64 NEON CPU Ultra Meta Engine integrating all 36 verified algebraic and microarchitectural theories:
  - **6-Level 2-Adic Branching Hensel Tree + Rotated-CTZ $O(1)$ Root Selector + Lazy 1,024-Coset Sieve** (`Theory 34`): Proves that Op2 Micro-Step 1 lives on only $256 \times 4 = 1,024$ cosets `(a11_lo8, hi2)` (`74.22%` algebraically dead), cutting first-visit node evaluations by `4.54x` and rejecting repeated dead cosets in 2 instructions before `S1(E15)` and `S0(a11)`.
  - **4-Wide ARM64 NEON `dz = 0..7` Hensel Lifting & Dual Carry-Free Polynomial Multiplications** (`Theories 31 & 35`): Replaces scalar lookup loads and shift/XOR chains with exact carry-free polynomial multiplications $\Sigma_0(k \ll 28) \equiv k \times \mathtt{0x04008040u}$, $\Sigma_1(k \ll 28) \equiv k \times \mathtt{0x00420008u}$, $\Sigma_0(h_6 \ll 22) \equiv h_6 \times \mathtt{0x00100201u}$, and $\Sigma_0(m_3 \ll 19) \equiv m_3 \times \mathtt{0x20020040u}$.
  - **Sub-Sub-Lattice $\mathbb{Z}/32\mathbb{Z}$ (`c = 0`) + 20 MB System-Level-Cache-Resident $Q_0^{-1}$ Architecture** (`Theories 22, 26–30, 32–33, 36`): Compresses the $Q_0^{-1}$ lookup table into `16 MB` (`bm_Q0_ker`) + `4 MB` (`ht_Q0_ker`, built in **`0.014s`**), paired with 4-wide NEON Hop-1 & Hop-2 vectorization, rotation-free 3-op shifted-XOR `rec_u_z`, unified `sv1` bit-15 syzygy / bit-16 `b0` recovery, and 27-bit partial `W3` pruning.
- **`exp_r19_metal_gpu.m`**: Heterogeneous 8-core ARM64 NEON CPU + 10-core Apple Silicon Metal GPU engine supporting CPU-Only (`Mode 1`), GPU Stage-4 (`Mode 2`), and Lock-Free Work-Stealing CPU+GPU Co-Execution (`Mode 3`), with a 48-check automated invariant verification suite (`48 / 48 CHECKS PASSED`).
- **`exp_r19_carbon_lab.c` & `bench_carbon_modes.c`**: Carbon High `R=19` Engines (`Theories 37–40`) featuring:
  - **Theory 37 (Full-Word $D$ Liberation & Lower-Half Concentration Asymmetry)**: Generalizes $M = \neg(a_5 \oplus a_3) \mid D$ across all 32 bits while concentrating $D$'s residual weight into bits `10..15` (`pD_hi == 0`), shrinking `n_free_lo` without inflating `n_forced_hi`.
  - **Theory 38 (10-Bit Full-Top 2-Adic Hensel Bijection `k = 22..31` + Self-Coupled $\Sigma_0(a_9)$ Lock)**: Deterministically locks `(D & 0x3ff) == 0` and `(W13 & 0xff) == 0x80` in $O(1)$, slashing Op2 context trials by `6.9x–13.4x` (`133.8K` mean trials, regularly hitting `popc(D) <= 1` and `D == 0`).
  - **Theory 39 (Exact Upper-3-Bit $\sigma_0(W_3)[29..31]$ Syzygy + 4-Wide NEON Hop-3 Subspace Solver)**: Exploits $\sigma_0(W) \gg 29 = ((W_{\text{lo18}} \gg 4) \oplus (W_{\text{lo18}} \gg 15)) \ \& \ 7$ to harvest `n_free_lo <= 11` subspaces in vector registers with zero DRAM reads (`3.54x` fewer Stage-4 chains).
  - **Theory 40 (Upper-3-Bit $\sigma_0$ Output-Slice CAS-Free Builder + Zero-Table 9-Bit $\Sigma_1$ Blind-Spot Coset Lock)**: Eliminates all atomic CAS operations during $Q_0^{-1}$ construction and replaces the 32 MB `a1_cosets` table with 3-op algebraic `{a1[22..24], a1[16]}` coset synthesis (`3.04 ms/target` in `1024 MB` Carbon-Dense; `3.67 ms/target` in `512 MB` Carbon-Lite).
- **`experimental_mathematics_battery_36.md`**: Comprehensive 55-section mathematical treatise documenting all 30 Generations / 36 Theories, Passes 1–4 symbolic & multi-round invariants, empirical falsifications, exact proofs, and benchmark logs.

### B. Symbolic ARX Pattern & Firewall Analyzer & Multi-Round Active Algebraic Engine (`R = 19, 20, 21, 22`)
- **`symbolic_arx_analyzer.py`**: Interactive `SymPy` $\mathbb{Z}_{2^{32}} \times \mathbb{F}_2^{32}$ Ring & Canalizing Rewrite Engine (`Module 1`), Automated Firewall, Treewidth & Triangular-Pivot Searcher (`Module 2`), and Exact Rational (`fractions.Fraction`) Markov Carry-Chain Calculator (`Module 4`).
- **`symbolic_arx_f2_markov.c`**: Native C $32 \times 32$ $\mathbb{F}_2$ Bit-Matrix Algebra Engine, Complete Carry-Free Polynomial Multiplier Atlas (discovering 9-bit carry-free windows for $\Sigma_0$, 5-bit windows for $\Sigma_1$, and 4-bit windows for $\sigma_0$), Automated Half-Word $\mathbb{F}_2$ Syzygy Discovery for $Q_0$ & $Q_1$ (discovering the unique 7-bit syzygy mask `0xd522` of $\sigma_1(u_{\text{hi}} \ll 16)$), Carry-Pinning Automaton verifier, and $R=20 / R=21 / R=22$ firewall cut verifier (`Exact Sigma_0^-1 Arbitrary-e14* Bijection` and `Ferromagnetic 2-Adic Subspace Annihilation of e14`).
- **`exp_r20_r21_r22_active_algebraic.c`**: Active Algebraic Multi-Round Engine executing Passes 2, 3, and 4 (`R = 20, 21, 22`) in milliseconds via the Carry-Pinning Residual Theorem ($a_5 = (a_6 \ \& \ D) \mid (S \ \& \ \neg D)$, $D_{56} = (a_6 \oplus S) \ \& \ \neg D$), $\Sigma_0^{-1}$ Sparse-$e_{14}^*$ Bijection, and `KER_BITS = 4` ($\mathbb{Z}/16\mathbb{Z}$) Hop-3 `rec_u_z` Subspace Inversion.
- **`SYMBOLIC_ARX_SOLVER_BLUEPRINT.md`**: Architectural blueprint for the Symbolic ARX Pattern & Firewall Analyzer, general `QF_BV` SMT integration, and the 4-pass multi-round algebraic optimization campaign.

---

## 3. Quick Start

```bash
# Build all engines (R=19 CPU, R=19 Metal GPU, F_2/Markov C engine, and R=20..22 Active Algebraic engine)
make all

# Run the complete verification & benchmark suite
make test
```
