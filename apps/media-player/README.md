# Media Player (G3 Stage Player)

Multi-deck WAV player with transport, pitch, volume, pan, and metering — DJ-style use similar to Pioneer CDJs, plus additional performance features.

## Build

CodeWarrior for Mac OS 9 — create a new project and add `src/media-player.c` as the only source file.

## Source

`src/media-player.c` — three decks, 44.1 kHz, freeze/granular cloud per deck.

Freeze captures a window of audio at the playhead and granulates it (tweak `G3_FREEZE_*` / `G3_GRAIN_*` defines at top of source). Each grain picks independent L/R positions in the window and plays them hard-panned (L→left, R→right). Unfreeze resumes normal playback from the frozen position.

## Releases

Tag releases as `media-player/v1.0`, `media-player/v1.1`, etc. (initial import corresponded to the former `media-player-v1.c`).
