# Midas Android — port notes

This directory is the Gradle shell around the shared native port, based on
[Project Midas](https://github.com/murkantor/project-midas).

The current build defaults to a **universal APK** (`arm64-v8a` + `armeabi-v7a`),
so 64-bit-only devices (Galaxy S24+, Pixel 7+, Xiaomi Pad 7, ...) install the
same download as older 32-bit-capable phones and handhelds. The 64-bit engine
loads ROM data, models, fonts and audio banks, and has been smoke-tested through
every solo stage and the multiplayer arenas on an Android 16 KB-page x86_64
emulator (the same LP64 code path as arm64). See "64-bit status" below.

## Requirements

- JDK 17 (AGP 8.5.2 does not support JDK 25 as its launcher).
- Android SDK platform 34 and build-tools 34.0.0.
- NDK 26.2.11394342 and CMake 3.22.1 (both SDK-managed).
- An SDL2 source checkout at `third_party/SDL2` (the native build worker's
  `cmake/bootstrap-sdl2.sh` fetches the pinned revision). SDL is not vendored.
- `android/local.properties` with `sdk.dir=...` (ignored by git).

Gradle is pinned to 8.7 through the wrapper, and AGP to 8.5.2. The default
build is **universal**: `arm64-v8a` and `armeabi-v7a` in one APK, so users never
have to choose a build and 64-bit-only phones (Galaxy S24+, Pixel 7+, Xiaomi
Pad 7, ...) can install it. Single-ABI development builds are still available:

```sh
JAVA_HOME=/path/to/jdk-17 ./gradlew -PmidasAbis=arm64-v8a :app:assembleDebug
JAVA_HOME=/path/to/jdk-17 ./gradlew -PmidasAbis=armeabi-v7a :app:assembleDebug
```

`assembleRelease` runs `verifyUniversalApk`, which fails the build if the APK is
missing `lib/arm64-v8a` or `lib/armeabi-v7a`. A one-ABI release is exactly what
made v1.0.1 uninstallable on 64-bit-only phones
(`INSTALL_FAILED_NO_MATCHING_ABIS`: "App not installed" / "app is not
compatible").

The game and SDL2 both come from the root `CMakeLists.txt`, and `GE_VIDEO=ON`
is set by `app/build.gradle`. `midasAbis` only selects the Gradle/CMake ABI; it
does not add a second ABI flag.

The APK ships only the small runtime text tables and notices (controller
mappings, `Data/README`, `Mods/README`, shader list, ini help, license and
third-party notices). `GameData` copies them into `getFilesDir()` before native
entry and uses a SHA-256 manifest to keep user-edited copies across updates. No
ROM, save, or `midas.ini` is packaged. On first native entry the shell adds
Android defaults (`mouse=0`, `p1_hybrid=0`, `pads=1`, borderless fullscreen,
`maxfps=60`) only where the user has not already set the key; all other native
options remain untouched.

## Build

```sh
cd android
JAVA_HOME=/path/to/jdk-17 ./gradlew :app:testDebugUnitTest
JAVA_HOME=/path/to/jdk-17 ./gradlew :app:assembleDebug
JAVA_HOME=/path/to/jdk-17 ./gradlew :app:assembleRelease
```

The debug APK is written to `app/build/outputs/apk/debug/`; the universal,
sideloadable release APK to `app/build/outputs/apk/release/`.

## Signing

`assembleRelease` signs with `~/.android/debug.keystore` (alias
`androiddebugkey`) when that file exists. That is deliberate: v1.0.1 was
published with that certificate, and Android refuses an update signed by a
different key ("App not installed"). Override with `MIDAS_KEYSTORE`,
`MIDAS_KEY_ALIAS`, `MIDAS_KEYSTORE_PASSWORD` and `MIDAS_KEY_PASSWORD` to move to
a real release key; because the certificate changes, existing users have to
uninstall v1.0.1 once before installing a build signed with the new key.

## 64-bit status

The ARM64 engine now boots through ROM data, textures, fonts, models and audio,
loads every solo stage and the multiplayer arenas, and runs gameplay on a
16 KB-page Android 16 x86_64 emulator (same LP64 code as arm64). Physical arm64
phones still need the usual device pass, but the boot/menu/stage-load crashes
reported after the 64-bit move are fixed.

The last 64-bit correctness pass fixed:

- **Setup records.** The N64-layout prop-definition stream is expanded to host
  structs with the generated field map. `WATCH_MENU_OBJECTIVE_TEXT` (type 35)
  was only 16 bytes in the host array, so `setup_briefing_text_entry_parent`
  wrote its 8-byte `next` pointer into the following record. Every stage with
  watch-briefing text then walked a corrupted propdefs array; Cuba crashed on
  stage load and other stages crashed later. Type 35 is now a full 24-byte
  record, and `OBJECTIVE_ENTER_ROOM` reserves its runtime `next` pointer too.
- **Monitor scripts.** 64-bit `MONJUMPTO`/`MONJUMPCHANCE` used to emit a null
  target. The first animated monitor that took a jump set `cmdlist = NULL` and
  the next command fetch crashed (Frigate and Silo). The scripts now store a
  small target-table index; `process_monitor_animation_microcode` resolves it.
- **Pointer arithmetic.** `bg.c`, `lightfixture.c`, `image.c`, `bondview2.c`,
  `explosion.c` and `mpbots.c` no longer cast host pointers to `u32`/`s32`
  before adding offsets. Font table parameters in `front.c`/`options.c` are
  pointer-typed instead of `s32`.
- **Model OP20.** Header-style root nodes (opcode 0x14) are now converted and
  walked like opcode 1 instead of losing their child group.
- **Audio.** The AL banks and Rare sequence table are converted into host
  structs in `port/src/bank64.c`; the mixer reads low-arena buffers and the
  boot-switch mute is only used when a bank genuinely cannot load.

Known limits: `GE_STAGE=<mp-arena>` launches a *solo* setup that does not exist
for Complex/Temple/Caves/Library/Stack/Basement; use `GE_MP=<arena>` for those.
Physical-device profiling and the 32-bit-only parity lane remain separate from
the 64-bit fixes.

## ROM handling

`RomActivity` is the launcher. On first run it opens `ACTION_OPEN_DOCUMENT` and
takes a persistent read permission. The selected document is read once, checked
for an exact 12 MiB (0xC00000) size, converted from `.z64`/`.v64`/`.n64` order to
`.z64`, and compared against the stock USA SHA-1
`abe01e4aeb033b6c0836819f549c791b26cfde83`. Only then is it atomically installed
as `getFilesDir()/Data/baserom.u.z64`; a rejected or interrupted import never
replaces an existing ROM. `midas.ini` gets `[data] rom = Data/baserom.u.z64`
without changing other sections, and the source URI is kept in private
SharedPreferences for re-imports after a restart.

`MidasActivity` starts SDL with `--rom-file <filesDir>/Data/baserom.u.z64` and
adds the touch overlay. No storage permission is declared or requested.

## Checks without building the game

```sh
python -m unittest discover -s scripts/port -p 'test_*.py' -v
python scripts/port/check_android_entry.py
```

Run the Python commands from the repository root. Set `ANDROID_NDK_HOME` to
exercise the Android header probes as well. These checks cover platform helpers,
not the full engine or gameplay. Java tests and lint can run separately:

```sh
cd android
./gradlew :app:testDebugUnitTest :app:lintDebug
```

Optionally set `MIDAS_TEST_ROM_PATH` to your own local, stock USA `.z64` file and
use `--rerun-tasks` to exercise real ROM validation and all byte-order conversions.
The file is read locally and is never bundled or uploaded.

The touch overlay has no on-screen hide button (it used to sit beside the shoot
control). Hide/show it with **Ctrl+Shift+T** on a keyboard, or with a
**three-finger tap** on touch-only devices. Visibility is remembered. Its input
is released on hide, resize, focus loss and pause. A virtual SDL pad feeds the
existing controller backend; touch supplements player 1 rather than displacing a
physical controller.
