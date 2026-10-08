#!/usr/bin/env python3
# Copyright (c) 2012- PPSSPP Project.

# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, version 2.0 or later versions.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License 2.0 for more details.

# A copy of the GPL 2.0 should have been included with the program.
# If not, see http://www.gnu.org/licenses/

# Official git repository and contact information can be found at
# https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

"""Compare screenshots from different renderers (a hardware backend, the software renderer, a PSP capture)
without counting what any two rasterizers disagree on.

A plain pixel diff between Vulkan and the software renderer counts thousands of pixels for edges a pixel
apart and interpolation rounding, which hides a missing effect or wrong lighting. Here a difference counts
only if it survives three filters:

- Neighborhood: a pixel matches if each channel is within the range the other image's pixels within
  --radius span, widened by --tolerance. That takes in edges a pixel off and the values between
  neighbors that different filtering and interpolation give. Checked both ways, so a thin object present
  in only one image still shows.
- Area: a mismatching pixel counts only if at least --min-neighbors of the 3x3 block around it (itself
  included) mismatch too. Shifted edges leave lines a pixel wide and scattered speckle, which drop out; a
  wrong surface is an area.
- Low frequency, separately: both images averaged over --block x --block blocks, a block counting when an
  average differs by more than --block-tolerance. This catches a broad shift, like lighting too dark
  everywhere, that the neighborhood ranges let through.

Prints, per image pair, the area pixels, the low-frequency blocks (0 and 0 means equivalent) and the mean
difference per channel (B - A: a bias from color conversion, which 16-bit framebuffers often show), and with
--heatmaps writes each pair side by side with the counted pixels in red and the blocks in yellow.

Examples:
    python3 Tools/image_compare.py vulkan.png softgpu.png
    python3 Tools/image_compare.py out_vulkan/ out_soft/ --heatmaps heat/ --sort
"""

import argparse
import concurrent.futures
import itertools
import os
import sys

import numpy as np
from PIL import Image


def load(path):
    return np.asarray(Image.open(path).convert('RGB')).astype(np.int16)


def shifted(img, dy, dx):
    # img moved by (dy, dx), edges repeated.
    h, w = img.shape[:2]
    ys = np.clip(np.arange(h) - dy, 0, h - 1)
    xs = np.clip(np.arange(w) - dx, 0, w - 1)
    return img[ys][:, xs]


def unmatched(a, b, radius, tolerance):
    # True where a's pixel is outside the range b's pixels within radius span, widened by tolerance, on
    # some channel. A range rather than single pixels, so values between neighbors (filtering and
    # interpolation that differ) match too.
    lo = hi = b
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            s = shifted(b, dy, dx)
            lo = np.minimum(lo, s)
            hi = np.maximum(hi, s)
    return ((a < lo - tolerance) | (a > hi + tolerance)).any(axis=2)


def box_count(mask):
    # Mismatching pixels in each pixel's 3x3 block, itself included.
    m = mask.astype(np.int8)
    total = np.zeros(mask.shape, np.int16)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            total += shifted(m, dy, dx)
    return total


def block_means(img, block):
    h, w = img.shape[0] // block * block, img.shape[1] // block * block
    v = img[:h, :w].astype(np.float32).reshape(h // block, block, w // block, block, 3)
    return v.mean(axis=(1, 3))


def compare(a, b, args):
    h, w = min(a.shape[0], b.shape[0]), min(a.shape[1], b.shape[1])
    if args.crop:
        h, w = min(h, args.crop[1]), min(w, args.crop[0])
    a, b = a[:h, :w], b[:h, :w]
    mismatch = unmatched(a, b, args.radius, args.tolerance) | unmatched(b, a, args.radius, args.tolerance)
    area = mismatch & (box_count(mismatch) >= args.min_neighbors)
    blocks = np.abs(block_means(a, args.block) - block_means(b, args.block)).max(axis=2) > args.block_tolerance
    bias = (b.astype(np.float32) - a).reshape(-1, 3).mean(axis=0)
    return a, b, area, blocks, bias


def heatmap(path, a, b, area, blocks, block):
    out = (np.concatenate([a, b], axis=1) // 2).astype(np.uint8)
    mark = np.zeros(a.shape[:2], np.uint8)
    by, bx = np.nonzero(blocks)
    for y, x in zip(by, bx):
        mark[y * block:(y + 1) * block, x * block:(x + 1) * block] = 1
    mark[area] = 2
    diff = (a // 4).astype(np.uint8)
    diff[mark == 1] = (200, 200, 0)
    diff[mark == 2] = (255, 0, 0)
    Image.fromarray(np.concatenate([out, diff], axis=1)).save(path)


def pairs(a, b):
    if os.path.isdir(a) and os.path.isdir(b):
        names = sorted(n for n in os.listdir(a) if n.lower().endswith('.png') and os.path.exists(os.path.join(b, n)))
        return [(n, os.path.join(a, n), os.path.join(b, n)) for n in names]
    return [(os.path.basename(a), a, b)]


def compare_pair(pair, args):
    name, pa, pb = pair
    try:
        a, b, area, blocks, bias = compare(load(pa), load(pb), args)
    except Exception as e:
        print('%s: %s' % (name, e), file=sys.stderr)
        return None
    if args.heatmaps and (area.any() or blocks.any()):
        heatmap(os.path.join(args.heatmaps, name), a, b, area, blocks, args.block)
    return name, int(area.sum()), int(blocks.sum()), int(blocks.size), bias


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('a', help='image, or directory of PNGs')
    ap.add_argument('b', help='image, or directory with PNGs of the same names')
    ap.add_argument('--tolerance', type=int, default=12, help='per-channel difference a matching pixel may have (default 12)')
    ap.add_argument('--radius', type=int, default=1, help='how far to look for a matching pixel (default 1)')
    ap.add_argument('--min-neighbors', type=int, default=5, help='mismatching pixels a 3x3 block needs for its center to count (default 5)')
    ap.add_argument('--block', type=int, default=8, help='low-frequency block size (default 8)')
    ap.add_argument('--block-tolerance', type=float, default=8.0, help='per-channel difference of block averages (default 8)')
    ap.add_argument('--crop', type=int, nargs=2, metavar=('W', 'H'), default=(480, 272), help='compare only the top left W x H (default 480 272, the PSP screen)')
    ap.add_argument('--heatmaps', metavar='DIR', help='write NAME.png side by side: A, B, and the counted differences')
    ap.add_argument('--sort', action='store_true', help='list the most different first')
    ap.add_argument('--all', action='store_true', help='list equivalent pairs too')
    ap.add_argument('-j', type=int, default=os.cpu_count(), help='pairs compared at once (default: all cores)')
    args = ap.parse_args()

    if args.heatmaps:
        os.makedirs(args.heatmaps, exist_ok=True)
    with concurrent.futures.ProcessPoolExecutor(args.j) as ex:
        results = [r for r in ex.map(compare_pair, pairs(args.a, args.b), itertools.repeat(args), chunksize=4) if r]
    if args.sort:
        results.sort(key=lambda r: (-r[1], -r[2]))
    differing = 0
    for name, area, blocks, nblocks, bias in results:
        if area or blocks:
            differing += 1
        if area or blocks or args.all:
            print('%7d px %5d/%d blocks  bias %+5.1f %+5.1f %+5.1f  %s' % (area, blocks, nblocks, bias[0], bias[1], bias[2], name))
    print('%d compared, %d differ' % (len(results), differing))


if __name__ == '__main__':
    main()
