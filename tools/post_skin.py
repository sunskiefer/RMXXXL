#!/usr/bin/env python3
"""RMXXXL skin finishing, run by build.sh after gen_vst.py (and apply_knobs.py):

    python3 tools/post_skin.py "<skin dir>/Plugin Skins" layout.conf

MPC draws knob/toggle names and every value in Titillium Web at 21 px (layout.conf scale_names=1). Text baked into
the images by the default renderer is a small spaced bitmap font, so this redraws it in Titillium, the same size:
  - buttons:   every sh_btn_* image, label centred at 21 px; at least BTN_H tall (bounds in TUI.json grow to match)
  - pads:      buttons whose label is a single digit (the PADS page) become PAD_W x PAD_H pads, number at 44 px
  - segments:  every sh_seg_<key>_<i>_{on,off} option image, its option name at the same size
  - toggles:   a big ON / OFF pill (TOGGLE_W x TOGGLE_H), lit when on, the name under it
  - lists:     every popup option image (sh_po_*), the option name at the same size
  - labels:    `#@text tab="<tab>" cx= cy= [size=21] [color=hex] [align=center|left] label="..."` comment lines in
               layout.conf, drawn onto that tab's background (the generator ignores comments). Popups and readouts
               carry label="" and get their label this way.
"""
import json
import os
import re
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
FONT = os.path.join(ROOT, "art", "fonts", "TitilliumWeb-SemiBold.ttf")
SHADOW_Y = 86            # layout.conf y -> skin y
TEXT_PX = 21             # MPC's live text: 21 px x label_scale (scale_names=1); set from layout.conf in main()
BTN_H = 60               # 1.2: bigger buttons (Force test: too small to hit)
BTN_W = 150
TOGGLE_W, TOGGLE_H = 150, 56   # toggles: a lit pill with ON / OFF, the name under it
PAD_W, PAD_H = 250, 110
ACCENT, ACCENT_HI, INK, INK_DIM = "ff8a1f", "ffb15c", "ececec", "9a9a9a"


BTN_SIZES = {}   # key -> (w, h), from "#@btn key=<param> w= h=" lines in layout.conf


def font(px):
    return ImageFont.truetype(FONT, px)


def text_center(dr, cx, cy, text, px, color):
    f = font(px)
    b = dr.textbbox((0, 0), text, font=f)
    dr.text((cx - (b[2] - b[0]) / 2 - b[0], cy - (b[3] - b[1]) / 2 - b[1]), text, font=f, fill="#" + color)


def parse_attrs(line):
    return {k: (v1 if v1 != "" else v2) for k, v1, v2 in re.findall(r'(\w+)=(?:"([^"]*)"|(\S+))', line)}


def tab_names(layout):
    return [m.group(1).strip() for m in re.finditer(r"^\[tab ([^\]]+)\]", layout, re.M)]


def draw_button(path, w, h, label, pad):
    if pad:
        off = Image.new("RGB", (w, h), (0, 0, 0))
        dr = ImageDraw.Draw(off)
        dr.rounded_rectangle((1, 1, w - 2, h - 2), radius=10, fill="#161616", outline="#3a3a3a", width=2)
        text_center(dr, w / 2, h / 2, label, 44, INK)
        on = Image.new("RGB", (w, h), (0, 0, 0))
        dr = ImageDraw.Draw(on)
        dr.rounded_rectangle((1, 1, w - 2, h - 2), radius=10, fill="#" + ACCENT, outline="#" + ACCENT_HI, width=2)
        text_center(dr, w / 2, h / 2, label, 44, "111111")
    else:
        fill, fill_hi, ink = (("d23a2a", "ff5a48", "ffffff") if label == "PANIC" else (ACCENT, ACCENT_HI, "111111"))
        px = TEXT_PX + 6 if h >= 64 else TEXT_PX   # the big performance buttons read from further away
        off = Image.new("RGB", (w, h), (0, 0, 0))
        dr = ImageDraw.Draw(off)
        dr.rounded_rectangle((0, 0, w - 1, h - 1), radius=8, fill="#" + fill)
        text_center(dr, w / 2, h / 2, label, px, ink)
        on = Image.new("RGB", (w, h), (0, 0, 0))
        dr = ImageDraw.Draw(on)
        dr.rounded_rectangle((0, 0, w - 1, h - 1), radius=8, fill="#" + fill_hi)
        text_center(dr, w / 2, h / 2, label, px, ink)
    off.save(path + "_off.png")
    on.save(path + "_on.png")


def set_bounds(comp_list, w, h):
    for c in comp_list:
        c["bounds"]["bounds"] = "0 0 %d %d" % (w, h)


def fix_buttons(skin, tui):
    loc = tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
    defs = {d["key"]: d for d in loc}
    # labels and placements of every button definition
    placements = {}
    for d in loc:
        for c in d["value"].get("componentsData", []):
            t = c["componentData"].get("type")
            if t in defs and t.startswith("shTrig_"):
                placements.setdefault(t, []).append(c)
    n = 0
    for key, places in placements.items():
        d = defs[key]
        comps = d["value"]["componentsData"]
        btn = [c for c in comps if c["componentData"]["type"] == "Button"][0]
        label = places[0]["componentData"]["name"]
        _, _, w, h = (int(v) for v in btn["bounds"]["bounds"].split())
        pad = label.isdigit()
        big = next((v for k, v in BTN_SIZES.items() if key.startswith("shTrig_%s_" % k)), None)   # "#@btn" lines
        f = font(TEXT_PX)
        tw = int(ImageDraw.Draw(Image.new("RGB", (1, 1))).textlength(label, font=f))
        nw, nh = (PAD_W, PAD_H) if pad else big if big else (max(w, tw + 40, BTN_W), max(h, BTN_H))
        set_bounds(comps, nw, nh)
        for c in places:   # keep each placement centred where the layout put it
            x, y, pw, ph = (int(v) for v in c["bounds"]["bounds"].split())
            cx, cy = x + pw // 2, y + ph // 2
            c["bounds"]["bounds"] = "%d %d %d %d" % (cx - nw // 2, cy - nh // 2, nw, nh)
        img = btn["componentData"]["data"]["offImage"][:-len("_off.png")]
        draw_button(os.path.join(skin, img), nw, nh, label, pad)
        n += 1
    return n


def fix_segments(skin, layout, params):
    global TEXT_PX
    opts = {p["key"]: p.get("options") for p in params["params"]}
    for line in layout.splitlines():
        if line.startswith(("enum_h", "enum_v")):
            a = parse_attrs(line)
            if "options" in a:
                opts[a["key"]] = a["options"].split(",")
    n = 0
    for name in os.listdir(skin):
        m = re.match(r"sh_seg_(.+)_(\d+)_(on|off)\.png$", name)
        if not m or m.group(1) not in opts or not opts[m.group(1)]:
            continue
        label = opts[m.group(1)][int(m.group(2))].upper()
        path = os.path.join(skin, name)
        im = Image.open(path).convert("RGB")
        w, h = im.size
        on = m.group(3) == "on"
        out = Image.new("RGB", (w, h), (0, 0, 0))
        dr = ImageDraw.Draw(out)
        dr.rounded_rectangle((0, 1, w - 1, h - 2), radius=4, fill="#" + ACCENT if on else "#161616",
                             outline=None if on else "#2a2a2a")
        text_center(dr, w / 2, h / 2, label, min(TEXT_PX, h - 12), "111111" if on else INK)
        out.save(path)
        n += 1
    return n


def fix_toggles(skin, tui):
    """Toggles: the drawn switch is a small pill (53 x 29). Make it a TOGGLE_W x TOGGLE_H pill reading ON / OFF, lit when
    on, with the name under it; the placements stay centred where the layout put them."""
    loc = tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
    keys = [d["key"] for d in loc if d["key"].startswith("shToggle")]
    if not keys:
        return 0
    bw = 0
    for d in loc:
        if d["key"] not in keys:
            continue
        comps = d["value"]["componentsData"]
        bw = max(bw, int(comps[0]["bounds"]["bounds"].split()[2]))
    W, H = max(bw, TOGGLE_W + 20), TOGGLE_H + 8 + 34
    images = set()
    for d in loc:
        if d["key"] not in keys:
            continue
        for c in d["value"]["componentsData"]:
            t = c["componentData"]["type"]
            if t == "Focus":
                c["bounds"]["bounds"] = "0 0 %d %d" % (W, H)
            elif t == "Button":
                c["bounds"]["bounds"] = "%d 4 %d %d" % ((W - TOGGLE_W) // 2, TOGGLE_W, TOGGLE_H)
                images.add((c["componentData"]["data"]["onImage"], c["componentData"]["data"]["offImage"]))
            elif t == "Label":
                c["bounds"]["bounds"] = "0 %d %d 32" % (TOGGLE_H + 8, W)
    for d in loc:   # placements
        for c in d["value"].get("componentsData", []):
            if c["componentData"].get("type") in keys:
                x, y, pw, ph = (int(v) for v in c["bounds"]["bounds"].split())
                cx, cy = x + pw // 2, y + ph // 2
                c["bounds"]["bounds"] = "%d %d %d %d" % (cx - W // 2, cy - H // 2, W, H)
    for on_img, off_img in images:
        for img, on in ((on_img, True), (off_img, False)):
            im = Image.new("RGB", (TOGGLE_W, TOGGLE_H), (0, 0, 0))
            dr = ImageDraw.Draw(im)
            dr.rounded_rectangle((1, 1, TOGGLE_W - 2, TOGGLE_H - 2), radius=10,
                                 fill="#" + (ACCENT if on else "161616"), outline="#" + (ACCENT_HI if on else "3a3a3a"), width=2)
            text_center(dr, TOGGLE_W / 2, TOGGLE_H / 2, "ON" if on else "OFF", TEXT_PX, "111111" if on else INK_DIM)
            im.save(os.path.join(skin, img))
    return len(keys)


def fix_popup_options(skin, tui, layout, params):
    """The open lists of popups: the generator draws their options in a small spaced bitmap font (unreadable on the
    Force). Redraw every option image in Titillium at the page's text size."""
    opts = {p["key"]: p.get("options") for p in params["params"]}
    for line in layout.splitlines():
        if line.startswith("popup"):
            a = parse_attrs(line)
            if "options" in a:
                opts[a["key"]] = a["options"].split(",")
    loc = tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
    done = set()
    for d in loc:
        m = re.match(r"shPopOpt_\d+_(.+)_(\d+)$", d["key"])
        if not m or not opts.get(m.group(1)):
            continue
        label = opts[m.group(1)][int(m.group(2))].upper()
        btn = [c for c in d["value"]["componentsData"] if c["componentData"]["type"] == "Button"][0]["componentData"]["data"]
        for img, on in ((btn["onImage"], True), (btn["offImage"], False)):
            if img in done:
                continue
            done.add(img)
            path = os.path.join(skin, img)
            w, h = Image.open(path).size
            out = Image.new("RGB", (w, h), (0, 0, 0))
            dr = ImageDraw.Draw(out)
            dr.rounded_rectangle((1, 1, w - 2, h - 2), radius=4, fill="#" + (ACCENT if on else "161616"))
            text_center(dr, w / 2, h / 2, label, min(TEXT_PX, h - 12), "111111" if on else INK)
            out.save(path)
    return len(done)


def bake_labels(skin, layout):
    tabs = tab_names(layout)
    images = {}
    n = 0
    for line in layout.splitlines():
        if not line.startswith("#@text"):
            continue
        a = parse_attrs(line)
        idx = tabs.index(a["tab"])
        path = os.path.join(skin, "sh_bg_%d.png" % idx)
        if path not in images:
            images[path] = Image.open(path).convert("RGB")
        dr = ImageDraw.Draw(images[path])
        px = int(float(a.get("size", TEXT_PX)))
        cx, cy = float(a["cx"]), float(a["cy"]) - SHADOW_Y
        color = a.get("color", INK_DIM)
        if a.get("align") == "left":
            f = font(px)
            b = dr.textbbox((0, 0), a["label"], font=f)
            dr.text((cx - b[0], cy - (b[3] - b[1]) / 2 - b[1]), a["label"], font=f, fill="#" + color)
        else:
            text_center(dr, cx, cy, a["label"], px, color)
        n += 1
    for path, im in images.items():
        im.save(path)
    return n


def main():
    skin, layout_path = sys.argv[1], sys.argv[2]
    layout = open(layout_path).read()
    global TEXT_PX
    m = re.search(r"^label_scale=([\d.]+)", layout, re.M)
    TEXT_PX = int(round(21 * float(m.group(1)))) if m else 21
    for line in layout.splitlines():
        if line.startswith("#@btn"):
            a = parse_attrs(line)
            BTN_SIZES[a["key"]] = (int(a["w"]), int(a["h"]))
    params = json.load(open(os.path.join(ROOT, "params.json")))
    tui_path = os.path.join(skin, "TUI.json")
    tui = json.load(open(tui_path))
    nb = fix_buttons(skin, tui)
    nt = fix_toggles(skin, tui)
    npop = fix_popup_options(skin, tui, layout, params)
    with open(tui_path, "w") as f:
        json.dump(tui, f, indent=2)
    ns = fix_segments(skin, layout, params)
    nl = bake_labels(skin, layout)
    print("post_skin: %d buttons, %d toggle kinds, %d list images, %d option images, %d labels in Titillium" % (nb, nt, npop, ns, nl))


if __name__ == "__main__":
    main()
