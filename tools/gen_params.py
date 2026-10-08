#!/usr/bin/env python3
"""Single source of truth for RMXXXL's parameters.

    python3 tools/gen_params.py      -> params.json (for mpc-vst-plugins' gen_vst.py) and src/param_table.h (engine)

The list order IS the VST parameter index. MPC stores project values by index, so once a version is shared:
append only, never reorder or delete (CLAUDE.md "Parameters").
"""
import json
import os

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")


def knob(key, name, lo=0.0, hi=1.0, default=0.0, unit=None, **kw):
    p = {"key": key, "name": name, "min": float(lo), "max": float(hi), "default": float(default)}
    if unit:
        p["unit"] = unit
    p.update(kw)
    return p


def opt(key, name, options, default=0, **kw):
    # qlink_ticks (1.2): MPC sends a Q-Link turn as many small nudges, and each used to step a whole option, so a
    # short turn raced through a list (Force test of 1.1.1). Lists step once per 3 nudges, on/off switches per 2.
    p = {"key": key, "name": name, "options": list(options), "default": default,
         "qlink_ticks": 2 if len(options) == 2 else 3}
    p.update(kw)
    return p


def trig(key, name, **kw):
    p = {"key": key, "name": name, "min": 0.0, "max": 1.0, "momentary": True, "type": "trigger", "default": 0.0}
    p.update(kw)
    return p


ONOFF = ["Off", "On"]

# Each section is one row of 8 controls = one Q-Link bank; two sections per tab.
SECTIONS = [
    ("Scene", [
        knob("build_up", "Build Up"),
        knob("break_down", "Break Down"),
        knob("filter", "Filter", -1.0, 1.0, 0.0, dynamic_display=True),
        knob("filter_res", "Resonance", 0.0, 1.0, 0.25, dynamic_display=True),
        knob("echo_send", "Echo"),
        opt("echo_div", "Echo Beat", ["1/16", "1/8", "3/16", "1/4", "3/8", "1/2", "3/4", "1 Bar"], 3),
        opt("release_fx", "Release FX", ["Echo Out", "Vinyl Brake", "Backspin"], 1),
        opt("release", "Release", ONOFF, 0),   # 1.2: the kill switch (was a trigger, now Release FX Go)
    ]),
    ("Isolator", [
        knob("iso_low", "Low", default=0.5, dynamic_display=True),
        knob("iso_mid", "Mid", default=0.5, dynamic_display=True),
        knob("iso_high", "High", default=0.5, dynamic_display=True),
        opt("release_len", "Release Beats", ["1/2", "1", "2", "4"], 1),
        knob("echo_fb", "Echo Feedback", default=0.45),
        knob("scene_noise", "Riser Noise", default=0.5),
        knob("in_gain", "In Gain", -18.0, 6.0, 0.0, "dB"),
        knob("lim_ceiling", "Ceiling", -12.0, 0.0, -0.3, "dB"),
    ]),
    ("Clouds", [
        opt("cl_on", "Clouds", ONOFF, 0),
        opt("cl_mode", "Mode", ["Granular", "Stretch", "Looping Delay", "Spectral", "Oliverb", "Resonestor"], 2),
        knob("cl_position", "Position", default=0.5, dynamic_name=True),
        knob("cl_size", "Size", default=0.5, dynamic_name=True),
        knob("cl_pitch", "Pitch", -24.0, 24.0, 0.0, "st", dynamic_name=True),
        knob("cl_density", "Density", default=0.7, dynamic_name=True),
        knob("cl_texture", "Texture", default=0.5, dynamic_name=True),
        knob("cl_blend", "Blend", default=0.5, dynamic_name=True),
    ]),
    ("Clouds More", [
        knob("cl_spread", "Spread", default=0.5, dynamic_name=True),
        knob("cl_feedback", "Feedback", dynamic_name=True),
        knob("cl_reverb", "Cloud Verb", dynamic_name=True),
        opt("cl_freeze", "Freeze", ONOFF, 0),
        opt("cl_reverse", "Reverse", ONOFF, 0),
        opt("cl_quality", "Quality", ["16-bit Stereo", "16-bit Mono", "8-bit Stereo", "8-bit Mono"], 0),
        trig("cl_trigger", "Trigger"),
        knob("cl_scene", "Scene Depth", default=0.5),
    ]),
    ("Reverb", [
        opt("rv_type", "Type", ["Plate", "Room", "Hall", "Clouds"], 0),
        knob("rv_send", "Reverb"),
        knob("rv_decay", "Decay", default=0.65, dynamic_display=True),
        knob("rv_size", "Size", default=0.5, dynamic_display=True),
        knob("rv_tone", "Tone", default=0.6, dynamic_display=True),
        knob("rv_predelay", "Predelay", 0.0, 100.0, 10.0, "ms"),
        knob("rv_width", "Width", 50.0, 150.0, 100.0, "%"),
        knob("rv_scene", "Scene Depth", default=0.7),
    ]),
    ("X-Pad", [
        trig("pad_kick", "Pad 1"),
        trig("pad_snare", "Pad 2"),
        trig("pad_clap", "Pad 3"),
        trig("pad_hat", "Pad 4"),
        knob("pad_level", "Pad Level", default=0.7),
        knob("pad_tune", "Pad Tune", -12.0, 12.0, 0.0, "st", display="int"),
        opt("pad_roll", "Roll Beat", ["1/8", "1/8T", "1/16", "1/16T", "1/32"], 2),
        knob("pad_root", "MIDI Root", 0.0, 115.0, 36.0, display="int", dynamic_display=True),
    ]),
    ("Setup", [
        knob("iso_lo_freq", "Low/Mid Xover", 50.0, 500.0, 246.0, "Hz", display="int"),
        knob("iso_hi_freq", "Mid/High Xover", 1000.0, 8000.0, 2484.0, "Hz", display="int"),
        knob("build_hpf", "Build HPF Max", 200.0, 4000.0, 1500.0, "Hz", display="int"),
        knob("break_lpf", "Break LPF Min", 80.0, 2000.0, 300.0, "Hz", display="int"),
        knob("lim_drive", "Limiter Drive", 0.0, 18.0, 0.0, "dB"),
        knob("lim_release", "Limiter Release", 10.0, 500.0, 80.0, "ms", display="int"),
    ]),
    # 1.1: the PADS page. Edit Pad picks the pad the ADSR / Sound controls show and change (the engine keeps all four).
    ("Pad Edit", [
        opt("pad_sel", "Edit Pad", ["PAD 1", "PAD 2", "PAD 3", "PAD 4"], 0),
        knob("env_a", "Attack", default=0.0, dynamic_display=True),
        knob("env_d", "Decay", default=0.5, dynamic_display=True),
        knob("env_s", "Sustain", default=1.0, dynamic_display=True),
        knob("env_r", "Release", default=0.3, dynamic_display=True),
        opt("pad_sound", "Sound", ["BUILT-IN"] + ["SLOT %d" % i for i in range(1, 17)], 0),
        knob("pad_file", "Loaded Sound", dynamic_display=True),
        trig("pad_reload", "Reload Samples"),
    ]),
    ("Pad Rolls", [
        opt("roll_1", "Roll 1", ONOFF, 0),
        opt("roll_2", "Roll 2", ONOFF, 0),
        opt("roll_3", "Roll 3", ONOFF, 0),
        opt("roll_4", "Roll 4", ONOFF, 0),
    ]),
    # 1.2: Release became a kill switch (Hard: a 5 ms cut to dry, Smooth: a fade over Release Beats; nothing else moves);
    # the Release FX (Echo Out / Vinyl Brake / Backspin) has its own button; Panic; riser Tune / Duck; user presets.
    ("Release / Presets", [
        opt("kill_mode", "Release Mode", ["Hard", "Smooth"], 0),
        trig("release_go", "Release FX Go"),
        trig("panic", "Panic"),
        knob("noise_tune", "Riser Tune", -24.0, 24.0, 0.0, "st", display="int"),
        knob("noise_mod", "Riser Duck", default=0.0, dynamic_display=True),
        opt("preset_slot", "Preset", [str(i) for i in range(1, 17)], 0),
        trig("preset_save", "Save Preset"),
        trig("preset_load", "Load Preset"),
        knob("preset_info", "Preset Status", dynamic_display=True),
    ]),
]


def main():
    params = [p for _, ps in SECTIONS for p in ps]
    keys = [p["key"] for p in params]
    assert len(keys) == len(set(keys)), "duplicate key"
    doc = {"name": "RMXXXL", "params": params,
           "sections": [{"label": label, "keys": [p["key"] for p in ps]} for label, ps in SECTIONS]}
    with open(os.path.join(ROOT, "params.json"), "w") as f:
        json.dump(doc, f, indent=2)
        f.write("\n")

    lines = ["// GENERATED by tools/gen_params.py -- edit that, not this file.",
             "#pragma once", "",
             "enum ParamKind { K_CONT, K_OPTION, K_TRIGGER };",
             "struct ParamDef { const char* key; float min, max, def; ParamKind kind; int nopts; };", "",
             "enum ParamId {"]
    for k in keys:
        lines.append("  P_%s," % k.upper())
    lines += ["  P_COUNT", "};", "", "static const ParamDef kParamDefs[P_COUNT] = {"]
    for p in params:
        if "options" in p:
            lines.append('  { "%s", 0.0f, %d.0f, %d.0f, K_OPTION, %d },' %
                         (p["key"], len(p["options"]) - 1, p["default"], len(p["options"])))
        elif p.get("momentary"):
            lines.append('  { "%s", 0.0f, 1.0f, 0.0f, K_TRIGGER, 0 },' % p["key"])
        else:
            lines.append('  { "%s", %rf, %rf, %rf, K_CONT, 0 },' % (p["key"], p["min"], p["max"], p["default"]))
    lines += ["};", ""]
    with open(os.path.join(ROOT, "src", "param_table.h"), "w") as f:
        f.write("\n".join(lines))
    print("%d params -> params.json, src/param_table.h" % len(params))


if __name__ == "__main__":
    main()
