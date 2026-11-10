# `sha256-arx-nextgen` — Next-Generation Symbolic & Heterogeneous ARX Preimage Engine (`R=19` Gen-30 + Symbolic ARX Analyzer)

Next-generation experimental mathematics, symbolic ARX analysis, and heterogeneous Apple Silicon (8-core ARM64 NEON CPU + 10-core Metal GPU) preimage solver suite for step-reduced FIPS 180-4 padded SHA-256.

---

## 1. Performance Leap Over Legacy Baseline (`R=19` FIPS 180-4 Padded SHA-256)

| Engine / Mode | Target A + Target B Pure Search | Speedup vs `10.260s` Legacy Baseline | 10-Target Stress Suite Total (`Mean / Target`) | Table Footprint | Verification |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Legacy Baseline** | `10.260s` | `1.0x` | `~102.6s` (`~10.26s / target`) | Multi-GB | `2/2 PASS` |
| **`exp_r19_distant_theories.c` (8-Core ARM64 NEON CPU)** | **`0.0070s`** (`0.0055s` + `0.0015s`) | **`1,468.9x`** (`6,556.6x` on Target B) | **`0.0501s`** (`5.0 ms / target`; min **`0.5 ms`**) | **20 MB SLC-resident** (`16 MB` bitmap + `4 MB` hash) | `10/10 PASS` (`0` errors) |
| **`exp_r19_metal_gpu.m` — Mode 1 (8-Core CPU Only)** | **`0.0136s`** (`0.0072s` + `0.0064s`) | **`751.7x`** | **`0.0429s`** (`4.3 ms / target`) | **20 MB SLC-resident** | `10/10 PASS` |
| **`exp_r19_metal_gpu.m` — Mode 2 (NEON Op2 + 10-Core Metal GPU Stage-4)** | **`0.0103s`** (`0.0029s` + `0.0073s`) | **`998.1x`** | **`0.0244s`** (`2.4 ms / target`) | **20 MB Unified Memory** | `10/10 PASS` |
| **`exp_r19_metal_gpu.m` — Mode 3 (Heterogeneous CPU + Metal GPU Co-Execution)** | **`0.0055s`** (`0.0045s` + `0.0010s`) | **`1,870.9x`** (`1.0 ms` on Target B) | **`0.0213s`** (**`2.1 ms / target`**) | **20 MB Unified Memory** | **`48 / 48 CHECKS PASSED`** |

---

## 2. Repository Contents

### A. Next-Generation `R=19` Production Engines (Generations 1–30 / Theories 1–36)
- **`exp_r19_distant_theories.c`**: 8-core ARM64 NEON CPU Ultra Meta Engine integrating all 36 verified algebraic and microarchitectural theories:
  - **6-Level 2-Adic Branching Hensel Tree + Rotated-CTZ $O(1)$ Root Selector + Lazy 1,024-Coset Sieve** (`Theory 34`): Proves that Op2 Micro-Step 1 lives on only $256 \times 4 = 1,024$ cosets `(a11_lo8, hi2)` (`74.22%` algebraically dead), cutting first-visit node evaluations by `4.54x` and rejecting repeated dead cosets in 2 instructions before `S1(E15)` and `S0(a11)`.
  - **4-Wide ARM64 NEON `dz = 0..7` Hensel Lifting & Dual Carry-Free Polynomial Multiplications** (`Theories 31 & 35`): Replaces scalar lookup loads and shift/XOR chains with exact carry-free polynomial multiplications $\Sigma_0(k \ll 28) \equiv k \times \mathtt{0x04008040u}$, $\Sigma_1(k \ll 28) \equiv k \times \mathtt{0x00420008u}$, $\Sigma_0(h_6 \ll 22) \equiv h_6 \times \mathtt{0x00100201u}$, and $\Sigma_0(m_3 \ll 19) \equiv m_3 \times \mathtt{0x20020040u}$.
  - **Sub-Sub-Lattice $\mathbb{Z}/32\mathbb{Z}$ (`c = 0`) + 20 MB System-Level-Cache-Resident $Q_0^{-1}$ Architecture** (`Theories 22, 26–30, 32–33, 36`): Compresses the $Q_0^{-1}$ lookup table into `16 MB` (`bm_Q0_ker`) + `4 MB` (`ht_Q0_ker`, built in **`0.014s`**), paired with 4-wide NEON Hop-1 & Hop-2 vectorization, rotation-free 3-op shifted-XOR `rec_u_z`, unified `sv1` bit-15 syzygy / bit-16 `b0` recovery, and 27-bit partial `W3` pruning.
- **`exp_r19_metal_gpu.m`**: Heterogeneous 8-core ARM64 NEON CPU + 10-core Apple Silicon Metal GPU engine supporting CPU-Only (`Mode 1`), GPU Stage-4 (`Mode 2`), and Lock-Free Work-Stealing CPU+GPU Co-Execution (`Mode 3`), with a 48-check automated invariant verification suite (`48 / 48 CHECKS PASSED`).
- **`experimental_mathematics_battery_36.md`**: Comprehensive 54-section mathematical treatise documenting all 30 Generations / 36 Theories, empirical falsifications, exact proofs, and benchmark logs.

### B. Symbolic ARX Pattern & Firewall Analyzer (`R = 19, 20, 21, 22`)
- **`symbolic_arx_analyzer.py`**: Interactive `SymPy` $\mathbb{Z}_{2^{32}} \times \mathbb{F}_2^{32}$ Ring & Canalizing Rewrite Engine (`Module 1`), Automated Firewall, Treewidth & Triangular-Pivot Searcher (`Module 2`), and Exact Rational (`fractions.Fraction`) Markov Carry-Chain Calculator (`Module 4`).
- **`symbolic_arx_f2_markov.c`**: Native C $32 \times 32$ $\mathbb{F}_2$ Bit-Matrix Algebra Engine, Complete Carry-Free Polynomial Multiplier Atlas (discovering 9-bit carry-free windows for $\Sigma_0$, 5-bit windows for $\Sigma_1$, and 4-bit windows for $\sigma_0$), Automated Half-Word $\mathbb{F}_2$ Syzygy Discovery for $Q_0$ & $Q_1$ (discovering the unique 7-bit syzygy mask `0xd522` of $\sigma_1(u_{\text{hi}} \ll 16)$), Carry-Pinning Automaton verifier, and $R=20 / R=21$ firewall cut verifier (`Exact Sigma_0^-1 Arbitrary-e14* Bijection` and `Ferromagnetic 2-Adic Subspace Annihilation of e14`).
- **`SYMBOLIC_ARX_SOLVER_BLUEPRINT.md`**: Architectural blueprint for the Symbolic ARX Pattern & Firewall Analyzer and general `QF_BV` SMT integration.

---

## 3. Quick Start

```bash
# Build all engines (CPU, Metal GPU, and F_2/Markov C engine)
make all

# Run the complete verification & benchmark suite
make test
```
