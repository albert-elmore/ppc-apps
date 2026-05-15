# ppc-apps

Experimental music and utility applications for **Macintosh G3 (PowerPC)** running **Mac OS 9**. Sources are written in C for **Metrowerks CodeWarrior** as self-contained, single-file programs (add one `.c` file per target—no external dependencies beyond the classic Mac OS SDK).

## Applications

| App | Source | Description |
|-----|--------|-------------|
| [Osc40](apps/osc40/) | `apps/osc40/src/osc40.c` | Forty-oscillator composition tool with randomized note drift and extensive performance controls. |
| [Noise Lab](apps/noise-lab/) | `apps/noise-lab/src/noise-lab.c` | Percussion-oriented waveform designer for building drum-like samples. |
| [Media Player](apps/media-player/) | `apps/media-player/src/media-player.c` | Multi-deck WAV player (“G3 Stage Player”) with CDJ-style features and more. |
| [Granular](apps/granular/) | `apps/granular/src/granular.c` | Granular synthesis instrument (in active development). |
| [Event Sequencer](apps/event-sequencer/) | `apps/event-sequencer/src/event-sequencer.c` | TidalCycles-inspired text language for patterns on this platform. |
| [CPU Monitor](apps/cpu-monitor/) | `apps/cpu-monitor/src/cpu-monitor.c` | CPU load meter for profiling apps on real hardware. |

Each app has its own `README.md` under `apps/<name>/` with build notes.

## Target platform

- **Hardware:** Power Macintosh G3 (and similar PowerPC Macs)
- **OS:** Mac OS 9.x
- **Toolchain:** CodeWarrior for Mac OS (classic Mac OS APIs: Sound Manager, QuickDraw, Controls, etc.)

Open `apps/<name>/src/<name>.c` in a new CodeWarrior project (one source file per target). There is no shared build system yet.

## Repository layout

```text
ppc-apps/
├── README.md
├── LICENSE
├── apps/
│   ├── osc40/
│   │   ├── README.md
│   │   └── src/
│   │       └── osc40.c
│   ├── noise-lab/
│   ├── media-player/
│   ├── granular/
│   ├── event-sequencer/
│   └── cpu-monitor/
├── common/              # future: shared headers, audio helpers
└── docs/                # future: screenshots, language spec
```

**Versioning:** use git tags per app (e.g. `osc40/v1.0`, `noise-lab/v1.3`), not version suffixes in filenames.

## Status

Early/experimental. Event Sequencer’s language and Granular’s feature set are still evolving.

## License

GPL-3.0 — see [LICENSE](LICENSE).
