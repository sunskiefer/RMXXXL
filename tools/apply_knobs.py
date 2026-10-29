#!/usr/bin/env python3
"""Replace the skin's drawn knob filmstrips with RMXXXL's knob art.

    python3 tools/apply_knobs.py "<skin dir>/Plugin Skins"

gen_vst.py writes one filmstrip per knob radius (sh_knob_r<R>.png: 128 square frames of 2R+10 px, stacked down, on the
page colour). This rebuilds each from art/:
  - art/knob_tick.png         the scale (ticks), under every knob, in place of the drawn orange dot ring
  - art/knob_black_strip.png  101 frames, 150 px: the main controls (radius >= 55 in layout.conf)
  - art/knob_grey_strip.png   101 frames, 80 px: everything else (radius < 55)
The knob is scaled to sit just inside the ticks. Frames are resampled 101 -> 128, minimum first, as MPC expects.
"""
import os
import sys

import numpy as np
from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
ART = os.path.join(ROOT, "art")
FRAMES = 128
PAGE = (0, 0, 0)          # layout.conf theme_panel: the strips are opaque, drawn on the page colour
TICK_DOT_IN = 28.0        # inner radius of the tick dots in art/knob_tick.png (80 px): the ring runs 28..38.4
MAIN_RADIUS = 58          # radius >= this -> BLACK (layout.conf: main knobs r=58, others r=57). r=59 (a 16384 px strip) drifted on a Force (1.2.0): r=58 (16128 px) is the largest used (Meta4MPCXXL's biggest); 1.1.1 verified r=56 (15616 px)


def body_radius(strip, size):
    """Radius of the knob's solid body around the frame centre (the art is centred), from the middle frame."""
    mid = strip.crop((0, 50 * size, size, 51 * size))
    a = np.array(mid)[:, :, 3]
    ys, xs = np.nonzero(a > 200)
    c = (size - 1) / 2.0
    return float(np.max(np.hypot(xs - c, ys - c)))


def bezel_radius(strip, size):
    """Radius of the knob's round bezel (the lower half of the middle frame, where the pointer isn't): the pointer
    sticks out past it, so body_radius() alone makes the bezel about 12 % smaller than the room it has."""
    mid = strip.crop((0, 50 * size, size, 51 * size))
    a = np.array(mid)[:, :, 3]
    ys, xs = np.nonzero(a > 200)
    c = (size - 1) / 2.0
    low = ys >= c
    return float(np.max(np.hypot(xs[low] - c, ys[low] - c)))


def tick_inner_radius(tick):
    a = np.array(tick)[:, :, 3]
    ys, xs = np.nonzero(a > 60)
    c = (tick.size[0] - 1) / 2.0
    r = np.hypot(xs - c, ys - c)
    r = r[r > tick.size[0] * 0.25]   # ignore any stray pixel near the centre
    return float(r.min()), float(r.max())


def build(path, radius):
    old = Image.open(path)
    frame = old.size[0]
    assert old.size[1] == frame * FRAMES, "%s: expected %d square frames" % (path, FRAMES)
    style = "black" if radius >= MAIN_RADIUS else "grey"
    strip = Image.open(os.path.join(ART, "knob_%s_strip.png" % style)).convert("RGBA")
    src = strip.size[0]
    n_src = strip.size[1] // src
    tick = Image.open(os.path.join(ART, "knob_tick.png")).convert("RGBA")
    t_in, t_out = tick_inner_radius(tick)

    # tick ring fills the frame (its outer edge 1 px in); the knob sits 1.5 px inside the ticks' inner edge
    t_scale = (frame / 2.0 - 1.0) / t_out
    tick_px = max(1, int(round(tick.size[0] * t_scale)))
    tick_img = tick.resize((tick_px, tick_px), Image.LANCZOS)
    # 1.3: the bezel (not the pointer tip) reaches the inside of the tick dots, so the knob is as big as the frame
    # allows (the frame is capped at r=58 by MPC's filmstrip limit). The dots sit between TICK_DOT_IN and the outer edge.
    knob_r = TICK_DOT_IN * t_scale - 2.0
    k_scale = knob_r / bezel_radius(strip, src)
    knob_px = max(1, int(round(src * k_scale)))

    out = Image.new("RGB", (frame, frame * FRAMES), PAGE)
    for i in range(FRAMES):
        j = int(round(i * (n_src - 1) / float(FRAMES - 1)))
        k = strip.crop((0, j * src, src, (j + 1) * src)).resize((knob_px, knob_px), Image.LANCZOS)
        cell = Image.new("RGBA", (frame, frame), PAGE + (255,))
        cell.alpha_composite(tick_img, ((frame - tick_px) // 2, (frame - tick_px) // 2))
        cell.alpha_composite(k, ((frame - knob_px) // 2, (frame - knob_px) // 2))
        out.paste(cell.convert("RGB"), (0, i * frame))
    out.save(path, optimize=True)
    print("%s: %s knob, %d px frames, knob %d px inside the ticks" % (os.path.basename(path), style, frame, knob_px))


def main():
    skin = sys.argv[1]
    found = False
    for name in sorted(os.listdir(skin)):
        if name.startswith("sh_knob_r") and name.endswith(".png") and name[9:-4].isdigit():
            build(os.path.join(skin, name), int(name[9:-4]))
            found = True
    if not found:
        raise SystemExit("apply_knobs: no sh_knob_r<R>.png in " + skin)


if __name__ == "__main__":
    main()
