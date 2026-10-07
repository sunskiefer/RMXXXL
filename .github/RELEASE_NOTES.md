**RMXXXL v.1 — an RMX-1000-style remix effect that runs natively inside MPC OS on the Akai Force.**

Put it on a track, a submix or the master and play the mix live: build-ups, breakdowns, filter sweeps, band kills, echoes, reverb washes, granular textures, drum and sample pads, and a Release button that snaps everything back. Everything sits in one insert slot, with its own touchscreen pages and Q-Link sets.

> ⚠️ **Pre-release, not yet tested on hardware.** It passes its full test suite offline, but nobody has run it on a Force yet. Save and back up your projects before installing, and report problems under **Issues**.

## How you play it
1. **Shape the mix** with the isolator (Low / Mid / High, fully left = kill) and the DJ filter (left = low-pass, right = high-pass).
2. **Build tension** with **Build Up** (rising high-pass, echo, reverb and noise riser) or **Break Down** (closing low-pass washed into echo and reverb). One knob each.
3. **Drop it** with **Release**: Echo Out, Vinyl Brake or Backspin over 1/2 to 4 beats. Release latches the scene off until both knobs are back at zero, like the RMX.
4. **Add texture** with Clouds (six granular modes) and Dragonfly Plate, Room or Hall reverb.
5. **Hit the pads** from the screen, latching rolls or a MIDI track: built-in kick, snare, clap and hat, or your own WAV one-shots, each with its own ADSR.
6. **Stay safe**: a look-ahead brickwall limiter keeps the output under your Ceiling.

Everything follows the MPC tempo, and all settings save with your project.

## Pages
**REMIX** (performance) · **CLOUDS** · **REVERB / PADS** · **PADS** · **SETUP** (limiter, crossovers, scene ranges)

## Requirements
- Akai Force (first generation); other Gen1 MPC OS units should work but are untested
- MPC OS 3.x
- Root SSH access (for example MockbaMod)

## Install
Download **RMXXXL-1.1.1-mpc-armv7.zip** below, unzip it, then:

    scp -r RMXXXL-1.1.1 root@<device-ip>:/tmp/
    ssh -t root@<device-ip> sh /tmp/RMXXXL-1.1.1/install.sh

The installer stops MPC (save first), installs the plugin, and starts MPC again. Then insert **RMXXXL** (manufacturer ANDREALPHEUS) as an insert effect. Pad samples go in `/sdcard/RMXXXL Samples`.

Full guide (every control, MIDI setup, Q-Links, samples, troubleshooting): [docs/USER_GUIDE.md](https://github.com/sunskiefer/RMXXXXL/blob/main/docs/USER_GUIDE.md)

## Credits
- **Isolator and filter:** designs after the Mixxx project's LR8 isolator and filter effect
- **Clouds:** Emilie Gillet (Mutable Instruments), with Matthias Puech's Parasites firmware; MPC OS port and MIDI input by FullPace ([overcast](https://github.com/FullPace/overcast))
- **Reverbs:** Dragonfly Reverb by Michael Willis and Rob van den Berg, built on freeverb3 by Teru Kamogashira; MPC OS port by gmorb ([mpc-vst-dragonfly](https://github.com/gmorb/mpc-vst-dragonfly))
- **Plugin framework, skin tools and installer:** sd88me ([mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins))
- **Interface font:** Titillium Web (SIL Open Font License)

Licensed GPL-3.0-or-later.
