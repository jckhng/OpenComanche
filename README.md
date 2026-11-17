# OpenCommanche

A portable C++17/SDL2 reimplementation of Comanche CD using player-supplied
original installation and CD data. The enhanced desktop prerelease targets
Windows x64 and Linux x86-64.

Start with **Begin a Mission → Comanche Training → Oil Tank Holiday**.
Other selectable missions remain experimental.

## Enhanced desktop prerelease

**0.2.0-preview1-rc1** provides dedicated faithful and enhanced desktop players,
with widescreen GPU rendering, interpolated presentation, a look-around
cockpit, controller configuration and saved display/audio preferences.
Both platform ZIPs contain matching source and license notices.
[Download the Windows or Linux candidate](https://github.com/jckhng/OpenCommanche/releases/tag/v0.2.0-preview1-rc1)

Current enhanced source and build instructions are on the `enhanced-sdl2`
branch. The default `main` branch contains the RC10 keyboard-preview source.
[Enhanced source](https://github.com/jckhng/OpenCommanche/tree/enhanced-sdl2) · [Enhanced build instructions](https://github.com/jckhng/OpenCommanche/blob/enhanced-sdl2/BUILDING.md)

OpenXR remains an experimental source-build option. Full physical headset
testing, terrain stability, performance and comfort remain open.

## Gameplay footage

[![DOSBox-X original and enhanced desktop comparison](https://github.com/jckhng/OpenCommanche/releases/download/v0.2.0-preview1-rc1/DOS-vs-Enhanced-preview.jpg)](https://github.com/jckhng/OpenCommanche/releases/download/v0.2.0-preview1-rc1/OpenCommanche-RC1-DOS-vs-Enhanced.mp4)

**DOS vs enhanced · 25 seconds.** Original DOSBox-X footage alongside the same
recorded flight replayed through the RC1 enhanced GPU renderer. Widescreen,
terrain, cockpit and interpolation differences share recorded scene states and
original DOS audio. [Watch or download the MP4](https://github.com/jckhng/OpenCommanche/releases/download/v0.2.0-preview1-rc1/OpenCommanche-RC1-DOS-vs-Enhanced.mp4)

[![Experimental OpenXR scripted stereo preview](https://github.com/jckhng/OpenCommanche/releases/download/v0.2.0-preview1-rc1/OpenXR-Stereo-preview.jpg)](https://github.com/jckhng/OpenCommanche/releases/download/v0.2.0-preview1-rc1/OpenCommanche-RC1-OpenXR-Stereo-Preview.mp4)

**Experimental OpenXR · 36 seconds.** Scripted stereo head tracking through the
GPU eye renderer shows the surrounding cockpit and rear crew station. Both
clips are offline 60 fps captures with checked audio alignment. Full physical
headset testing, terrain stability, performance and comfort remain open.
[Watch or download the stereo MP4](https://github.com/jckhng/OpenCommanche/releases/download/v0.2.0-preview1-rc1/OpenCommanche-RC1-OpenXR-Stereo-Preview.mp4)

## RC10 download and play

Download the Windows or Linux ZIP from the RC10 release and extract it fully.
Both archives include matching source and third-party notices.
[RC10 downloads](https://github.com/jckhng/OpenCommanche/releases/tag/v0.1.0-preview1-rc10)

Provide the installed DOS directory containing `COMANCHE.EXE`, `COMANCHE.NAM`
and campaign MIS files, plus the `COMANCHE` payload directory from your CD.
The supported packed CD executable is 67,221 bytes with SHA-256
`1fb8ec4023d8727a644685d49be64ca608bc2bdc12431cb4a00d6a0e8e653f4d`.
Keep the native profile in a separate writable directory.

From PowerShell in the extracted Windows package, use your own paths.

```powershell
./comanche_viewer.exe "D:/Games/Comanche" "E:/COMANCHE" "./profile"
```

On Linux, preserve uppercase resource filenames on case-sensitive filesystems.

```sh
./comanche_viewer /path/to/installed/COMANCHE /path/to/cd/COMANCHE ./profile
```

Windows uses the supplied SDL2.dll and the Windows Universal C Runtime.
The Linux release needs SDL2 2.0.10+, glibc 2.29 and libstdc++ providing
GLIBCXX_3.4.26. The runtime reads original resources locally and stores pilot
progress/settings in the native profile directory.

## Controls

| Context | Keys | Action |
|---|---|---|
| Menus | Arrows, Enter, Escape | Navigate, accept, return/settings |
| Flight | Arrows | Steer |
| Flight | E / D | Increase / decrease throttle |
| Flight | Tab / Enter | Acquire or cycle target |
| Flight | [ / ] | Select weapon |
| Flight | Space | Fire |
| Flight | Escape | In-flight settings |
| Flight | Alt-A | Abort to debrief |
| Session | P | Pause/resume outside pilot name entry |
| Debrief | Escape | Return to operations |

Focus loss pauses flight and releases held controls. Resume with P after
returning. The detailed preview guide covers name entry, displays,
countermeasures and experimental SDL2 controller input.
[Full controls and preview limits](PREVIEW.md)

## Build from source

The source tree builds directly from this repository with CMake 3.16+, a C++17
compiler and SDL2 development files. Build outputs stay in `build/`.
[Windows and Linux build instructions](BUILDING.md)

## RC10 scope

The Windows physical playtest covers pilot/menu input, flight controls, audio,
pause/focus recovery, resizing, Oil Tank Holiday victory, debrief, pilot save
and separate-process restart. The saved pilot record remains byte-identical
across restart. Both platforms pass archive-source rebuilds and automated
training/debrief/save/restart/abort checks. Linux qualification uses Ubuntu
20.04 under WSL.

Retained DOSBox-X comparisons cover declared state, RNG, debrief, saves and
mixed audio on five additional completed routes. All 100 missions have bounded
native checks. Continuous cold-training audio, broader command/progression
coverage and wider rendering/hardware qualification remain open. Physical
Linux desktop/controller checks and OpenXR gameplay remain open.
[RC10 changes and qualification](CHANGELOG.md)

## Credits and licensing

Developed with DOS RE Harness using DOSBox-X original comparisons. The native
runtime builds independently of the reverse-engineering tools.
[DOS RE Harness](https://github.com/jckhng/dos-re-harness)

Original project code retains the MIT grant in LICENSE-MIT. DBOPL and adapted
DOSBox-X audio/timing components use GPL-2.0-or-later, with license text in
COPYING. SDL2 and other retained third-party notices accompany the source.
Original game data and trademarks remain subject to their owners' rights.
[Third-party notices](THIRD_PARTY.md)
