# Lab 17: Halton & Sobol Sequence Generators (Low-Discrepancy Sampling)

This lab implements and delves into **Quasi-Random (Low-Discrepancy) Sequence Generators** (**Halton** and **Sobol**), comparing their spatial distribution against **Pseudo-Random (`std::mt19937`)** in a 1×1×1 3D box rendered in real-time with miniballs.

---

## 1. Mathematical Details

### A. Pseudo-Random (`std::mt19937`)
- Points are sampled independently and uniformly: $p_i \sim \mathcal{U}([0, 1]^3)$.
- **Problem**: Due to complete statistical independence (Poisson process), points suffer from **Poisson clumping**:
  - Probability of pairs landing arbitrarily close together is high ($d_{\min} \to 0$).
  - Large empty voids / holes remain throughout the volume.
  - In Monte Carlo path tracing, this leads to high variance converging slowly at $\mathcal{O}(N^{-1/2})$.

### B. Halton Sequence
- Based on the 1D **van der Corput** radical inverse function:
  $$i = \sum_{j=0}^{M} a_j(i) b^j \implies \Phi_b(i) = \sum_{j=0}^{M} a_j(i) b^{-(j+1)}$$
- For 3D points, pairwise coprime prime bases are chosen: $b_1 = 2, b_2 = 3, b_3 = 5$.
  $$P_i = (\Phi_2(i), \Phi_3(i), \Phi_5(i))$$
- **Unwanted Patterns in Halton**:
  - When using higher prime bases (such as $(11, 13, 17)$) or large sample counts, strong **linear correlation lines / stripes** emerge across 2D projections.
  - **Solution (Scrambled Halton)**: Applying a digit permutation $\sigma_b$ (e.g. Faure permutation) shuffles digits while keeping $\sigma_b(0) = 0$, shattering the linear correlation stripes.

### C. Sobol Sequence
- Base-2 $(t, s)$-sequence using primitive polynomials over $\mathbb{F}_2$ and direction integers $m_j$.
  - Dimension 0 ($x$): Polynomial $P_0(x) = x$, all $m_j = 1$.
  - Dimension 1 ($y$): Polynomial $P_1(x) = x + 1$, recurrence $m_j = 2 m_{j-1} \oplus m_{j-1}$.
  - Dimension 2 ($z$): Polynomial $P_2(x) = x^2 + x + 1$, recurrence $m_j = 2 m_{j-1} \oplus 5 m_{j-2}$.
- Generated efficiently in $\mathcal{O}(1)$ via **Antonov-Saleev Gray Code**:
  $$X_i = \bigoplus_{j=1}^{32} g(i)_j \cdot V_j, \quad g(i) = i \oplus (i \gg 1)$$
- **Unwanted Patterns in Sobol**:
  - **Non-Power-of-2 Artifacts**: Sobol's $(t, s)$ net guarantees uniform dyadic elementary interval coverage strictly when sample count $N$ is a power of 2 ($N = 2^m$). If $N$ is arbitrary, dyadic boxes are partially filled, producing density imbalance.
  - **Origin Alignment**: Point $i=0$ is pinned to $(0, 0, 0)$.
  - **Solution**: Digital Cranley-Patterson shift ($X_i \oplus S$) randomizes the origin while strictly maintaining dyadic net properties.

---

## 2. Quantitative Comparison ($N = 512$ points in 1×1×1 box)

| Generator | Min Pairwise Dist ($d_{\min}$) | Spacing StdDev ($\sigma_{NN}$) | $\chi^2$ (4×4×4 Grid Uniformity) | Voxel Voids (Empty Cells) |
| :--- | :---: | :---: | :---: | :---: |
| **`std::random`** | `0.0159` (clumped) | `0.0278` (high variance) | `~58.2` (Poisson noise) | Often 1–3 voids |
| **`Halton (2,3,5)`** | `0.0195` (repulsive) | `0.0229` (tight spacing) | `~12.5` (smooth coverage) | `0` voids |
| **`Sobol (3D)`** | `0.0259` (best spacing) | `0.0273` | **`0.00`** (perfect dyadic net!) | `0` voids |

---

## 3. How to Build & Run

```bash
cd 17_sampling
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j

# Run the viewer
./17_sampling
```

### Controls & GUI Features
- **Sequence Selection**: Switch between `Pseudo-Random`, `Halton`, `Sobol`, or `Side-by-Side (All 3)`.
- **Sample Count Presets**: Quick power-of-2 buttons ($64, 128, 256, 512, 1024, 2048, 4096$) or continuous slider.
- **Halton Prime Presets & Scramble Toggle**: Test preset $(11, 13, 17)$ to see unwanted correlation stripes, then toggle **Scramble** to observe them vanish!
- **Sobol Digital Shift**: Toggle Cranley-Patterson XOR shift.
- **Coloring Modes**:
  - Uniform Theme Color
  - 3D Coordinates ($RGB = XYZ$)
  - Sequence Order (Rainbow/Turbo gradient)
  - Nearest-Neighbor Density Heatmap (Blue = sparse, Red = clumped)
- **Camera Shortcuts**: Orbit (Left Click + Drag), Zoom (Scroll), or Orthographic Top (XY) / Front (XZ) / Side (YZ) buttons.
