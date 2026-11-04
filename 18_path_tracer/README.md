# Lab 18: Path Tracer Pixel Reconstruction Filters & Super-Sampling

This lab implements **multiple rays per pixel (super-sampling)** using various sample generators, combined with **multiple pixel reconstruction filters** (**Box**, **Triangle**, **Gaussian**, and **Mitchell-Netravali**) to weight returned ray radiances based on their subpixel distance from the pixel center.

The implementation is integrated into both the **Intel Embree (CPU)** and **NVIDIA OptiX (GPU)** path tracing backends.

---

## 1. Pixel Filters Formulation

Given a pixel with center $(x_c, y_c) = (px + 0.5, py + 0.5)$, a continuous subpixel sample $(x, y)$ has offset:
$$\Delta x = x - x_c, \quad \Delta y = y - y_c$$
The sample weight is evaluated using a 2D separable filter:
$$W(\Delta x, \Delta y) = f(\Delta x) \cdot f(\Delta y)$$

The final pixel color for a set of $S$ samples is normalized by the sum of their weights:
$$I(px, py) = \frac{\sum_{s=1}^{S} w_s \cdot L_s}{\sum_{s=1}^{S} w_s}$$

### 1. Box Filter
- **Radius**: $r_x = 0.5, r_y = 0.5$ (spans the pixel boundary).
- **Formula**:
  $$f_{\text{box}}(x) = \begin{cases} 1.0 & \text{if } |x| \le r_x \\ 0.0 & \text{otherwise} \end{cases}$$
- Uniformly averages all samples within the pixel without spatial weighting.

### 2. Triangle (Tent) Filter
- **Radius**: $r_x = 1.0, r_y = 1.0$.
- **Formula**:
  $$f_{\text{tri}}(x) = \max\left(0.0, 1.0 - \frac{|x|}{r_x}\right)$$
- Weights decrease linearly from 1.0 at the pixel center to 0.0 at the boundary.

### 3. Windowed Gaussian Filter
- **Radius**: $r_x = 1.5, r_y = 1.5$ (or configurable).
- **Formula**:
  $$f_{\text{gauss}}(x) = \max\left(0.0, e^{-\alpha x^2} - e^{-\alpha r_x^2}\right)$$
- Subtracting the edge term ensures that the filter smoothly drops to zero at $|x| = r_x$, preventing harsh boundary steps. Default $\alpha = 2.0$.

### 4. Mitchell-Netravali Cubic Spline Filter
- **Radius**: $r_x = 2.0, r_y = 2.0$.
- **Formula**:
  With normalized distance $d = 2 \cdot \frac{|x|}{r_x}$:
  - If $d < 1$:
    $$f(x) = \frac{1}{6} \left[ (12 - 9B - 6C) d^3 + (-18 + 12B + 6C) d^2 + (6 - 2B) \right]$$
  - If $1 \le d < 2$:
    $$f(x) = \frac{1}{6} \left[ (-B - 6C) d^3 + (6B + 30C) d^2 + (-12B - 48C) d + (8B + 24C) \right]$$
  - If $d \ge 2$:
    $$f(x) = 0.0$$
- Recommended parameters: $B = \frac{1}{3}, C = \frac{1}{3}$. Includes subtle negative lobes for edge sharpening without excessive ringing. Catmull-Rom ($B=0, C=\frac{1}{2}$) is also available.

---

## 2. Subpixel Sample Generators

Samples within the filter support $[-r_x, r_x] \times [-r_y, r_y]$ are generated using four selectable techniques:

1. **Random / Uniform Jitter**: Independent uniform pseudo-random subpixel positions.
2. **Stratified Grid**: Samples placed inside a regular grid of sub-cells with randomized jitter per cell.
3. **Halton Sequence**: Low-discrepancy 2D sequence using prime bases $(2, 3)$ with Cranley-Patterson per-pixel shift to prevent aliasing.
4. **Sobol Sequence**: Base-2 dyadic $(t, 2)$-sequence using Antonov-Saleev Gray code with Cranley-Patterson per-pixel shift.

---

## 3. How to Build & Run

```bash
cd 18_path_tracer
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCUDA_ARCH=75
cmake --build . -j

# Run path tracer
./18_path_tracer
```

### GUI Features & Controls
- **Pixel Filters Header**:
  - Filter Type combo (Box, Triangle, Gaussian, Mitchell-Netravali).
  - Filter Radius slider ($0.5$ to $3.0$ px).
  - Gaussian Alpha parameter slider.
  - Mitchell $B$ and $C$ sliders (with presets for Standard $1/3, 1/3$ and Catmull-Rom $0, 1/2$).
  - **Live Filter Weight Curve**: Real-time cross-section plot of the weighting function $f(\Delta x)$ vs. distance from pixel center.
  - Sample Generator combo (Random, Stratified, Halton, Sobol).
  - Rays/Pixel per frame presets ($1, 2, 4, 8, 16, 32$) and custom slider.
