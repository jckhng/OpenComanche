# Building OpenComanche

Use CMake 3.16+, a C++17 compiler and SDL2 2.0.10+ development files. Obtain
dependencies separately through your toolchain or package manager.
The source root contains CMakeLists.txt. In a runtime archive, enter `source/`
before following these commands. CMake uses local dependencies.
Enhanced desktop GPU rendering requires OpenGL 3.2 at runtime. CMake enables
it when system OpenGL development files are available. Set
`COMANCHECD_BUILD_OPENGL=OFF` for a CPU-only desktop build. OpenXR SDK headers
are required only for `COMANCHECD_BUILD_OPENXR=ON`.

## Windows x64

The desktop candidate Windows toolchain is MinGW-w64 GCC 13.2.0 with SDL2 2.32.10.
Use the SDL2 development package matching your compiler and architecture.
Place CMake and the compiler tools on PATH. Set `SDL2_DIR` to your SDK's
directory containing SDL2Config.cmake.

```powershell
cmake -S . -B build -G "MinGW Makefiles" `
  -DCMAKE_BUILD_TYPE=Release `
  -DSDL2_DIR="$env:SDL2_DIR" `
  -DCOMANCHECD_BUILD_DIAGNOSTICS=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

CMake copies SDL2.dll beside each player when using its imported SDL2 target. The MinGW player statically links its compiler runtimes. Keep the
shared SDL2 DLL beside the player.

```powershell
./build/comanche_faithful.exe "D:/Games/Comanche" "E:/COMANCHE" "./profile"
```

## Linux x86-64

Desktop candidate Linux qualification uses GCC 9.4 and SDL2 2.0.10 on Ubuntu 20.04 under WSL.
Install your distribution's C++ toolchain, CMake, pkg-config and SDL2 development
package. On Debian/Ubuntu, the package names are build-essential, cmake,
pkg-config, libsdl2-dev and libgl1-mesa-dev for GPU desktop rendering.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCOMANCHECD_BUILD_DIAGNOSTICS=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/comanche_faithful /path/to/installed/COMANCHE /path/to/cd/COMANCHE ./profile
```

The runtime uses the installed SDL2 and C++ libraries. Original resources keep
their uppercase filenames on case-sensitive filesystems.

## Player targets

Default desktop builds provide `comanche_faithful` and `comanche_enhanced`.
The enhanced player selects enhanced controls/preferences on launch.

```sh
./build/comanche_enhanced /path/to/installed/COMANCHE /path/to/cd/COMANCHE ./enhanced-profile
```

A faithful-only build excludes enhanced sources, OpenGL discovery and OpenXR.

```sh
cmake -S . -B build-faithful -DCMAKE_BUILD_TYPE=Release \
  -DCOMANCHECD_BUILD_ENHANCED=OFF -DCOMANCHECD_BUILD_OPENXR=OFF \
  -DCOMANCHECD_BUILD_DIAGNOSTICS=OFF
cmake --build build-faithful --target comanche_faithful
```

Set `COMANCHECD_BUILD_OPENXR=ON` and provide
`COMANCHECD_OPENXR_INCLUDE_DIR` to add `comanche_openxr`. That executable
selects headset presentation automatically. `COMANCHECD_BUILD_ENHANCED=OFF`
can omit the enhanced desktop executable while building XR support.
Install copies each configured player, notices and buildable source. Candidate
archives include the complete source snapshot and per-file manifests.

The executable determines its mode. Matching legacy `--mode`/`--xr on`
arguments remain accepted. Cross-mode selections fail before resource loading
or profile writes. The published RC10 executable keeps its original name.

## Tests and installation

CTest runs resource-free suites, including desktop layout, CPU fallback and
real OpenGL checks. OpenGL checks skip under SDL dummy video.
Linux retains an existing controller test skip on SDL2 versions lacking its virtual
controller support.

Optional integration checks read your own supported game data. Run from the
repository root, substituting your directories. Use `.exe` on Windows.

```sh
./build/viewer_tests /path/to/installed/COMANCHE /path/to/cd/COMANCHE
./build/session_tests /path/to/installed/COMANCHE /path/to/cd/COMANCHE
```

Installation produces the player, notices and matching source.

```sh
cmake --install build --prefix dist/OpenComanche
```

Use `-DBUILD_TESTING=OFF` for a player-only build. Set
`COMANCHECD_BUILD_DIAGNOSTICS=ON` to build the retained diagnostic tools.
Build and test artifacts belong in ignored output directories.

## Enhanced input and experimental OpenXR

Run `comanche_enhanced` to enable experimental SDL2 GameController input
and presentation options. PREVIEW.md describes stick
tuning, high-resolution terrain/model presentation, camera interpolation,
integer/filtered scaling and the fullscreen shortcut. Physical
controller, fullscreen and high-DPI qualification remains open.

The optional OpenXR host supports tracked stereo rendering, frame submission
and a spatial cockpit through SDL2/OpenGL. It requires independently installed
Khronos SDK headers. VR development is deferred pending further physical
testing. See PREVIEW.md for build options. The desktop candidate archives contain the faithful and enhanced players.
OpenXR is available through the experimental source-build option.

## Keyboard mission smoke test

The `sdl_keyboard_route` diagnostic exercises pilot entry, pause/focus recovery,
settings, a training mission, debrief and profile persistence using SDL2 key
events. Keep its window focused. Use fresh profile/output directories for play.

```sh
./build/sdl_keyboard_route INSTALLATION PAYLOAD_DIRECTORY TEST_PROFILE PLAY_OUTPUT play --mode enhanced --renderer gpu --fps 75
./build/sdl_keyboard_route INSTALLATION PAYLOAD_DIRECTORY TEST_PROFILE RESTART_OUTPUT restart --mode enhanced --renderer gpu --fps 75
```

Its test-only pilot samples completed flight frames, retains a live objective
and brakes toward a stand-off range even without a target lock. Unit regressions
cover lost-lock braking, pursuit changes and keyboard press/release scheduling.
Keep generated profiles and traces outside the source tree.

## Source and redistribution

SOURCE_MANIFEST.json identifies the candidate version, frozen feature commit
and source-file hashes, excluding the manifest itself. Its baseline release
and archive hashes identify the earlier RC10 snapshot. Each runtime ZIP has
a separate `manifest.json` identifying its source commit and file hashes.
Canonical work accounting and simulation are shared across host modes.
Spatial observation hooks are opt-in. Candidate ZIPs contain matching source
and notices. The published RC10 tag retains its earlier source snapshot.

Player-supplied game files, native profiles, captures, SDKs and local build
configuration stay outside the published tree. Preserve LICENSE-MIT, COPYING
and the third-party notices when redistributing source or binaries.
