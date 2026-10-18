# M1 boot report: headless boot on Unicorn (18.10.2026)

Command (from `build/`):

```
./nj_headless_uc --so ../input/extracted/lib/armeabi-v7a/libLEGO_M1.so \
  --obb ../input/extracted/Android/Obb/com.lego.ninjago.toe/main.1057.com.lego.ninjago.toe.obb \
  --frames 1500 --fps 60 --tap 600,960,540 --tap 900,960,540
```

## Result

- Library load: 400 constructors, then the full `GameActivity.onCreate` sequence, `nativeResume`, `nativeInit`,
  `nativeResize(1920,1080)`, `nativeWindowFocusChanged(true)`: **0.04 s**, no fatal.
- 1500 frames at 60 Hz (25 s) without a fatal error. Every frame has draw calls (1–3 while loading, 3 on the first
  screen, **20–21 after the simulated taps**), so the game reacts to touch input and changes screen.
- Threads: main/GL thread, a data-loading thread (parsing `.fib` data: memcpy/strcasecmp/atoi heavy), a per-frame
  worker (cond-wait each frame), the OpenSL callback pump and one idle worker.
- Data: the OBB is opened through the `addOBBEntriesToFusion` entries (4 opens); `.fib` content is read at entry
  offsets. The game created and re-read `/data/data/com.lego.ninjago.toe/files/savegame.dat`.
- Shaders: 18 compiled from source, 11 programs linked. The engine first looks for cached program binaries in
  `cache/shaderbinaries/13504292/*.glprog` (missing → compiles from source; `glProgramBinaryOES` is reported absent).
- CPU: Unicorn needs ~2 s of CPU for 15 s of menu/loading at 60 fps, so the oracle backend is fast enough here.

## Texture edition of this OBB: **ATC** (Qualcomm Adreno)

| Format | Uploads (1500 frames) |
|---|---|
| `0x8C92` ATC_RGB_AMD | 83 |
| `0x87EE` ATC_RGBA_INTERPOLATED_ALPHA_AMD | 98 |
| uncompressed RGBA/UNSIGNED_BYTE (`0x1908/0x1401`) | 61 |

The engine uploads ATC even though the GL extension string offers only ETC1/PVRTC, so the edition is fixed by the
data. **iOS GPUs have no ATC support → the iOS GL layer must decode ATC to RGBA8 in software** (or transcode) in
`glCompressedTexImage2D` (M3).

## Fixes made during bring-up

| Symptom | Cause | Fix |
|---|---|---|
| 100 % CPU, no progress inside `nativeInit` | `SLPlaybackRateItf::GetRateRange` returned success for every index; the engine enumerates ranges until an error (46 M calls in 20 s) | only index 0 exists; others return `SL_RESULT_PARAMETER_INVALID` (test `sl_rate_range_index_bounds`) |
| Game seemed stuck on the first screen | harness ran 1200 frames in 0.27 s; loading is time-based | `--fps` pacing in the harness |
| Music silent; the music player got fd 0 | bionic's inline `fileno()` reads `FILE::_file` (offset 14) directly; our guest FILE objects were zeroed | `fopen` fills `_flags`/`_file` (test `fs_fileno_field_inline`) → music plays (peak 0.44) |

## Observations for later milestones

- `[java] Flurry(FLURRY_EVENT_CHARACTER)` is logged repeatedly (analytics, harmless).
- Audio, confirmed: 30 PCM buffer-queue players (sound effects, `SL_DATAFORMAT_PCM`) and 1 music player with
  `SL_DATALOCATOR_ANDROIDFD` (fd of the OBB + entry offset/length) and `SL_DATAFORMAT_MIME` (MP3, decoded by minimp3).
- Shaders are plain GLSL ES 1.00 (19 dumped, no `#version`/`#extension`), so they compile on iOS unchanged.
