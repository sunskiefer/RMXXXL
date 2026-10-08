# Roadmap

Changes planned after testing 1.1.1 on an Akai Force (MPC OS 3.9.1). Found a problem or have an idea? Open an
[issue](../../issues).

## Bugs
- **Screen pads are slow.** Pressing a pad or button on the touchscreen takes a few seconds to trigger the sound.
- **Q-Links miss on mode selectors and on/off switches.** Changing them from a Q-Link is almost impossible
  (for example Clouds On/Off).
- **Mode selector text is unreadable.** The text in the selector boxes needs to be as big as the rest, or shown in
  another readable way.
- **Release doesn't kill everything.** It cuts the effect only for a moment, then the effect comes back (see the
  new Release below).

## Changes
- **Bigger controls.** Scale up all buttons and all knobs.
- **New Release: a kill switch.** Release becomes an On/Off switch:
  - On: everything goes to the dry signal and stays dry until it is switched off again.
  - Off: the effects come back exactly as they were.
  - It never changes any parameters.
  - Two kill modes: **Hard** (instant cut to dry) and **Smooth** (a transition from effects on to off).
- **Performance mode.** Change which parameters are shown (list to follow).
- **Noise riser.** Add **Tune** and **Modulation** knobs. Modulation is a simple LFO or a ducker driven by the
  incoming sound, whichever works best.

## New features
- **Panic button.** Resets the plugin to factory defaults, as freshly installed, whatever was saved in the project.
  It also stops all sound and clears every tail.
- **Presets.**
