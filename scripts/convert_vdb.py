#!/usr/bin/env python3
"""
scripts/convert_vdb.py
Converts OpenVDB volumetric sequences or single files (density + optional flames/temperature)
to optimized 2-channel 3D volume raw binary files + metadata.json
for real-time OpenGL ray marching.
"""

import sys
import os
import glob
import json
import argparse
import time
import ctypes
import numpy as np


def _setup_openvdb_environment():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.abspath(os.path.join(script_dir, ".."))

    runtime_candidates = [
        os.path.join(repo_root, "external", "openvdb_runtime"),
        "/tmp/openvdb_root",
    ]

    for runtime_base in runtime_candidates:
        py_dist = os.path.join(runtime_base, "usr", "lib", "python3", "dist-packages")
        if os.path.isdir(py_dist):
            if py_dist not in sys.path:
                sys.path.insert(0, py_dist)

            lib_dirs = [
                os.path.join(runtime_base, "usr", "lib", "x86_64-linux-gnu"),
                os.path.join(runtime_base, "usr", "lib", "llvm-14", "lib"),
            ]
            for ld in lib_dirs:
                if os.path.isdir(ld):
                    for so_name in [
                        "liblog4cplus-2.0.so.3",
                        "libLLVM-14.so.1",
                        "libopenvdb.so.10.0",
                        "libopenvdb_ax.so.10.0",
                    ]:
                        so_path = os.path.join(ld, so_name)
                        if os.path.isfile(so_path):
                            try:
                                ctypes.CDLL(so_path, mode=ctypes.RTLD_GLOBAL)
                            except Exception:
                                pass
            break


_setup_openvdb_environment()

try:
    import pyopenvdb as vdb
except ImportError:
    try:
        import openvdb as vdb
    except ImportError:
        sys.stderr.write(
            "ERROR: Could not import 'pyopenvdb' or 'openvdb'.\n"
            "Please ensure OpenVDB Python bindings are installed or extracted in external/openvdb_runtime.\n"
        )
        sys.exit(1)


DENSITY_CANDIDATES = ("density", "Density", "smoke", "Smoke", "fog", "Fog")
TEMP_CANDIDATES = ("flames", "Flames", "temperature", "Temperature", "heat", "Heat", "fuel", "Fuel")


def find_grid(grids, candidates, fallback_first=False):
    grid_map = {g.name: g for g in grids}
    for name in candidates:
        if name in grid_map:
            return grid_map[name]
    if fallback_first and grids:
        return grids[0]
    return None


def resample_3d(arr, target_shape=(128, 128, 128)):
    """
    Fast vectorized trilinear resampling of a 3D numpy array.
    Maps arr (D_in, H_in, W_in) -> (target_D, target_H, target_W).
    """
    in_shape = arr.shape
    gz = np.linspace(0, in_shape[0] - 1, target_shape[0], dtype=np.float32)
    gy = np.linspace(0, in_shape[1] - 1, target_shape[1], dtype=np.float32)
    gx = np.linspace(0, in_shape[2] - 1, target_shape[2], dtype=np.float32)

    iz0 = np.floor(gz).astype(np.int32)
    iz1 = np.minimum(iz0 + 1, in_shape[0] - 1)
    fz = (gz - iz0)[:, None, None]

    iy0 = np.floor(gy).astype(np.int32)
    iy1 = np.minimum(iy0 + 1, in_shape[1] - 1)
    fy = (gy - iy0)[None, :, None]

    ix0 = np.floor(gx).astype(np.int32)
    ix1 = np.minimum(ix0 + 1, in_shape[2] - 1)
    fx = (gx - ix0)[None, None, :]

    c000 = arr[np.ix_(iz0, iy0, ix0)]
    c001 = arr[np.ix_(iz0, iy0, ix1)]
    c010 = arr[np.ix_(iz0, iy1, ix0)]
    c011 = arr[np.ix_(iz0, iy1, ix1)]
    c100 = arr[np.ix_(iz1, iy0, ix0)]
    c101 = arr[np.ix_(iz1, iy0, ix1)]
    c110 = arr[np.ix_(iz1, iy1, ix0)]
    c111 = arr[np.ix_(iz1, iy1, ix1)]

    c00 = c000 * (1.0 - fx) + c001 * fx
    c01 = c010 * (1.0 - fx) + c011 * fx
    c10 = c100 * (1.0 - fx) + c101 * fx
    c11 = c110 * (1.0 - fx) + c111 * fx

    c0 = c00 * (1.0 - fy) + c01 * fy
    c1 = c10 * (1.0 - fy) + c11 * fy

    return c0 * (1.0 - fz) + c1 * fz


def collect_vdb_files(input_path):
    if os.path.isfile(input_path) and input_path.lower().endswith(".vdb"):
        return [input_path]
    if os.path.isdir(input_path):
        files = sorted(glob.glob(os.path.join(input_path, "*.vdb")))
        if not files:
            # Búsqueda recursiva si los .vdb están en una subcarpeta interna
            files = sorted(glob.glob(os.path.join(input_path, "**", "*.vdb"), recursive=True))
        return files
    return []


def main():
    parser = argparse.ArgumentParser(
        description="Convert OpenVDB smoke/fire/cloud files to 2-channel 3D textures (.raw + metadata.json)"
    )
    parser.add_argument(
        "--input", "--input-dir",
        dest="input_path",
        type=str,
        default="scenes/SmallCampfireVDB",
        help="Directory containing .vdb sequence or path to a single .vdb file",
    )
    parser.add_argument(
        "--output", "--output-dir",
        dest="output_dir",
        type=str,
        default="assets/volumes",
        help="Output directory for .raw files and metadata.json",
    )
    parser.add_argument(
        "--res",
        type=int,
        default=128,
        help="Dense 3D grid resolution per dimension (default: 128 -> 128x128x128)",
    )
    parser.add_argument(
        "--max-frames", "--frames",
        dest="max_frames",
        type=int,
        default=0,
        help="Max frames to export (0 = ALL frames, default: 0)",
    )
    parser.add_argument(
        "--format",
        type=str,
        choices=["uint8", "float16"],
        default="uint8",
        help="Data type for 2-channel raw output: uint8 (4 MB/frame, GL_RG8) or float16 (8 MB/frame, GL_RG16F)",
    )
    parser.add_argument(
        "--no-swizzle",
        action="store_true",
        help="Keep original VDB axes instead of swizzling Z-up (Houdini/EmberGen) to Y-up (OpenGL)",
    )
    parser.add_argument(
        "--density-scale",
        type=float,
        default=1.0,
        help="Multiplier scale for density channel normalization",
    )
    parser.add_argument(
        "--temp-scale",
        type=float,
        default=1.0,
        help="Multiplier scale for temperature channel normalization",
    )

    args = parser.parse_args()

    vdb_files = collect_vdb_files(args.input_path)
    if not vdb_files:
        sys.stderr.write(f"ERROR: No .vdb files found in '{args.input_path}'\n")
        sys.exit(1)

    total_found = len(vdb_files)
    if args.max_frames > 0 and args.max_frames < total_found:
        if args.max_frames == 1:
            selected_indices = [0]
        else:
            indices = np.linspace(0, total_found - 1, args.max_frames, endpoint=True, dtype=int)
            selected_indices = sorted(list(set(indices)))
    else:
        selected_indices = list(range(total_found))

    print(f"[convert_vdb] Found {total_found} .vdb file(s) in '{args.input_path}'")
    print(f"[convert_vdb] Exporting {len(selected_indices)} frame(s) (100% sequence)...")

    # Pass 1: Global active bounding box and max scalar ranges across all selected frames
    print("[convert_vdb] Pass 1/2: Computing global bounding box and value ranges...")
    global_min = [float("inf")] * 3
    global_max = [float("-inf")] * 3
    global_max_density = 0.0
    global_max_temp = 0.0

    t_start = time.time()
    for idx in selected_indices:
        grids, _ = vdb.readAll(vdb_files[idx])
        d_grid = find_grid(grids, DENSITY_CANDIDATES, fallback_first=True)
        t_grid = find_grid(grids, TEMP_CANDIDATES, fallback_first=False)

        for is_temp, g in [(False, d_grid), (True, t_grid)]:
            if g is None:
                continue
            min_c, max_c = g.evalActiveVoxelBoundingBox()
            if min_c[0] > max_c[0]:
                continue
            for d in range(3):
                global_min[d] = min(global_min[d], min_c[d])
                global_max[d] = max(global_max[d], max_c[d])

            if hasattr(g, "evalMinMax"):
                _, v_max = g.evalMinMax()
                v_max = float(v_max)
            else:
                dim = (max_c[0] - min_c[0] + 1, max_c[1] - min_c[1] + 1, max_c[2] - min_c[2] + 1)
                tmp = np.zeros(dim, dtype=np.float32)
                g.copyToArray(tmp, ijk=min_c)
                v_max = float(tmp.max())

            if is_temp:
                global_max_temp = max(global_max_temp, v_max)
            else:
                global_max_density = max(global_max_density, v_max)

    if global_min[0] == float("inf"):
        sys.stderr.write("ERROR: All VDB grids appear to be empty.\n")
        sys.exit(1)

    global_min = [int(global_min[d]) - 1 for d in range(3)]
    global_max = [int(global_max[d]) + 1 for d in range(3)]
    span_vdb = [global_max[d] - global_min[d] + 1 for d in range(3)]

    print(f"[convert_vdb] Global VDB Bounding Box: min={global_min}, max={global_max}, span={span_vdb}")
    print(f"[convert_vdb] Global Raw Maxima: density={global_max_density:.6f}, flame/temp={global_max_temp:.4f}")

    if args.no_swizzle:
        max_dim = float(max(span_vdb))
        aspect_x = float(span_vdb[0]) / max_dim
        aspect_y = float(span_vdb[1]) / max_dim
        aspect_z = float(span_vdb[2]) / max_dim
    else:
        max_height = float(span_vdb[2])
        aspect_x = float(span_vdb[0]) / max_height
        aspect_y = 1.0
        aspect_z = float(span_vdb[1]) / max_height

    aspect_ratio = [aspect_x, aspect_y, aspect_z]
    print(f"[convert_vdb] OpenGL Box Aspect Ratio (X, Y_up, Z): {[round(a, 4) for a in aspect_ratio]}")

    os.makedirs(args.output_dir, exist_ok=True)
    # Limpiar archivos .raw antiguos en el directorio de salida para evitar residuos
    for old_raw in glob.glob(os.path.join(args.output_dir, "*.raw")):
        os.remove(old_raw)

    target_dim = (args.res, args.res, args.res)
    norm_density_div = max(global_max_density, 1e-6)
    norm_temp_div = max(global_max_temp, 1e-6)
    has_temperature = global_max_temp > 1e-6

    # Pass 2: Extract, swizzle, resample, normalize, and write each frame
    print("[convert_vdb] Pass 2/2: Resampling and writing .raw frames...")
    frame_files = []
    vdb_dim = (span_vdb[0], span_vdb[1], span_vdb[2])

    for out_idx, src_idx in enumerate(selected_indices):
        fn = vdb_files[src_idx]
        t0 = time.time()

        grids, _ = vdb.readAll(fn)
        density_grid = find_grid(grids, DENSITY_CANDIDATES, fallback_first=True)
        temp_grid = find_grid(grids, TEMP_CANDIDATES, fallback_first=False)

        raw_density = np.zeros(vdb_dim, dtype=np.float32)
        if density_grid is not None:
            density_grid.copyToArray(raw_density, ijk=tuple(global_min))

        raw_temp = np.zeros(vdb_dim, dtype=np.float32)
        if temp_grid is not None:
            temp_grid.copyToArray(raw_temp, ijk=tuple(global_min))

        if args.no_swizzle:
            density_gl = np.transpose(raw_density, (2, 1, 0))
            temp_gl = np.transpose(raw_temp, (2, 1, 0))
        else:
            density_gl = np.transpose(raw_density, (1, 2, 0))
            temp_gl = np.transpose(raw_temp, (1, 2, 0))

        resampled_density = resample_3d(density_gl, target_dim)
        resampled_temp = resample_3d(temp_gl, target_dim) if has_temperature else np.zeros(target_dim, dtype=np.float32)

        norm_density = np.clip((resampled_density / norm_density_div) * args.density_scale, 0.0, 1.0)
        norm_temp = np.clip((resampled_temp / norm_temp_div) * args.temp_scale, 0.0, 1.0) if has_temperature else resampled_temp

        packed = np.stack([norm_density, norm_temp], axis=-1)

        out_name = f"frame_{out_idx:04d}.raw"
        out_path = os.path.join(args.output_dir, out_name)

        if args.format == "uint8":
            packed_data = (packed * 255.0).astype(np.uint8)
        else:
            packed_data = packed.astype(np.float16)

        with open(out_path, "wb") as f:
            f.write(packed_data.tobytes())

        frame_files.append(out_name)
        dt = time.time() - t0
        if (out_idx + 1) % 10 == 0 or out_idx == 0 or out_idx == len(selected_indices) - 1:
            print(
                f"  [Frame {out_idx + 1:03d}/{len(selected_indices):03d}] "
                f"({os.path.basename(fn)}) -> {out_name} ({dt:.2f}s)"
            )

    bytes_per_voxel = 2 if args.format == "uint8" else 4
    total_mb = (len(frame_files) * (args.res ** 3) * bytes_per_voxel) / (1024 * 1024)

    metadata = {
        "grid_resolution": [args.res, args.res, args.res],
        "channels": 2,
        "channel_names": ["density", "temperature"],
        "has_temperature": has_temperature,
        "format": args.format,
        "gl_internal_format": "GL_RG8" if args.format == "uint8" else "GL_RG16F",
        "gl_format": "GL_RG",
        "gl_type": "GL_UNSIGNED_BYTE" if args.format == "uint8" else "GL_HALF_FLOAT",
        "num_frames": len(frame_files),
        "total_size_mb": round(total_mb, 2),
        "aspect_ratio": [round(a, 4) for a in aspect_ratio],
        "box_min": [round(-aspect_x * 0.5, 4), 0.0, round(-aspect_z * 0.5, 4)],
        "box_max": [round(aspect_x * 0.5, 4), round(aspect_y, 4), round(aspect_z * 0.5, 4)],
        "max_density_raw": float(global_max_density),
        "max_temperature_raw": float(global_max_temp),
        "frames": frame_files,
    }

    meta_path = os.path.join(args.output_dir, "metadata.json")
    with open(meta_path, "w") as f:
        json.dump(metadata, f, indent=2)

    total_time = time.time() - t_start
    print(f"\n[convert_vdb] Converted all {len(frame_files)} frames in {total_time:.2f}s!")
    print(f"[convert_vdb] Output: {meta_path} (Total RAM size: {total_mb:.2f} MB)")


if __name__ == "__main__":
    main()
