# RMXXXL v1.1.1

**An RMX-1000-style remix effect that runs natively inside MPC OS on the Akai Force.**

RMXXXL is a VST2 insert effect for MPC OS's built-in plugin host, made by L'Cronx (shown on the device as
**ANDREALPHEUS**). Put it on a track, a submix or the master and play the mix live: build-ups, breakdowns,
filter sweeps, band kills, echoes, reverb washes, granular textures, drum and sample pads, and a Release button
that snaps everything back. Everything sits in one insert slot, with its own touchscreen pages and Q-Link sets.

> [!NOTE]
> **Status: 1.1.1, tested on an Akai Force** (MPC OS 3.9.1 with MockbaMod). It also passes its full test suite on
> x86 (ASan + UBSan) and on the ARM build under QEMU. Report anything odd under [Issues](../../issues).

![The REMIX page](docs/img/remix.png)

*The REMIX page, rendered offline from the skin (on the device MPC fills in the values).*

## Highlights

- **Scene FX:** Build Up and Break Down are one knob each, moving the filter, echo, Clouds, reverb and a noise
  riser together. Release latches them off until both knobs are back at zero, like the RMX.
- **Isolator:** 3-band Linkwitz-Riley 8th-order kill EQ with Mixxx's design and crossovers (246 Hz / 2.48 kHz).
- **DJ filter:** one bipolar knob, low-pass to the left, high-pass to the right, with resonance.
- **Echo:** synced to the MPC tempo, 1/16 to 1 bar, damped feedback.
- **Release FX:** Echo Out, Vinyl Brake or Backspin over 1/2, 1, 2 or 4 beats.
- **Clouds:** Mutable Instruments Clouds with the Parasites firmware, all six modes (granular, stretch, looping
  delay, spectral, Oliverb, Resonestor).
- **Reverb:** Dragonfly Plate, Room and Hall inside the plugin, or Clouds' own reverb as the light option.
- **Pads:** four pads from the screen, MIDI or latching rolls; each has its own ADSR and plays its built-in drum
  or one of 16 swappable WAV one-shots.
- **Brickwall limiter:** look-ahead, Drive in, Ceiling out.
- **Skin:** black, BLACK and GREY knob art on a tick scale, all text in one size.

| | | |
| --- | --- | --- |
| ![CLOUDS](docs/img/clouds.png) | ![REVERB / PADS](docs/img/reverb-pads.png) | ![PADS](docs/img/pads.png) |

## Documentation

| Document | What's in it |
| --- | --- |
| [User guide](docs/USER_GUIDE.md) | Install, every page and control, MIDI, Q-Links, samples, troubleshooting |
| [Changelog](CHANGELOG.md) | What changed in each version |
| [Roadmap](ROADMAP.md) | Bugs and changes planned for the next versions |

## Requirements

- An **Akai Force** (first generation). Other first-generation MPC OS units (MPC Live / Live II, One, X,
  Key 61) should work but are untested.
- **Root SSH access** to the device, for example through MockbaMod. Stock MPC OS can't install third-party
  plugins.
- **MPC OS 3.x.** The skin uses the 3.x format.

## Installation

Download `RMXXXL-<version>-mpc-armv7.zip` from [Releases](../../releases), or build it (below). Unzip it and
follow the `INSTALL.md` inside. In short:

```
scp -r RMXXXL-<version> root@<device-ip>:/tmp/
ssh -t root@<device-ip> sh /tmp/RMXXXL-<version>/install.sh
```

The installer asks for confirmation (`-y` skips it), **stops MPC** (save your project first), copies the plugin
to `/sdcard/Synths/ANDREALPHEUS - VST - RMXXXL/`, backs up and edits `MPC.settings`, and starts MPC again.
Running it again upgrades in place. Then insert **RMXXXL** (manufacturer ANDREALPHEUS) on a track, submix or the
master. `uninstall.sh` removes it the same way.

Pad samples go in `/sdcard/RMXXXL Samples` (created on first load), outside the plugin folder, so they survive
updates.

## Building

Linux or WSL with Python 3 (+ Pillow), gcc and [Zig](https://ziglang.org/) (`pip install ziglang pillow numpy`). No
Docker.

```
git clone --recursive https://github.com/sunskiefer/RMXXXL
cd RMXXXL
./build.sh                # build/arm/rmxxxl.so + the skin -> build/package/
test/run_tests.sh         # the framework's host test + RMXXXL's own suite, under ASan/UBSan
```

`tools/gen_params.py` is the single source of the parameter list (it writes `params.json` and
`src/param_table.h`). MPC stores automation by parameter index, so from now on parameters are appended only.

## Related projects

- [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) by sd88me: the framework RMXXXL is built on, and
  the plugin catalog.
- [EffectForce](https://github.com/Devko/EffectForce) by Devko: an effect rack for the Force that is already
  available, with ten reorderable modules, modulation and an Octatrack-style performance mixer.
- [Overcast](https://github.com/FullPace/overcast) and [mpc-vst-dragonfly](https://github.com/gmorb/mpc-vst-dragonfly):
  the Clouds and Dragonfly ports RMXXXL takes its granular and reverb engines from.

## License

RMXXXL is released under the **GNU GPL v3.0 or later** ([LICENSE](LICENSE)), because it includes Dragonfly
Reverb. Third-party components keep their own licenses (below).

## Credits

- **Isolator and filter:** designs follow [Mixxx](https://github.com/mixxxdj/mixxx) 2.5 (GPL-2.0-or-later):
  `linkwitzriley8eqeffect.cpp`, `enginefilterlinkwitzriley8.cpp`, `filtereffect.cpp`, re-implemented here.
- **Clouds:** Emilie Gillet (Mutable Instruments), with Matthias Puech's Parasites firmware (MIT). MPC OS port by
  FullPace ([overcast](https://github.com/FullPace/overcast)), vendored from there; one local fix in
  `clouds/dsp/correlator.cc` (an undefined shift by 32).
- **MIDI input port and Clouds engine glue:** after FullPace's [overcast](https://github.com/FullPace/overcast) (MIT).
- **Reverbs:** Dragonfly Reverb by Michael Willis and Rob van den Berg, built on freeverb3 by Teru Kamogashira
  (GPL-3.0-or-later). MPC OS port by gmorb ([mpc-vst-dragonfly](https://github.com/gmorb/mpc-vst-dragonfly)),
  vendored from there.
- **VST2 wrapper, skin generator, installer:** sd88me ([mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins))
  (MIT), as a submodule in `third_party/mpc-vst-plugins`.
- **Interface font:** [Titillium Web](https://fonts.google.com/specimen/Titillium+Web), SIL Open Font License 1.1
  (`art/fonts/OFL.txt`).
- **Knob artwork** (`art/`): supplied by L'Cronx.

RMXXXL is not affiliated with or endorsed by Pioneer DJ, Akai Professional, Mutable Instruments or the Mixxx
project.
