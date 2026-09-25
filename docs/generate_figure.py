"""
Generate the ASCII<->binary round-trip figure used in the top-level README.

Loads the Stanford Bunny (docs/sample_meshes/bunny.obj -- the classic
Stanford Computer Graphics Laboratory scanning-repository test mesh, widely
redistributed for research/testing use) via objrw_io.readOBJ, writes it out
as compact binary .objrw, reads *that* back, and plots the original vs.
round-tripped mesh side-by-side to visually confirm the round-trip is
lossless (the same guarantee checked numerically by tests [3]/[4]/[5] in
tests/objrw_tests.cpp and the Python test suite).

Run against a built library:

    OBJRW_LIBRARY_PATH=../build_cpu/libobjrw.so python docs/generate_figure.py
"""
import os
import sys

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(_HERE)
sys.path.insert(0, os.path.join(_ROOT, "python"))

import objrw_io  # noqa: E402

OUT_DIR = os.path.join(_HERE, "images")
BUNNY_OBJ = os.path.join(_HERE, "sample_meshes", "bunny.obj")
os.makedirs(OUT_DIR, exist_ok=True)


def plot_mesh(ax, v, t, title, color):
    coll = Poly3DCollection(v[t], facecolor=color, edgecolor="k", linewidths=0.05, alpha=0.95)
    ax.add_collection3d(coll)
    ax.set_title(f"{title}\n{len(v)} verts, {len(t)} tris", fontsize=10, pad=14)
    lo, hi = v.min(axis=0), v.max(axis=0)
    ctr, half = (lo + hi) / 2.0, (hi - lo).max() / 2.0
    ax.set_xlim(ctr[0] - half, ctr[0] + half)
    ax.set_ylim(ctr[1] - half, ctr[1] + half)
    ax.set_zlim(ctr[2] - half, ctr[2] + half)
    ax.set_box_aspect((1, 1, 1))
    ax.view_init(elev=15, azim=110)
    ax.set_axis_off()


def main():
    m1 = objrw_io.readOBJ(BUNNY_OBJ)           # ASCII .obj -> Mesh
    v = np.array(m1.positions, dtype=np.float32)
    t = np.array([tri[:3] for tri in m1.triangles], dtype=np.int32)

    binary_path = os.path.join(OUT_DIR, "_bunny_roundtrip.objrw")
    objrw_io.writeOBJ(m1, binary_path, flagBinary=True)   # Mesh -> binary
    m2 = objrw_io.readOBJ(binary_path)                    # binary -> Mesh (auto-detected)

    v2 = np.array(m2.positions, dtype=np.float32)
    t2 = np.array([tri[:3] for tri in m2.triangles], dtype=np.int32)

    identical = (v2.shape == v.shape and t2.shape == t.shape
                 and np.allclose(v2, v, atol=1e-6) and np.array_equal(t2, t))

    fig = plt.figure(figsize=(9, 5))
    ax1 = fig.add_subplot(1, 2, 1, projection="3d")
    ax2 = fig.add_subplot(1, 2, 2, projection="3d")
    plot_mesh(ax1, v, t, "Original (bunny.obj, ASCII)", "#4C72B0")
    plot_mesh(ax2, v2, t2, "After .obj -> .objrw -> .obj round-trip", "#55A868")
    fig.suptitle(f"objrw round-trip on the Stanford Bunny: "
                 f"{'IDENTICAL' if identical else 'MISMATCH'}", fontsize=12)
    fig.subplots_adjust(top=0.85, wspace=0.05)
    out = os.path.join(OUT_DIR, "objrw_roundtrip.png")
    fig.savefig(out, dpi=140, bbox_inches="tight")
    print("wrote", out, "| identical:", identical)

    os.remove(binary_path)


if __name__ == "__main__":
    main()
