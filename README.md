# ninjago-recomp

Personal preservation project: run LEGO Ninjago: Tournament (Android build 1.05.2) natively on iOS 27
by statically recompiling its 32-bit ARM engine to arm64 and replacing the Android platform layer.

**No game code or data lives in this repository.** Put your own copies in `input/` (git-ignored).
Generated translations of the game code go to `gen/` (git-ignored) and are never committed.

Design: `docs/superpowers/specs/2026-10-18-ninjago-ios-recomp-design.md`
