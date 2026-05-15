# ppc-apps

Experimental music and utility applications for **Macintosh G3 (PowerPC)** running **Mac OS 9**. Sources are written in C for **Metrowerks CodeWarrior** as self-contained, single-file programs (add one `.c` file per target—no external dependencies beyond the classic Mac OS SDK).

## Target platform

- **Hardware:** Power Macintosh G3 (and similar PowerPC Macs)
- **OS:** Mac OS 9.x
- **Toolchain:** CodeWarrior for Mac OS (classic Mac OS APIs: Sound Manager, QuickDraw, Controls, etc.)

Open `apps/<name>/src/<name>.c` in a new CodeWarrior project (one source file per target). There is no shared build system yet.

## Status

Early/experimental. Event Sequencer’s language and Granular’s feature set are still evolving.

## License

GPL-3.0 — see [LICENSE](LICENSE).
