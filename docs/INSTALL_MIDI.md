
## After installing: MIDI, samples and presets

### Play RMXXXL from a MIDI track

MPC OS sends no MIDI to insert effects, so RMXXXL opens its own MIDI input port, **RMXXXL 1** (more instances: RMXXXL 2,
RMXXXL 3, ...). The port exists while the plugin is inserted; no restart is needed.

    MIDI track (pads / clip)  --MIDI Out-->  port "RMXXXL 1"  -->  RMXXXL (on a track, submix or the master)

1. Insert RMXXXL on the track, submix or master you want to effect.
2. Create a MIDI track (no instrument needed).
3. Set the MIDI track's MIDI output port to RMXXXL 1. Any channel. If only RMXXXL 2 (or higher) is listed, pick that one.
4. Play the MIDI track's pads, or record / draw notes in its clips.
5. Use a chromatic pad layout and set MIDI Root (REVERB / PADS page, default 36 = C1, where 60 = C3) to the note of the
   pad you want as Pad 1.

Notes from MIDI Root:

    +0 .. +3   Pads 1-4 (one-shot, velocity = level)
    +4 .. +7   Pads 1-4 as rolls while held (Roll Beat, tempo-locked)
    +8         Release FX (Echo Out / Vinyl Brake / Backspin)
    +9         Release on / off (kill to dry)
    +10        Panic (factory settings, tails cleared, pad setup kept)

Nothing happens? The port number changes when the plugin is removed and inserted again (re-select it on the MIDI track),
and only notes from MIDI Root to MIDI Root + 10 do something.

### Samples and presets

- Pad samples: WAV files in `/sdcard/RMXXXL Samples` (slots 1-16, sorted by name). Tap Reload on the PADS page after
  copying files, e.g. `scp "01 Kick.wav" "root@<device-ip>:/sdcard/RMXXXL Samples/"`.
- Presets: SETUP page, Preset 1-16, SAVE / LOAD. Stored as `/sdcard/RMXXXL Presets/Preset NN.txt`.

Both folders are outside the plugin folder, so updates and reinstalls keep them. The full guide is docs/USER_GUIDE.md
in the repository: https://github.com/sunskiefer/RMXXXL
