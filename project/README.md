# Project 1: Interactive 3D Scene Viewer & Engine


https://github.com/user-attachments/assets/cef94706-4bd0-40d7-986b-3c48ac014f02


A 3D scene viewer and geometric processing engine built with C++20 and Modern OpenGL (3.3+ Core Profile). The application integrates OBJ/MTL asset parsing, dynamic shadow mapping with directional lighting, mouse ray-casting for vertex picking, Half-Edge topology structures, Fast Marching geodesic distance field propagation, and QEM mesh decimation, all controlled in real-time via a Dear ImGui panel.

---

## Features Checklist

| Feature | Category | Implementation Details |
| :--- | :--- | :--- |
| **Engine & Camera Foundation** | Core | Modern OpenGL 3.3+ Core Profile pipeline with keyboard navigation (WASD) and Left-Click mouse look. |
| **Asset & Material Parsing** | Assets | Loads `.obj` / `.mtl` scenes using `tinyobjloader` with $1:1$ vertex deduplication by position and a `TextureCache` using `std::weak_ptr`. |
| **Lighting & Two-Pass Shadows**| Lighting | Two-pass directional shadow mapping using depth FBOs paired with Phong illumination ($K_a, K_d, K_s, K_e$, shininess) in modular shaders for textured and solid materials. |
| **Diagnostic Render Modes** | Visualization | UI-selectable modes: `0: Default Phong`, `1: Unlit Flat Geometry`, and `2: TexCoords / Geodesic Heatmap` (with `UV.x` interpretation). |
| **Vertex Picking** | Interaction | Right-Click ray-casting to pick vertices, displaying vertex IDs, sub-mesh pointers, world positions, and hit distances. |
| **Fast Marching Distance Fields**| Geometry | Eikonal equation solver over Half-Edge topology to calculate surface geodesic distances propagating from the selected vertex seed. |
| **QEM Mesh Simplification** | Geometry | Quadric Error Metric edge-collapse decimation with target ratio control, Min-Heap priority queue, and instant mesh reset functionality. |
| **Procedural Motion & Controls**| Animation | Toggleable animated directional light motion and dynamic Wireframe rendering (`glPolygonMode`). |
| **GUI Control Panel** | Controls | Integrated Dear ImGui control panel providing live performance monitoring (FPS/ms), light tweaking, OBJ loading, picking info, and QEM decimation tools. |

---

## Build & Run Instructions

### Prerequisites
* **Compiler:** C++20 compatible compiler (MSVC, GCC, or Clang).
* **Graphics API:** OpenGL 3.3+ Core Profile.
* **Build System:** CMake 3.20 or higher.
* **Dependencies:** GLFW, GLM, Dear ImGui, `tinyobjloader`, `stb_image`.

### Building with CMake

```bash
# 1. Clone the repository
git clone https://github.com/juaquin456/3D-Graphics-Programming-Labs.git
cd 3d-scene-viewer

# 2. Create build directory
mkdir build && cd build

# 3. Configure and build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

## Running the Viewer
```bash
# From the root directory:
./build/SceneViewer
```
## Control Reference
### Camera Navigation & Look
- WASD: Move camera position.
- Hold Left Click + Drag: Rotate camera view direction (Look around).

### Interaction & Vertex Picking
- Right Click: Cast a ray into the scene to pick a vertex on the targeted mesh. This sets the source seed for Fast 
Marching distance field calculations and selects the active sub-mesh for QEM simplification.## Technical Overview

## Technical Overview
1. Asset Loading & Texture Cache
- Deduplication: Utilizes tinyobjloader with forced triangulation and $1:1$ position-based deduplication (idx.
   vertex_index), preserving manifold surface topology required by Half-Edge algorithms.
- Texture Cache: Employs a TextureCache backed by std::weak_ptr to avoid redundant GPU texture allocations and VRAM thrashing when materials share texture maps.
- Format Handling: Configures glPixelStorei(GL_UNPACK_ALIGNMENT, 1) and GL_REPEAT wrapping to ensure correct unpacking of odd-width textures and tiling UV coordinates.2. Shadow Mapping Strategy

2. Lighting & Dynamic Shadow Mapping
- Two-Pass Pipeline: Renders depth from the directional light's perspective into a Depth Framebuffer Object (FBO) during Pass 1, followed by standard scene rendering in Pass 2.
- Modular Shaders: Uses separate shader programs tailored for solid Phong materials and textured materials, incorporating shadow bias calculations to prevent shadow acne.

3. Half-Edge Topology & Fast Marching
- Converts loaded MeshData into a HalfEdgeContainer representing vertex, half-edge, and twin connections.
- Solves the Eikonal distance equation over triangular faces, propagating wavefront distances from the Right-Click selected vertex seed across the manifold structure without heap re-allocations in local fan traversals.
- Stores normalized distances into UV.x, rendered via the "TexCoords / Geodesic Heatmap" diagnostic mode.

4. QEM Mesh Decimation Algorithm
- Calculates fundamental error quadrics $Q$ per vertex.
- Candidate edge collapses are managed via a Min-Heap priority queue (QueueSystem) using 64-bit edge keys and Lazy Deletion for $O(1)$ edge erasures.
- Exposed via an ImGui Target Ratio slider (0.10 to 1.00) with a Simplify Mesh trigger and a Reset Mesh button to restore the original geometry.

## Gallery

<img width="1283" height="718" alt="Screenshot from 2026-09-28 21-00-40" src="https://github.com/user-attachments/assets/ac98186e-2aac-43dd-a4d6-5f93bd0d586f" />
<img width="1283" height="718" alt="Screenshot from 2026-09-28 21-01-11" src="https://github.com/user-attachments/assets/b5c66dc3-622a-4b3b-9ddd-9cc93f0725c3" />
<img width="1283" height="718" alt="Screenshot from 2026-09-28 21-02-33" src="https://github.com/user-attachments/assets/f6f9b556-712d-47f9-ad5a-2b2056738fcd" />
<img width="1283" height="718" alt="Screenshot from 2026-09-28 21-04-12" src="https://github.com/user-attachments/assets/a6357069-6978-4c18-ae2f-9e9f05eb8385" />
<img width="1283" height="718" alt="Screenshot from 2026-09-28 21-04-37" src="https://github.com/user-attachments/assets/34e2acac-1db6-4a15-a20f-b14484fe0a06" />

<img width="1283" height="718" alt="Screenshot from 2026-09-28 21-05-24" src="https://github.com/user-attachments/assets/3c6e6b71-f24f-47f8-b77e-4f40f64219ff" />
