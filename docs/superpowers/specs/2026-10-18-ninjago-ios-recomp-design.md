# LEGO Ninjago Tournament on iOS 27: static recompilation design

**Date:** 18.10.2026 · **Owner:** Miró (personal use) · **Status:** approved direction ("just do it"), plan next

## Intent

Miró played LEGO Ninjago: Tournament (TT Fusion / Hellbent, 2015) a lot as a kid. It is 32-bit only and
has been unplayable on iOS since iOS 11. The goal is to **play it again on his iPhone 17 (iOS 27.0.1)**
as a normal app installed with his Apple developer account. Testing also happens in the iOS Simulator.

**Success, in his words:** select a character and play the game. All characters available.

## Goals

- Boot to the menu, then the character selection, then the arena gameplay (waves, bosses, studs,
  Spinjitzu), with original graphics, music, sound effects and touch controls.
- All characters selectable (unlock by save-data edit or by overriding the unlock check; builder's call).
- Progress persists between launches.
- Runs smoothly (at least the original 30/60 fps) on iPhone 17; also runs in the iOS Simulator.

## Non-goals

Tutorial/Dojo, prize draw, challenges, Temple Gallery, multiplayer/Game Center, in-app purchases,
Flurry analytics, browser links, store features. These stay in the game code but need not work.
If one blocks the main path, it is bypassed (hook or save edit), not repaired.

## Anti-goals

- No redistribution: no game code, game data or generated translations of the game code ever go to
  git or any remote. The repository holds only original tooling and runtime code.
- No reimplementation of the game logic "by eye". The original code runs, translated.
- No dependence on JIT enabling (StikDebug etc.) on the phone: it must start like any app.

## Decisions (with evidence)

1. **Source binary: the Android build, not the iOS IPA.** `libLEGO_M1.so` v1.05.2 (Sep 2016, armeabi-v7a)
   exports 11,762 functions with full C++ signatures; ~14,250 function starts are known in total
   (exported symbols ∪ `.ARM.exidx` ∪ relocated pointers ∪ call targets). It has 219 imports, no
   Objective-C, no C++ exception throwing, and only a JNI boundary (26 `Java_*` entry points, about 12
   Java callbacks). The iOS binary (v1.1) is stripped (37 symbols) and bridges into 61 UIKit/Foundation
   classes. The iOS IPA stays as a secondary reference and an alternative data source (same `FUSE1.00`
   container; PVRTC textures, m4a music).
2. **CPU: ahead-of-time static recompilation (ARM32 → C → arm64).** Evidence from the census
   (`tools/census.py`): ARM mode only, about 70 real mnemonics, VFP only (no NEON), 300 `add pc, pc, rX`
   switch tables, 1,329 indirect calls. iPhone 17 + iOS 27 (TXM) makes on-device JIT unreliable, so a
   JIT-based emulator (tapHLE, LiveExec32, a THUMB-style runtime) is a fallback only.
   - A precedent exists: DeividGGXD/Lego-ninjago-tournament-64-bit runs this same library on a
     64-bit-only Android phone via THUMB (dynamic ARM32→ARM64 translation) with the same 219 imports
     shimmed. That means the library and data are known to work under translation.
3. **Reference CPU for development: Unicorn (QEMU ARM) on macOS.** It is used to bring up the platform
   layer before the recompiler exists, and as the oracle for differential testing of the recompiler.
   It is never shipped.
4. **Platform layer (HLE) written once in C/Objective-C, shared by both CPU backends**, against a
   small `GuestCPU` interface (registers, guest memory base, call-guest-function).
5. **Guest memory model:** one 4 GiB reserved host region; guest address `a` lives at `base + a`.
   The library is loaded at a fixed guest address chosen at recompile time, with relocations applied.
   Guest pointers stay 32-bit, so engine data structures and game files are used unchanged.
6. **Data: the OBB from the user's XAPK** (`main.1057...obb`, 973 MB, all 33 entries stored, no
   backslash paths). It is bundled into the app at build time from `input/`. The texture edition is not
   yet determined (engine has ETC1/ATITC/DXT converters; iOS GLES supports ETC2/PVRTC natively). It is
   identified in milestone 1; software decoding in the GL layer is the fallback.

## Architecture

```
 iOS app shell (Swift/ObjC)            platform layer (C/ObjC, "HLE")            guest code
 ─────────────────────────            ───────────────────────────────           ──────────────
 UIApplication, CAEAGLLayer view  ──▶  JNI facade: Java_* entry points    ──▶  recompiled
 CADisplayLink → nativeRender          fake JavaVM/JNIEnv tables in guest       libLEGO_M1.so
 touches → nativeTouchEvent*           memory, ~12 Java methods emulated        (C → arm64)
 lifecycle → nativePause/Resume        bionic libc/libm subset, pthreads   ◀──  import calls
 OBB zip index → addOBBEntries...      EGL → EAGL, GLES2 pass-through
                                       OpenSL ES → AVAudioEngine/AudioUnit
                                       AAssetManager, liblog, dlopen/dlsym
```

- **Recompiler** (`tools/recomp/`, Python + Capstone): finds functions, recovers switch tables
  (both `add pc` and `ldr pc` forms), and emits one C function per guest function operating on a
  `GuestCtx` (r0–r15, NZCV, VFP d0–d31/FPSCR, exclusive monitor). Intra-function branches become
  `goto`, calls become direct C calls, and indirect calls go through a sorted guest-address → host-function
  table. Conditional execution becomes `if`. LDREX/STREX use host compare-and-swap. Imports call HLE thunks.
  Generated C goes to `gen/` (git-ignored) and is compiled into the app.
- **Calling convention at the boundary:** armeabi-v7a softfp (floats in core registers). HLE thunks
  read r0–r3 and the guest stack, then write r0/r1.
- **Threads:** each guest thread gets a host thread, its own `GuestCtx` and a guest stack. Mutexes and
  condition variables are host objects referenced from the guest's 4-byte bionic structs.
- **Unlocks/bypasses:** function-level overrides by symbol name (possible thanks to the exports),
  usable in both backends.

## Testing

- Per-instruction differential tests: random register/memory states run through Unicorn and the
  generated C, then compared.
- Per-function and boot-trace differential runs on macOS (same process, two backends).
- HLE unit tests (C), and recompiler unit tests (pytest).
- Milestone screenshots in the Simulator, compared by Miró against the reference videos
  (TapGameplay "Walkthrough Part 1, Lloyd" and the ChatGPT research brief's list).

## Milestones

| # | Result | Where |
|---|---|---|
| M0 | Repo, inputs hashed, ELF loader, function and instruction inventory | Mac |
| M1 | Headless boot on Unicorn + HLE: OBB opened, data loaded, render loop reached (GL calls logged) | Mac |
| M2 | Recompiler: whole library translates and compiles; differential tests pass; headless boot on AOT | Mac |
| M3 | iOS app shell: first rendered frames (loading/title) | Simulator |
| M4 | Touch and audio: menus usable, music and sound | Simulator |
| M5 | Character selection and gameplay; all characters unlocked; saves persist | Simulator |
| M6 | Device build with the dev account, performance and lifecycle | iPhone 17 |

## Open questions left to the builder

Texture edition and GL extension strategy (M1). The exact unlock mechanism (M5). The audio
backend detail. Whether intro movies play (AVPlayer) or are skipped. Bundle versus first-run copy of the OBB.

## Risks

Recompiler gaps (rare instructions, unrecovered indirect jumps): mitigated by abort-with-diagnostics
plus the differential oracle. GL differences on iOS (deprecated GLES, texture formats): software decode
fallback. OpenSL ES emulation effort (the Android project shows ~1 C file). App memory limits:
extended-virtual-addressing and increased-memory-limit entitlements.
