# Third-party components

| Component | Where | License |
|---|---|---|
| dlmalloc 2.8.6 (Doug Lea) | `runtime/third_party/dlmalloc/` | permissive MIT-style license (2023), see the file header |
| minimp3 (lieff) | `runtime/third_party/minimp3/` | CC0 1.0, see `LICENSE` there |
| Khronos OpenGL ES 2 and KHR headers | `runtime/third_party/khronos/GLES2`, `KHR` | MIT, see the file headers |
| Khronos EGL headers | `runtime/third_party/khronos/EGL` | Apache-2.0, see the file headers |

Used at build time and in tests, not shipped in the app: [Capstone](https://www.capstone-engine.org) (BSD-3-Clause),
[Unicorn](https://www.unicorn-engine.org) (GPL-2.0, only linked into the test harness, never into the iOS app).

LEGO Ninjago Tournament and all of its code and assets belong to their rights holders and are not part of this
repository.
