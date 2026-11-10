# Symbolic ARX Pattern & Firewall Analyzer + General `QF_BV` Solver Blueprint

**Source Session:** [`efdd7ba3-6aa1-43c2-b910-be8758ca4e79`](conversation://efdd7ba3-6aa1-43c2-b910-be8758ca4e79)  
**Target Session:** [`e28c5bb0-5610-4f20-aaae-568a120f150a`](conversation://e28c5bb0-5610-4f20-aaae-568a120f150a)  
**Workspace:** `/Users/srw/Code/sha-check-eprint`

---

## 1. Executive Summary & Objective

Instead of relying on commercial **Wolfram Mathematica** to discover multi-round algebraic cancellations, co-variation invariants, optimal firewall cuts, and $\mathbb{F}_2$ bit-slice projections in SHA-256 ($R = 19, 20, 21, 22$) and general ARX systems, we want to build our own **Symbolic ARX Pattern & Firewall Analyzer** (`symbolic_arx_analyzer.py` + native C $\mathbb{F}_2$ bit-matrix helper).

This tool serves **two purposes simultaneously**:
1. **Interactive Mathematical Telescope (Replacing Mathematica):** Automatically discovers hidden $\mathbb{Z}_{2^{32}}$ telescopic cancellations, directional co-variation invariants, minimum-treewidth canalizing firewall cuts, low-rank $\mathbb{F}_2$ bit-slice masks, and exact rational Markov carry-chain probabilities for SHA-256 ($R = 19\text{--}22$) and SHA-512.
2. **Automated Macro-Level Pre-Processor for General Solvers (Z3 / Bitwuzla / C-NEON):** Acts as the static analysis pass that takes any mixed $\mathbb{Z}_{2^w} \times \mathbb{F}_2^w$ constraint graph, automatically applies canalizing cuts, extracts triangular pivots ($\det J \equiv \pm 1 \pmod{2^w}$), and compiles the remaining $x + L(x) = y$ bridges into 256-byte SIMD LUT propagators.

---

## 2. Core Algebraic Identities & Patterns Uncovered (Benchmarks for the Analyzer)

The Symbolic ARX Analyzer must automatically reproduce and generalize the following exact identities discovered during our $R = 20\text{--}22$ analysis:

### 2.1 Exact Telescopic Cancellation of $a_{14}$ at $R = 22$
At $R = 22$ (when `H_target[0..7] - IV[0..7]` locks the final 8 state words $a_{15..22}$ and $e_{19..22}$), **`a14` is the highest free state word**:
* From the Round-19 state equation:
  $$e_{18} = C_{e18} + a_{14} \pmod{2^{32}}$$
* Consequently, the Round-22 target message word $W_{21}^*$ varies **linearly** with $a_{14}$ (slope $-1 \pmod{2^{32}}$):
  $$W_{21}^*(a_{14}) = (C_{21} - C_{e18}) - a_{14} \pmod{2^{32}}$$
* **Hidden Telescopic Cancellation in $W_{17}$:** When expanding $W_{17}$ in terms of the state backbone, the terms $e_{18}$ and $-a_{14}$ appear as the exact difference $(e_{18} - a_{14}) \equiv C_{e18} \pmod{2^{32}}$. Thus:
  $$\frac{\partial W_{17}}{\partial a_{14}} \equiv 0 \pmod{2^{32}}$$
  meaning $a_{14}$ cancels completely out of $W_{17}$ and only enters the top window via $\Sigma_1(e_{18})$ in $W_{18}$ and $-a_{14}$ in $W_{21}^*$.

### 2.2 Exact Top–Bottom Variable Separation at $R = 22$ ($\rho_{21} = 0$)
Because single-block FIPS 180-4 padding fixes $W_{14} = 0$ (high 32 bits of the 64-bit length), once $\rho_{19} = 0$ holds ($W_{19} = W_{19}^*$), the Round-22 message recurrence $\rho_{21} = W_{21} - (\sigma_1(W_{19}) + W_{14} + \sigma_0(W_6) + W_5) = 0$ splits cleanly into a **purely top-state LHS** and a **purely bottom-state RHS**:
$$W_{21}^*(a_{14}) - \sigma_1(W_{19}^*) \equiv \sigma_0(W_6) + W_5 \pmod{2^{32}}$$

### 2.3 Dynamic Co-Variation / Iso-Surface Invariants ($\ker J$)
In the 20/21/22-round Firewall (`solve20_padded_fast.c`, `solve22_padded_pivot.c`), $a_5$ is traditionally frozen inside the firewall so that $e_9 = a_{13} - \Sigma_0(a_{12}) - \text{Maj}(a_{12},a_{11},a_{10}) - K_9 - a_9 \dots$ and $W_9$ stay constant.
* **Directional Derivative Invariant:** Because $a_9$ and $a_5$ enter the $e_9 / W_9$ spine via the sum $a_9 + a_5$, moving along the 1D affine subspace:
  $$a_9 + a_5 \equiv C_9 \pmod{2^{32}} \iff \Delta a_9 \equiv -\Delta a_5 \pmod{2^{32}}$$
  keeps $e_9$ and $W_9$ **strictly invariant** while allowing $a_5$ to vary dynamically at Hop 4 to satisfy $W_5$!

### 2.4 Lower-Triangular Causal Horizon Jacobians ($\det J \equiv \pm 1 \pmod{2^{32}}$)
As implemented in `solve22_padded_pivot.c`, the $3 \times 3$ Jacobian of the top recurrence residuals $(\rho_{19}, \rho_{20}, \rho_{21})$ with respect to $(a_{20}, a_{21}, a_{22})$ on the direct linear spine satisfies:
$$\det \frac{\partial(\rho_{19}, \rho_{20}, \rho_{21})}{\partial(a_{20}, a_{21}, a_{22})} \equiv 1 \pmod{2^{32}}$$
enabling $O(1)$ triangular closure (or 1-cycle / 2-cycle fixed-point iteration across $\Sigma, \sigma$).

---

## 3. Specification of the 4 Modules to Build

### Module 1: Symbolic $\mathbb{Z}_{2^w} \times \mathbb{F}_2^w$ Ring & Canalizing Rewrite Engine (`SymPy`)
* Represent state words $a_t, e_t$, message words $W_t$, and recurrence residuals $\rho_t = W_t - (\sigma_1(W_{t-2}) + W_{t-7} + \sigma_0(W_{t-15}) + W_{t-16})$ as exact symbolic expressions over $\mathbb{Z}_{2^{32}}$ with uninterpreted operator heads `S0`, `S1`, `s0`, `s1`, `Ch`, `Maj`.
* Bake in automatic **canalizing rewrite rules**:
  * `Ch(0, y, z) -> z`, `Ch(0xFFFFFFFF, y, z) -> y`, `Ch(x, y, y) -> y`, `Ch(x, y, ~y) -> ~(x ^ y)`
  * `Maj(x, x, z) -> x`, `Maj(x, ~x, z) -> z`, `Maj(0, y, z) -> y & z`, `Maj(0xFFFFFFFF, y, z) -> y | z`
  * Domain-Wall Insulator identities: e.g., $a_{t-2} = a_{t-1} = \Sigma_0^{-1}(a_t + \Sigma_0(a_t) + \text{0xFFFFFFFF} - a_{t+1}) \implies e_t = 0$.
* **Automated Symbolic Jacobian & Nullspace Calculator:**
  * Compute $J_{\text{linear}}[i, j] = \frac{\partial W_i}{\partial a_j} \pmod{2^{32}}$ (separating direct modular linear dependencies from nonlinear operator dependencies $\frac{\partial W_i}{\partial \Sigma_0(a_j)}$, etc.).
  * Automatically list all **telescopic cancellations** (where multi-round paths sum to $0 \pmod{2^{32}}$) and all **co-variation nullspace vectors** $v \in \ker J_{\text{linear}}$ (combinations of $\Delta a_j$ that hold a set of intermediate words invariant).

### Module 2: Automated Firewall, Treewidth & Triangular-Pivot Searcher
* Build the directed dependency hypergraph $G = (V, E)$ connecting state variables $\{a_1, \dots, a_R\}$ to target equations $\{\text{Pad}(W_{0..15}), \rho_{16..R-1}, \text{HashTarget}\}$.
* Automatically search over candidate sets of canalizing cuts (`Ch` insulators and `Maj` equalities) to find:
  1. **Maximal Triangular Pivots:** Largest subset of equations and variables whose linear spine Jacobian is lower-triangular with unit diagonal ($\det J \equiv \pm 1 \pmod{2^{32}}$).
  2. **Minimum Feedback Vertex Set (Optimal Firewall Cut):** Smallest subset of state words whose fixing disconnects feedback loops and maximizes the number of decoupled $x + L(x) = y$ single-word bridges ($Q_0^{-1}$-type hops).
  3. **Inside-Out vs. Outside-In Schedule Comparison:** Compare treewidth when solving recurrence equations $\rho_{16..R-1} = 0$ first and padding constraints last vs. padding-first.

### Module 3: Fast C / Python $\mathbb{F}_2$ Bit-Matrix Algebra Engine
* Extend the operator algebra approach in `exp_r20_op_algebra.c`:
  * Represent any $\mathbb{F}_2$-linear 32-bit operator ($\Sigma_0, \Sigma_1, \sigma_0, \sigma_1$, rotations, shifts, compositions, and inverses) as a 32-element `uint32_t` array (`128 bytes`).
  * Automatically search for **minimum-dependency bit-slice masks** $P \in \mathbb{F}_2^{k \times 32}$ such that $P \cdot M_B \cdot M_A^{-1}$ has minimum row weight / rank—identifying exact partial bit-masks that can prune Hop-1 / Hop-2 / Hop-3 candidates before full word evaluation.

### Module 4: Exact Rational Markov Carry-Chain Transfer Calculator
* Using exact rational matrices (`SymPy.Matrix` or `fractions.Fraction`), compute the exact carry-transition probabilities across byte boundaries ($b = 7 \to 8, 15 \to 16, 23 \to 24$) for composite operators $x + L(x) = y \pmod{2^{32}}$ without Monte Carlo sampling error.

---

## 4. General `QF_BV` SMT Integration (Z3 / Bitwuzla Macro + Micro Architecture)

When generalizing our engine beyond SHA-256 into a reusable SMT solver contribution (or standalone general `QF_BV` ARX solver), the architecture splits cleanly into **4 SHA-agnostic primitives**:

| Level | Primitive Name | General `QF_BV` Pattern Matched | Mechanism |
| :--- | :--- | :--- | :--- |
| **Macro (`Z3 Tactic`)** | **1. Canalizing-Gate Centrality Cut** | `bvor(bvand(x,y), bvand(x,z), bvand(y,z))` (`Maj`) and `bvor(bvand(c,x), bvand(bvnot(c),y))` (`Ch`/`ITE`) | Computes betweenness centrality in the AST dependency graph; asserts `x == y` on top-centrality `Maj` gates or `c == 0 / ~0` on `Ch` gates, allowing native `simplify` + `solve-eqs` to collapse feedback cycles automatically. |
| **Macro (`Z3 Tactic`)** | **2. Automatic $\mathbb{F}_2$-Linear Matrix Inverter** | Any expression $L(x) = y$ or `Extract(hi, lo, L(x)) == y` composed of `bvxor`, `bvshl`, `bvlshr`, `RotateRight` | Builds the $w \times w$ $\mathbb{F}_2$ bit-matrix in one 64-bit word per row, runs Gaussian elimination in nanoseconds, and substitutes $x = L^{-1}(y)$ (plus nullspace basis if singular, e.g. $\sigma_0, \sigma_1$). |
| **Micro (`user_propagator`)** | **3. Causal LSB-to-MSB Carry-Sudoku Branching** | Bit-vectors coupled by `bvadd` / `bvsub` chains | Overrides default CDCL `VSIDS` bit-order on arithmetic words to branch strictly LSB-first ($j = 0 \to w-1$), making carry bit $c_{j+1}$ a deterministic unit propagation at every step. |
| **Micro (`user_propagator`)** | **4. 256-Byte LUT SIMD Composite Inverter** | Any node matching `bvadd(x, L(x)) == y` (or AES key-schedule `bvxor(x, SubWord(RotWord(x))) == y`) | Synthesizes the four 256-byte byte-slice tables at startup in $< 1\text{ }\mu\text{s}$ and executes 16-way ARM NEON `vtbl` + `vpaddq_u8` lookahead pruning in $\sim 40\text{ ns}$ per inversion. |

---

## 5. Immediate Action Plan for Session `e28c5bb0`

Once current work on `exp_r19_distant_theories.c` completes:
1. Build a clean, fast prototype of the **Symbolic ARX Pattern & Firewall Analyzer** (`symbolic_arx_analyzer.py`, optionally paired with a C $\mathbb{F}_2$ bit-matrix tool).
2. Run it on **SHA-256 at $R = 19, 20, 21, 22$** to:
   * Automatically verify the Jacobian $J_{i,j} = \frac{\partial W_i}{\partial a_j} \pmod{2^{32}}$, confirm all telescopic cancellations ($\frac{\partial W_{17}}{\partial a_{14}} = 0$), and enumerate all co-variation nullspaces ($\Delta a_9 + \Delta a_5 = 0$, etc.).
   * Search for any **new, previously unnoticed multi-word cancellations or lower-treewidth firewall cuts** at $R = 20, 21, 22$.
3. Report the discovered algebraic invariants and optimal firewall cuts back to the user.
