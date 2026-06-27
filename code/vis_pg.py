#!/usr/bin/env python3
"""
可视化 Path Guiding 的逐 iter 学习过程。
用法: python3 vis_pg.py output/pg_stats/
每个 pg_iter_N.txt 生成两张图:
  1) 空间热力图 (z-slice, 俯视图) — 每个格子累积的 radiance 权重
  2) 方向分布极坐标图 — 权重最高格子 (top-1) 的 theta-phi 直方图
"""

import sys, os, re
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.colors import LogNorm
from glob import glob

def parse_file(filepath):
    """解析 pg_iter_N.txt，返回 (meta, spatial, histos)"""
    meta = {}
    spatial = []   # list of (ix, iy, iz, weight)
    histos = []    # list of (ix, iy, iz, theta_res, phi_res, weight, bins_2d)

    with open(filepath) as f:
        section = None
        bins_2d = None
        theta_res = phi_res = 0
        ti = 0
        for line in f:
            line = line.strip()
            if not line:
                continue
            if line.startswith("# grid:"):
                parts = line.split()
                meta["grid"] = (int(parts[2]), int(parts[3]), int(parts[4]))
            elif line.startswith("# bbox_min:"):
                parts = line.split()
                meta["bbox_min"] = (float(parts[2]), float(parts[3]), float(parts[4]))
            elif line.startswith("# bbox_max:"):
                parts = line.split()
                meta["bbox_max"] = (float(parts[2]), float(parts[3]), float(parts[4]))
            elif line.startswith("# theta_bins:"):
                parts = line.split()
                meta["theta_bins"] = int(parts[2])
                meta["phi_bins"]   = int(parts[4])
            elif line == "SPATIAL":
                section = "spatial"
            elif line.startswith("HISTO"):
                section = "histo"
                parts = line.split()
                ix, iy, iz = int(parts[1]), int(parts[2]), int(parts[3])
                theta_res, phi_res = int(parts[4]), int(parts[5])
                weight = float(parts[6])
                bins_2d = np.zeros((theta_res, phi_res))
                ti = 0
                histos.append((ix, iy, iz, theta_res, phi_res, weight, bins_2d))
            elif section == "spatial":
                parts = line.split()
                spatial.append((int(parts[0]), int(parts[1]), int(parts[2]), float(parts[3])))
            elif section == "histo":
                vals = [float(x) for x in line.split()]
                if ti < theta_res:
                    bins_2d[ti, :] = vals[:phi_res]
                    ti += 1
    return meta, spatial, histos


def plot_spatial(meta, spatial, iter_num, outdir):
    """z-slice 空间热力图：在每个 z 层画一个 subplot"""
    gx, gy, gz = meta["grid"]
    bmin = meta["bbox_min"]
    bmax = meta["bbox_max"]

    # 构建 3D 权重数组
    vol = np.zeros((gz, gy, gx))
    for ix, iy, iz, w in spatial:
        vol[iz, iy, ix] = w

    fig, axes = plt.subplots(1, gz, figsize=(gz * 3.2, 3), squeeze=False)
    fig.suptitle(f"Iter {iter_num} — Spatial Weight Distribution (top-down view)", fontsize=12)

    for z in range(gz):
        ax = axes[0, z]
        z_center = bmin[2] + (z + 0.5) * (bmax[2] - bmin[2]) / gz
        im = ax.imshow(vol[z, :, :].T, origin="lower", cmap="inferno",
                       extent=[bmin[0], bmax[0], bmin[1], bmax[1]],
                       norm=LogNorm(vmin=max(vol[vol > 0].min() or 1e-3, 1e-3),
                                     vmax=max(vol.max(), 1e-2)),
                       aspect="auto")
        ax.set_title(f"z={z_center:.1f}")
        ax.set_xlabel("x"); ax.set_ylabel("y")

    fig.colorbar(im, ax=axes[0, -1], fraction=0.046, pad=0.04, label="total weight")
    plt.tight_layout()
    outpath = os.path.join(outdir, f"iter_{iter_num:02d}_spatial.png")
    fig.savefig(outpath, dpi=100)
    plt.close(fig)
    return outpath


def plot_directional(histo, meta, iter_num, rank, outdir):
    """单个格子的方向分布极坐标图"""
    ix, iy, iz, tres, pres, weight, bins_2d = histo
    gx, gy, gz = meta["grid"]
    bmin = meta["bbox_min"]
    bmax = meta["bbox_max"]

    cell_cx = bmin[0] + (ix + 0.5) * (bmax[0] - bmin[0]) / gx
    cell_cy = bmin[1] + (iy + 0.5) * (bmax[1] - bmin[1]) / gy
    cell_cz = bmin[2] + (iz + 0.5) * (bmax[2] - bmin[2]) / gz

    if iter_num == -1:
        cell_cx, cell_cy, cell_cz = -0.4, 3.0, -1.4

    # theta ∈ [0, π/2] (半径), phi ∈ [0, 2π] (角度)
    theta_edges = np.linspace(0, np.pi / 2, tres + 1)
    phi_edges = np.linspace(0, 2 * np.pi, pres + 1)

    fig, ax = plt.subplots(1, 1, subplot_kw={"projection": "polar"}, figsize=(5, 5))
    fig.suptitle(f"Iter {iter_num} — Top-{rank+1} Cell ({cell_cx:.1f}, {cell_cy:.1f}, {cell_cz:.1f})", fontsize=11)

    # 径向 = θ, 角度 = φ
    TH, PH = np.meshgrid(theta_edges, phi_edges, indexing="ij")
    # 每个 bin 用其中心值
    vals = np.zeros((tres, pres))
    for ti in range(tres):
        for pi in range(pres):
            vals[ti, pi] = bins_2d[ti, pi]

    # pcolormesh: 需要在 θ 方向也重复最后一列
    pcm = ax.pcolormesh(PH, TH, vals, cmap="inferno", shading="auto",
                        norm=LogNorm(vmin=max(vals[vals > 0].min() or 1e-4, 1e-4),
                                      vmax=max(vals.max(), 1e-3)))
    ax.set_theta_zero_location("E")
    ax.set_theta_direction(1)
    ax.set_ylim(0, np.pi / 2)
    ax.set_yticks([0, np.pi / 6, np.pi / 3, np.pi / 2])
    ax.set_yticklabels(["0°", "30°", "60°", "90°"])
    ax.set_title(f"weight={weight:.2f}", pad=15, fontsize=9)
    fig.colorbar(pcm, ax=ax, fraction=0.046, pad=0.1, label="accumulated weight")

    plt.tight_layout()
    outpath = os.path.join(outdir, f"iter_{iter_num:02d}_dir_top{rank+1}.png" if isinstance(iter_num, int)
                            else f"iter_{iter_num}_dir_top{rank+1}.png")
    fig.savefig(outpath, dpi=100)
    plt.close(fig)
    return outpath


def main():
    stats_dir = sys.argv[1] if len(sys.argv) > 1 else "output/pg_stats"
    outdir = os.path.join(stats_dir, "vis")
    os.makedirs(outdir, exist_ok=True)

    files = sorted(glob(os.path.join(stats_dir, "pg_iter_*.txt")))
    if not files:
        print(f"No pg_iter_*.txt files found in {stats_dir}")
        return

    # 先处理初始状态（学习前，均匀分布）
    # 从第一个 iter 文件取 grid/dir 参数，构造均匀直方图作为对比基准
    if files:
        meta0, _, _ = parse_file(files[0])
        tres = meta0.get("theta_bins", 4)
        pres = meta0.get("phi_bins", 4)
        gx, gy, gz = meta0["grid"]
        dummy = np.ones((tres, pres))
        dummy_histo = (gx // 2, gy // 2, gz // 2, tres, pres, 0.0, dummy)
        f2 = plot_directional(dummy_histo, meta0, -1, 0, outdir)
        print(f"  {f2} - uniform baseline (before any learning)")

    cnt = 0
    for fp in files:
        if cnt % 10 != 0:
            cnt += 1
            continue
        cnt += 1
        m = re.search(r"pg_iter_(\d+)", fp)
        iter_num = int(m.group(1)) if m else 0
        print(f"Processing iter {iter_num} ...")

        meta, spatial, histos = parse_file(fp)
        if not spatial:
            continue

        f1 = plot_spatial(meta, spatial, iter_num, outdir)
        print(f"  {f1}")

        if histos:
            f2 = plot_directional(histos[0], meta, iter_num, 0, outdir)
            print(f"  {f2}")

    print(f"\nDone! Images saved to {outdir}/")


if __name__ == "__main__":
    main()
