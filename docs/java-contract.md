# Java ↔ native contract (Android build 1.05.2)

Source: `base.apk` decompiled with jadx 1.5.6 (`tools/decompile_java.sh`, output in `build/jadx/`, not committed).
Native library: `libLEGO_M1.so`. The emulated host replaces everything on the Java side described here.

## 1. Native methods (all 26 `Java_*` exports)

All are `static native`, so the JNI signature is `(JNIEnv*, jclass, …)`.

| Export | Java declaration | JNI descriptor |
|---|---|---|
| `Java_com_wbgames_LEGOgame_Fusion_addAPKEntry` | `addAPKEntry(String apkPath, String assetPath, long length, long startOffset)` | `(Ljava/lang/String;Ljava/lang/String;JJ)V` |
| `Java_com_wbgames_LEGOgame_Fusion_addOBBEntriesToFusion` | `addOBBEntriesToFusion(String obbPath, APKZipFileEntry[] entries)` | `(Ljava/lang/String;[Lcom/wbgames/LEGOgame/Fusion$APKZipFileEntry;)V` |
| `Java_com_wbgames_LEGOgame_Fusion_nativeBackButtonPressed` | `boolean nativeBackButtonPressed()` | `()Z` |
| `Java_com_wbgames_LEGOgame_Fusion_nativeInitializeAssetManager` | `nativeInitializeAssetManager(AssetManager)` | `(Landroid/content/res/AssetManager;)V` |
| `Java_com_wbgames_LEGOgame_Fusion_nativeSetCachePath` | `nativeSetCachePath(String)` | `(Ljava/lang/String;)V` |
| `Java_com_wbgames_LEGOgame_Fusion_nativeSetCommandLine` | `nativeSetCommandLine(String)` | `(Ljava/lang/String;)V` |
| `Java_com_wbgames_LEGOgame_Fusion_nativeSetDeviceStrings` | `nativeSetDeviceStrings(String model, String product, String manufacturer, String hardware)` | `(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V` |
| `Java_com_wbgames_LEGOgame_Fusion_nativeSetGameCircleEnabled` | `nativeSetGameCircleEnabled(boolean)` | `(Z)V` |
| `Java_com_wbgames_LEGOgame_Fusion_nativeSetGameCircleReady` | `nativeSetGameCircleReady(boolean)` | `(Z)V` |
| `Java_com_wbgames_LEGOgame_Fusion_nativeSetSavePath` | `nativeSetSavePath(String)` | `(Ljava/lang/String;)V` |
| `Java_com_wbgames_LEGOgame_Fusion_nativeSetWritePath` | `nativeSetWritePath(String)` | `(Ljava/lang/String;)V` |
| `Java_com_wbgames_LEGOgame_Fusion_nativeTouchEventCancelAll` | `nativeTouchEventCancelAll()` | `()V` |
| `Java_com_wbgames_LEGOgame_Fusion_nativeTouchEventDown` | `nativeTouchEventDown(int pointerId, float x, float y, float pressure)` | `(IFFF)V` |
| `Java_com_wbgames_LEGOgame_Fusion_nativeTouchEventMove` | same | `(IFFF)V` |
| `Java_com_wbgames_LEGOgame_Fusion_nativeTouchEventUp` | same | `(IFFF)V` |
| `Java_com_wbgames_LEGOgame_Fusion_nativeTouchEventGestureStart` | same (first pointer down) | `(IFFF)V` |
| `Java_com_wbgames_LEGOgame_Fusion_nativeTouchEventGestureEnd` | same (last pointer up) | `(IFFF)V` |
| `Java_com_wbgames_LEGOgame_GameGLSurfaceView_nativeColdBoot` | *(no Java declaration in this APK; never called)* | — |
| `Java_com_wbgames_LEGOgame_GameGLSurfaceView_nativeDone` | `nativeDone()` (declared, never called) | `()V` |
| `Java_com_wbgames_LEGOgame_GameGLSurfaceView_nativeInit` | `nativeInit(EGLConfig config, Activity activity)` | `(Ljavax/microedition/khronos/egl/EGLConfig;Landroid/app/Activity;)V` |
| `Java_com_wbgames_LEGOgame_GameGLSurfaceView_nativePause` | `nativePause()` | `()V` |
| `Java_com_wbgames_LEGOgame_GameGLSurfaceView_nativeRender` | `nativeRender()` | `()V` |
| `Java_com_wbgames_LEGOgame_GameGLSurfaceView_nativeResize` | `nativeResize(int width, int height)` | `(II)V` |
| `Java_com_wbgames_LEGOgame_GameGLSurfaceView_nativeResume` | `nativeResume()` | `()V` |
| `Java_com_wbgames_LEGOgame_GameGLSurfaceView_nativeUpdateMovieInfo` | `nativeUpdateMovieInfo(boolean playing, int positionMs)` | `(ZI)V` |
| `Java_com_wbgames_LEGOgame_GameGLSurfaceView_nativeWindowFocusChanged` | `nativeWindowFocusChanged(boolean hasFocus)` | `(Z)V` |

The library has **no `JNI_OnLoad`**. Its 400 `INIT_ARRAY` constructors run when the library loads (`System.loadLibrary("LEGO_M1")` in `GameActivity`'s static initialiser).

## 2. Startup and lifecycle sequence

**UI thread, `GameActivity.onCreate`** (in this order):
1. `nativeSetWritePath(getExternalFilesDir(null))`, i.e. `/storage/emulated/0/Android/data/com.lego.ninjago.toe/files` (only if non-null)
2. `nativeSetSavePath(getFilesDir())`, i.e. `/data/data/com.lego.ninjago.toe/files`
3. `nativeSetCachePath(getCacheDir())`, i.e. `/data/data/com.lego.ninjago.toe/cache`
4. `nativeInitializeAssetManager(getAssets())`
5. `nativeSetCommandLine(cmd)`, where `cmd` is `""` (built from intent extras as `"-<key> <value> "` pairs)
6. `nativeSetDeviceStrings(Build.MODEL, Build.PRODUCT, Build.MANUFACTURER, Build.HARDWARE)`
7. `addOBBEntriesToFusion(obbPath, entries)` via `ExpansionFile.prepareExpansionFileForFusion(ctx, 1057, 1057)`, called once per distinct zip file (one call here, because there is no patch OBB). `obbPath` = `/storage/emulated/0/Android/obb/com.lego.ninjago.toe/main.1057.com.lego.ninjago.toe.obb`.

`addAPKEntry` is only reachable from an unused recursive asset scan (dead code), so it is never called. `nativeSetGameCircleEnabled/Ready` are never called from Java either (Amazon GameCircle, other builds).

**UI thread, `onResume`** → `GameGLSurfaceView.onResume` → `nativeResume()`.
**UI thread, `onWindowFocusChanged(true)`** → `nativeWindowFocusChanged(true)`.

**GL thread (`GLSurfaceView.Renderer`)**:
- `onSurfaceCreated(gl, config)` → `nativeInit(config, activity)`. EGL is already current: ES 2 context (`EGL_CONTEXT_CLIENT_VERSION` 2), config RGBA 8/8/8/8, depth 24, stencil 8 (translucent surface), `setPreserveEGLContextOnPause(true)`.
- `onSurfaceChanged(gl, w, h)` → `nativeResize(w, h)` (pixels).
- `onDrawFrame(gl)` → `nativeRender()`, once per frame. GLSurfaceView then calls `eglSwapBuffers` (Java side).

**UI thread, `onPause`** → `nativePause()`. **Back button** → `nativeBackButtonPressed()`: if it returns true the activity closes.

## 3. Java methods the native code calls back

Each is looked up by name and signature through JNI. Planned emulation on iOS:

| Class | Method | Signature | Java behaviour | Emulation |
|---|---|---|---|---|
| `com/wbgames/LEGOgame/FusionCallback` | `GetCurrentLanguageAsISO3Letter` (static) | `()Ljava/lang/String;` | `Locale.getDefault().getISO3Language()` | ISO 639-2 code of the iOS preferred language (`"deu"`, `"eng"`, …), default `"eng"` |
| `com/lego/ninjago/toe/GameActivity` | `openBrowser` (static) | `(Ljava/lang/String;)V` | opens a URL | log only |
| `com/wbgames/LEGOgame/GameGLSurfaceView$Renderer` | `shutdownApp` (static) | `()V` | `activity.finish()` | log only |
| `com/wbgames/LEGOgame/GameGLSurfaceView$Renderer` | `startMoviePlayback` (static) | `(Ljava/lang/String;ZLjava/lang/String;)Z` | plays an FMV from APK/OBB assets | return `false`: the OBB contains no video files (5 `.fib` + 28 `.mp3`) |
| `com/wbgames/LEGOgame/GameGLSurfaceView$Renderer` | `stopMoviePlayback` (static) | `()V` | | no-op |
| `com/wbgames/LEGOgame/GameGLSurfaceView$Renderer` | `setMoviePlaybackAttributes` (static) | `(F)V` | volume | no-op |
| `com/wbgames/LEGOgame/GameGLSurfaceView$GameVideoPlayer` | `setNewSubTitleInfo` (static) | `(IF)V` | subtitle setup | no-op |
| `com/wbgames/LEGOgame/GameGLSurfaceView$GameVideoPlayer` | `addSubtitle` (static) | `(Ljava/lang/String;FF)V` | | no-op |
| `com/wbgames/LEGOgame/GameFlurry` | `Flurry_LogEvent` (static) | `(Ljava/lang/String;)V` | analytics | no-op (log) |
| `com/wbgames/LEGOgame/GameFlurry` | `Flurry_LogEventParam1` (static) | `(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V` | analytics | no-op (log) |
| activity object (`android/app/Activity`) | `getClassLoader` | `()Ljava/lang/ClassLoader;` | the app class loader | return a ClassLoader handle |
| `java/lang/ClassLoader` | `loadClass` | `(Ljava/lang/String;Z)Ljava/lang/Class;` | class by binary name (`com.wbgames…`) | map `.` → `/`, then return the class handle |

**Objects passed into native code**
- `EGLConfig` (`Ljavax/microedition/khronos/egl/EGLConfig;`): native code reads its field `mEGLConfig`, trying signature `I` first and then `J`. Emulate as an `int` field holding our fake `EGLConfig` handle. A `GetFieldID` miss must return 0 with a pending `NoSuchFieldError` that `ExceptionCheck`/`ExceptionClear` see.
- `Fusion$APKZipFileEntry[]`: fields `fileName` (`Ljava/lang/String;`), `offset` (`J`) and `length` (`J`). `offset` is the **data offset** inside the zip (past the local header; `ZipResourceFile.ZipEntryRO.mOffset`). `length` is the compressed length, which equals the size for stored entries.
- `AssetManager`: only passed to `AAssetManager_fromJava`. No other AAsset API is imported.
- `Activity`: the receiver for `getClassLoader`.

## 4. OBB entry list

`assets/obb_info.xml`: main version `a=1057`, main size `b=973671465`, patch `c=1057`, `d=0`. Our OBB
(`main.1057.com.lego.ninjago.toe.obb`, 973,671,465 bytes, 33 stored entries) is exactly the one this APK expects.
One `addOBBEntriesToFusion` call with all 33 entries `{fileName, dataOffset, size}`. Names include `music/…mp3`.

## 5. Touch input

`GameGLSurfaceView.onTouchEvent` runs on the **UI thread** and calls the native touch functions directly (not queued to
the GL thread). Coordinates are `MotionEvent.getX/getY` in **view pixels** (origin top-left); `pressure` is
`getPressure()` (≈1.0). Mapping of `getActionMasked()`:
- `ACTION_DOWN` (0) → `nativeTouchEventGestureStart(id, x, y, p)` (the native side resets all touch state, then
  records the finger as down)
- `ACTION_POINTER_DOWN` (5) → `nativeTouchEventDown(id, x, y, p)` for the action pointer, then
  `nativeTouchEventMove` for every pointer
- `ACTION_MOVE` (2) → `nativeTouchEventMove` for every pointer
- `ACTION_POINTER_UP` (6) → `nativeTouchEventMove` for every pointer, then `nativeTouchEventUp` for the action pointer
- `ACTION_UP` (1) → `nativeTouchEventGestureEnd` (the native side releases all touches)
- `ACTION_CANCEL` (3) → `nativeTouchEventCancelAll()`

An earlier version of this section had Down/Up and GestureStart/GestureEnd swapped. Delivering a second finger as
GestureStart reset the first finger, which made the on-screen D-pad register a new swipe on every extra touch
(fixed in the iOS app; `harness/boot.c` `nj_touch_native` and `tests/c/test_touch_map.c` follow this table).

(The video view has a copy of this handler; it is irrelevant without FMVs.)

## 6. Threads

| Thread | Calls |
|---|---|
| main/UI | library load (constructors), §2 onCreate sequence, `nativeResume`, `nativePause`, `nativeWindowFocusChanged`, touch, back button |
| GL | `nativeInit`, `nativeResize`, `nativeRender` |

On iOS both run on the main thread (CADisplayLink drives `nativeRender`). The native code serialises its own state, so
calling everything from one thread is a strict subset of the Android behaviour.

## Checklist

All 26 `Java_*` exports from `llvm-nm -D` appear in §1: addAPKEntry, addOBBEntriesToFusion, nativeBackButtonPressed,
nativeInitializeAssetManager, nativeSetCachePath, nativeSetCommandLine, nativeSetDeviceStrings,
nativeSetGameCircleEnabled, nativeSetGameCircleReady, nativeSetSavePath, nativeSetWritePath,
nativeTouchEventCancelAll, nativeTouchEventDown, nativeTouchEventGestureEnd, nativeTouchEventGestureStart,
nativeTouchEventMove, nativeTouchEventUp (Fusion: 17), plus nativeColdBoot, nativeDone, nativeInit, nativePause,
nativeRender, nativeResize, nativeResume, nativeUpdateMovieInfo, nativeWindowFocusChanged (GameGLSurfaceView: 9) = 26. ✔
