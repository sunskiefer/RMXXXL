# Changelog

## 1.2.1
- **Q-Links follow the screen** on every page: bank 1 = the top row left to right, bank 2 = the bottom row left to
  right, so Force knob N sits under screen column N.
- **On/off switches flip with a small Q-Link turn either way** (Release, Clouds, Freeze, Reverse, Rolls), once per turn.
- **Lists can't race any more**: a Q-Link moves Beats, Echo Beat, Mode and the other lists one option at a time, at
  most one every 0.2 s (Beats used to skip from 1/2 to 4). Screen taps still jump straight to any option.
- **Clouds no longer drops the volume**: switching it on cost 9 dB of dry signal (Clouds' own -3 dB dry crossfade and
  its output stage halving everything). Clouds now runs fully wet and RMXXXL mixes: the dry stays at full level up to
  Blend 50 %, the wet comes in on top; each mode's wet level is trimmed to sit near the dry.
- Fix: the big knobs (Build Up, Break Down, Filter, Low / Mid / High, Reverb on its page, the ADSR, Limit Drive and
  Ceiling) slid up and down on the Force instead of turning. Their 1.2.0 filmstrip was 16384 px tall, MPC's limit;
  they are back to the 1.1.1 size, which draws right on the device.

## 1.2.0
Fixes from the first test on an Akai Force (MPC OS 3.9.1), and the features asked for after it.

**Fixes**
- **Screen pads and buttons fire on every tap.** A trigger kept reading back as pressed, so MPC's next tap only
  released it and every second tap did nothing (it felt like a delay of seconds). Pads, Release FX, Trigger, Reload and
  the new buttons now fire on each tap.
- **Readable option lists.** The open lists (Echo Beat, Mode, Quality, Sound, Roll Beat) were drawn in a tiny dotted
  font; they now use the same font and size as everything else.
- **Q-Links on lists and switches** no longer race past the option you want: a list moves one option per three small
  knob steps, an on/off switch per two. (Needs confirming on hardware.)
- **Release** didn't kill the effect, it only played a release effect for a moment (see below).

**New**
- **Release is a kill switch** (REMIX, ON / OFF): on, the whole effect crossfades to the dry input and stays there; off,
  it comes back exactly as it was. No parameter changes. **Hard** cuts in 5 ms, **Smooth** fades over Beats.
- **RELEASE FX** has its own button (Echo Out / Vinyl Brake / Backspin, as before).
- **PANIC**: every effect control back to factory settings, Release off, pads and rolls stopped, all tails cleared.
  The pad setup (sounds, envelopes, level, tune, roll beat, MIDI root) stays.
- **Presets**: 16 user slots on SETUP (Preset, SAVE, LOAD, status line), stored in `/sdcard/RMXXXL Presets`, pad
  sounds and envelopes included.
- **Noise riser**: Riser Tune (±24 semitones) and Riser Duck (ducks it under the incoming beat).
- **MIDI**: root + 9 switches Release, root + 10 is Panic (root + 8 stays Release FX). The README and the INSTALL.md in
  the release zip now explain the MIDI routing step by step.
- **No more drop-downs covering the page**: Echo Beat, Clouds Mode, Quality and Roll Beat are always-visible button
  grids, and the 16 preset slots are a grid too. Only the 17-entry pad Sound picker is still a list (it closes on a pick).
- **REMIX lower half is a big-button panel** for performance: Echo Beat, Release FX, Beats, RELEASE FX and a red
  PANIC; Feedback moved up next to Echo, Noise to SETUP. CLOUDS shows its six modes as a row of big buttons.
- **Bigger controls**: larger knobs (as large as MPC's filmstrip limit allows), big ON / OFF switches, taller buttons
  and option boxes.

**Under the hood**
- Framework (sd88me/mpc-vst-plugins) updated: one engine call at a time per instance, MIDI CC 20-35 / NRPN control.
- Fixed an undefined integer shift in Clouds' looping sample player (no change in sound).
- Q-Links: REMIX bank 2 now ends with Release and Hard / Smooth (Ceiling and Limit Drive stay on SETUP); SETUP gains
  Riser Tune, Riser Duck, Noise and Preset on bank 2. MIDI CC 20-35 move the REMIX Q-Links.
- The MIDI note table moved off the SETUP page into the docs.

## 1.1.1
- All text on every page is one size (about 24 px), in MPC's own font (Titillium Web).

## 1.1.0
- **PADS page**: four big pads, a latching roll per pad, and one row of Attack / Decay / Sustain / Release that
  edits the pad selected in Edit Pad (each pad keeps its own envelope and sound).
- **Swappable one-shots**: each pad plays its built-in drum or one of 16 sample slots, the `.wav` files in
  `/sdcard/RMXXXL Samples` (sorted by name). Reload re-reads the folder.
- Larger text and buttons across the skin.
- Fix: the sample folder read as empty on 32-bit ARM file systems (64-bit file offsets).

## 1.0.1
- New knob art: BLACK knobs for the main controls, GREY for the rest, on a tick scale.

## 1.0.0
- First version as RMXXXL v.1 by ANDREALPHEUS: isolator, DJ filter, Scene FX (Build Up / Break Down),
  tempo-synced echo, Release FX (Echo Out / Vinyl Brake / Backspin), Clouds (Parasites, six modes),
  Dragonfly Plate / Room / Hall, X-Pad drums, look-ahead brickwall limiter, black skin.
