# Granular

Granular synthesis instrument (in active development). Ergonomic performance controls and fuller DSP/UI are planned.

## Loop presets

- **Assign** — stores the current loop window (start/end) to the next preset key you press.
- **Preset keys** — `a` `s` `d` `f` `g` `h` `j` `k` `l` `z` `x` `c` `v` `b` `n` `m` recall stored positions.
- **Preset Fade** — when recalling a preset (0–6000 ms, default 2500 ms), both loop windows stay active during the fade. Sliders and the destination markers move immediately; the waveform shows **blue** (source) and **red** (destination) window lines for the whole fade. 0 ms is instant.
- **Volume fade** (checkbox, below preset keys, **on by default**) — preset recalls use a volume crossfade. Uncheck for spawn blend.
- **W** — same as the Assign button.
- On load, the default loop window end is **2000 ms** into the file.
- **Escape** — cancels assign mode without saving.
- **Play/Stop** — one button toggles transport (label shows Play or Stop). **Space** toggles the same way.
- **Play/Stop Fade** — slider sets fade-in and fade-out time from 0 ms to 16 s (default ~200 ms) on output volume when starting or stopping.
- **Pitch Glide** — slider sets how long pitch changes take from 0 ms (instant) to 16 s. Applies to the Pitch and Fine Tune sliders; active grains follow the glide.
- **Record** — toggles recording the mixed engine output to a WAV file (stereo, 16-bit, 44.1 kHz). Press again to stop and finalize the file. Output is resampled from the loaded file’s sample rate when needed. Default save name is the loaded basename with `-rec.wav`. Disk writes run on the main thread (not in the audio callback) so playback stays stable.

## Patches (save / open collection)

- **Save Patch** — saves the loaded WAV and all current settings into the chosen folder as a matched pair: `Name.wav` + `Name.gran` (same base name, different extension).
- **Open Patch** — loads `Name.gran` from disk, restores every slider/preset/checkbox, then loads the sibling `Name.wav` from the same folder.
- Keep both files together when building a collection in one directory.

## Build

CodeWarrior for Mac OS 9 — create a new project and add `src/granular.c` as the only source file.

## Source

`src/granular.c`

## Releases

Tag releases as `granular/v0.1`, `granular/v1.0`, etc.
