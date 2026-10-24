# Midas Android — port notes

This directory is the Gradle shell around the shared native port, based on
[Project Midas](https://github.com/murkantor/project-midas).

The published 1.0.x APKs are **32-bit (`armeabi-v7a`) only**. They run on any
device that still supports 32-bit apps (most pre-2024 phones, the Retroid
Pocket, ...) and are refused by 64-bit-only devices (Galaxy S24+, Pixel 7+,
Xiaomi Pad 7, ...) with `INSTALL_FAILED_NO_MATCHING_ABIS`, which the OEM
installer shows as "App not installed" or "app is not compatible".

The 1.0.2 build defaults to a **universal APK** (`arm64-v8a` + `armeabi-v7a`).
Packaging, signing and installation on 64-bit-only devices work. The ARM64
engine now boots through ROM data, textures, fonts and input and loads a stage;
it then dies when the first menu model is parsed (`ModelNode` is a 24-byte
32-bit structure on the wire, 48 bytes as a host struct). See "ARM64 status"
below. Do not publish the universal APK until the asset-struct conversion is
finished.

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

## ARM64 status

The universal packaging is done; the ARM64 engine is the remaining work. On a
Galaxy S24 Ultra (SM-S928B, Android 16, `arm64-v8a` only) and on a 16 KB-page
arm64 emulator image the universal APK installs and the ROM launcher works. The
engine now gets through `bossInitMainthreadData` (ROM data, textures, fonts,
input), loads a stage, converts and draws the menu models, and reaches the
Rareware-logo intro; it currently dies inside that intro (`guMtxF2L` writing
through `matrixBufferRareLogo0`, which changes to `0x10000` between the first
and second frame - something overwrites that global, next lead).

Fixed on the way to that point (all 64-bit-only defects; 32-bit behaviour is
unchanged):

- `port/src/pcsched.c` — arenas must stay inside the 32-bit-addressable range.
  `portAllocHighRaw` now requires `0x10000000 <= addr < 0x80000000` on 64-bit
  hosts and uses `MAP_FIXED_NOREPLACE`, otherwise mmap's hint is advisory and
  the first `romCopy` truncates the ROM pointer.
- `port/src/libultra_shim.c` + `include/PR/os.h` — `osPiStartDma` /
  `osPiRawStartDma` take a `uintptr_t devAddr` (ROM "addresses" are host
  pointers here).
- `port/src/segments.c` + `port/src/animdata.c` — the animation segments are
  allocated in the low arena (not `.bss`), both animation tables are walked at
  pointer width, and `struct ModelAnimation` stores `bitDescriptors`/`bitStream`
  as 32-bit low-arena addresses.
- `src/game/initanitable.c` — `animation_table_ptrs1` is a pointer array (it was
  `s32[]`, which made the shared walker overrun into `ptrs2`).
- `src/game/image_bank.c` — image/DL rebasing is pointer-width; `texSetBitstring`
  takes a `uintptr_t`.
- `src/game/image.c` — `texLoad` aligns its stack buffer via `uintptr_t`, not
  `u32`.
- `src/game/textrelated.c` + `struct fontchar` — `pixeldata` is a 32-bit
  low-arena address (the font record is 24 bytes on the wire).
- `src/game/rsp.c` — the two `g_gfxTaskSettings` pointers are toggled with a
  compare-and-swap, not a `u32` XOR.
- `src/sched.c` — `osScAddClient` no longer writes `c[1].next` past the client.
- `src/boss.c` — `rspReplyMsg` is an `OSMesg` (a stack pointer was truncated to
  `s32`), and the gfx message queue is rebuilt after a stage load (its control
  block is still clobbered by the load; the overwrite is not yet found).
- `port/src/model64.c` (new) — converts a ROM model (24-byte wire nodes and
  rodata with 32-bit link words) into a host `ModelNode`/`ModelRoData` graph
  allocated from `MEMPOOL_STAGE`; vertex/DL/image data stays in the wire image
  and `BaseAddr`/`Vertices`/`Primary`/... point into it. `load_object_fill_header`
  calls it on 64-bit instead of `sub_GAME_7F075A90`.
- `src/game/tex.c` — `texLoadFromGdl` reads 8-byte wire commands into a local
  host `Gfx` and writes 16-byte host commands; `texCopyGdls` is byte-addressed.
  `portConvertModelGdl` (objecthandler_2.c) scans wire lists and recursively
  expands nested `G_DL` targets.
- `port/src/wiredl.c` (new) — registry of 8-byte-command ranges (ROM data
  segment, font DL copy, every model image). `gfx_run_dl` expands such a list
  into a cached host `Gfx` array before walking it, which covers font/logo/
  gun-barrel lists that are never converted by the texture pass.
- `src/game/model.c` — `modelGetNodeRwData` addresses the rwdata buffer in
  bytes (indices count 4-byte cells; `data + index` strided by 8 on 64-bit) and
  skips the walk when a model has no rwdata buffer. `modelAttachPart` does the
  same byte addressing.
- `src/game/objecthandler.h` — `ModelSlot`/`AnimModelSlot` are cast to `Model*`;
  on 64-bit their mirrored fields (`unk08`=obj, `unk0c`=render_pos,
  `unk10`=datas) are real pointers at Model's offsets, and the structs are at
  least `sizeof(Model)` so a Model write cannot run into the next slot.
- `port/src/pcsched.c` — memp gets its own 64 MB mapping
  (`portAllocHighDedicated`); taking it from the shared high arena consumed all
  of it and later `portAllocHigh` calls failed.
- `include/PR/os.h` + `port/src/libultra_shim.c` — `osVirtualToPhysical`,
  `OS_K0_TO_PHYSICAL`, `OS_PHYSICAL_TO_K0` are pointer-width (they feed display
  list words and segment registers).
- `src/game/title.c` — `virtualaddress` (rareware-logo segment base) is
  `uintptr_t`, not `s32`; its setter and the gun-barrel entry point have real
  prototypes in `title.h`.
- `port/fast3d/gfx_pc.cpp` — a matrix load whose resolved address is below
  `0x10000` is skipped instead of crashing (temporary; the producer is still to
  be found).

Temporary bring-up switches, must be removed before a release:

- `src/game/chrai.h` — 64-bit `MONJUMPTO`/`MONJUMPCHANCE` emit a null target so
  the engine compiles; animations that jump do nothing.
- `src/music.c`, `src/snd.c`, `port/src/audio_pc.c` — the AL bank/sequence data
  is a 32-bit structure tree, so arm64 builds the synth/players with an empty
  host-format bank and mute playback (`g_sndBootswitchSound`). No sound yet.

Remaining work, in the order a run hits it:

1. **Rareware-logo intro (current crash).** `matrixBufferRareLogo0` is valid on
   the first frame and reads `0x10000` on the second; `load_display_rare_logo`
   then calls `guPerspective` with it. The global is being overwritten - find the
   writer (the `alloc_intro_matrices` buffers and every `Mtx *` global in
   title.c are candidates).
2. **Display lists.** Runtime-built lists are host `Gfx` (16 bytes) and the
   wire lists are expanded now, but check every remaining `Gfx` producer for
   `(u32)` casts (`OS_K0_TO_PHYSICAL`-style) that truncate on 64-bit.
3. **Audio.** Convert `ALBankFile`/`ALInstrument`/`ALSound`/`ALWaveTable` and
   the Rare sequence table into host structs (or 32-bit address fields), then
   drop the mute/empty-bank switches.
4. **The rest of the 32-bit address model:** `src/game/ob.c`
   (`(u32) offset + 0x0F000000`, `(u32 *) cand`), `src/game/bg.c:1044`
   (`ptr_bg_data = (s32) mempAllocBytesInBank(...)`), `src/game/bondview_r.c:126`,
   `port/src/romdata.c:929` (host pointers into 32-bit text slots), and the
   remaining `romfile_*` converters.

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

The touch overlay has a persistent HIDE/PAD toggle and long-press opacity control.
Its input is released on hide, resize, focus loss and pause. A virtual SDL pad
feeds the existing controller backend; touch supplements player 1 rather than
displacing a physical controller. Device input, graphics, audio and all PC options
still require end-to-end verification after the native engine builds.
