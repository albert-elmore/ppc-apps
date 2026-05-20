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
- **Stop** — use the Stop button (not a keyboard key; `s` is a preset slot).

## Build

CodeWarrior for Mac OS 9 — create a new project and add `src/granular.c` as the only source file.

## Source

`src/granular.c`

## Releases

Tag releases as `granular/v0.1`, `granular/v1.0`, etc.
