# Roadmap

What the first test of 1.1.1 on an Akai Force (MPC OS 3.9.1) found, and where it stands. Found a problem or have an
idea? Open an [issue](../../issues).

## Done in 1.2.0 (installed and working on the Force, 2026-10-08)
- Screen pads and buttons needed several taps (fired on every second tap).
- Option lists unreadable (tiny dotted font).
- Q-Links raced through option lists; on/off switches hard to set from a Q-Link.
- Release didn't kill everything: it is now an On/Off kill switch (Hard / Smooth), with Release FX on its own button.
- Bigger knobs, buttons and switches.
- Noise riser: Tune and Duck.
- Panic button (factory settings, pad setup kept).
- Presets (16 user slots, pads included).
- MIDI routing explained in the README and in INSTALL.md.

## Done in 1.2.1 (tested on the Force, 2026-10-08)
- Big knobs slid instead of turning (filmstrip at MPC's 16384 px limit).
- Q-Links in screen order on every page; switches flip with a Q-Link turn; lists can't skip (Beats).
- Clouds no longer drops the dry level.

## Next
- **Performance mode.** Change which parameters are shown (list to follow).
- **Latency and CPU on the Force.** Plan: [docs/PERF_PLAN.md](docs/PERF_PLAN.md).
