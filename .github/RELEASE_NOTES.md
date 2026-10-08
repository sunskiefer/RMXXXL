**RMXXXL v.1 — an RMX-1000-style remix effect that runs natively inside MPC OS on the Akai Force.**

Put it on a track, a submix or the master and play the mix live: build-ups, breakdowns, filter sweeps, band kills, echoes, reverb washes, granular textures, drum and sample pads, a Release kill switch that drops everything back to dry, and a Panic button. Everything sits in one insert slot, with its own touchscreen pages and Q-Link sets.

> ✅ **1.2.0 is tested on an Akai Force** (MPC OS 3.9.1 with MockbaMod). It fixes what the first Force test (1.1.1, MPC OS 3.9.1 with MockbaMod) found: screen pads and buttons now fire on every tap, option lists are readable, Q-Links no longer race through lists, controls are bigger. New: Release kill switch, separate Release FX button, Panic, 16 user presets, riser Tune / Duck. As with any third-party plugin, save your projects before installing, and report problems under **Issues**. Full list: [CHANGELOG](https://github.com/sunskiefer/RMXXXL/blob/main/CHANGELOG.md).

## How you play it
1. **Shape the mix** with the isolator (Low / Mid / High, fully left = kill) and the DJ filter (left = low-pass, right = high-pass).
2. **Build tension** with **Build Up** (rising high-pass, echo, reverb and noise riser) or **Break Down** (closing low-pass washed into echo and reverb). One knob each.
3. **Drop it** with **Release FX**: Echo Out, Vinyl Brake or Backspin over 1/2 to 4 beats; it latches the scene off until both knobs are back at zero, like the RMX. Or switch **Release** on to kill everything to dry (Hard or Smooth) and off to bring it all back as it was.
4. **Add texture** with Clouds (six granular modes) and Dragonfly Plate, Room or Hall reverb.
5. **Hit the pads** from the screen, latching rolls or a MIDI track: built-in kick, snare, clap and hat, or your own WAV one-shots, each with its own ADSR.
6. **Stay safe**: a look-ahead brickwall limiter keeps the output under your Ceiling, and **Panic** puts everything back to factory settings in one tap.
7. **Save your scenes** in 16 preset slots (pads included).

Everything follows the MPC tempo, and all settings save with your project.

## Pages
**REMIX** (performance) · **CLOUDS** · **REVERB / PADS** · **PADS** · **SETUP** (limiter, crossovers, scene ranges, presets, riser)

## Requirements
- Akai Force (first generation); other Gen1 MPC OS units should work but are untested
- MPC OS 3.x
- Root SSH access (for example MockbaMod)

## Install
Download **RMXXXL-1.2.0-mpc-armv7.zip** below, unzip it, then:

    scp -r RMXXXL-1.2.0 root@<device-ip>:/tmp/
    ssh -t root@<device-ip> sh /tmp/RMXXXL-1.2.0/install.sh

The installer stops MPC (save first), installs the plugin, and starts MPC again. Then insert **RMXXXL** (manufacturer ANDREALPHEUS) as an insert effect. Pad samples go in `/sdcard/RMXXXL Samples`, presets in `/sdcard/RMXXXL Presets`.

## Play it from a MIDI track
RMXXXL opens its own MIDI port, **RMXXXL 1**. Create a MIDI track, set its MIDI output port to **RMXXXL 1**, and play: from MIDI Root (default 36 = C1) +0..+3 are Pads 1-4, +4..+7 the same pads as rolls while held, +8 Release FX, +9 Release on/off, +10 Panic. Step by step in the README and in INSTALL.md inside the zip.

Full guide (every control, MIDI setup, Q-Links, samples, presets, troubleshooting): [docs/USER_GUIDE.md](https://github.com/sunskiefer/RMXXXL/blob/main/docs/USER_GUIDE.md)

## Credits
- **Isolator and filter:** designs after the Mixxx project's LR8 isolator and filter effect
- **Clouds:** Emilie Gillet (Mutable Instruments), with Matthias Puech's Parasites firmware; MPC OS port and MIDI input by FullPace ([overcast](https://github.com/FullPace/overcast))
- **Reverbs:** Dragonfly Reverb by Michael Willis and Rob van den Berg, built on freeverb3 by Teru Kamogashira; MPC OS port by gmorb ([mpc-vst-dragonfly](https://github.com/gmorb/mpc-vst-dragonfly))
- **Plugin framework, skin tools and installer:** sd88me ([mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins))
- **Interface font:** Titillium Web (SIL Open Font License)

Licensed GPL-3.0-or-later.
