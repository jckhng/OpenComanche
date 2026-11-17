# OpenCommanche

A portable C++17/SDL2 reimplementation of Comanche CD using player-supplied
original installation and CD data. **0.2.0-preview1-rc1** is the enhanced
desktop release candidate for Windows x64 and Linux x86-64.

Start with **Begin a Mission → Comanche Training → Oil Tank Holiday**.
Other selectable missions remain experimental.

## Choose a player

| Executable | Role |
|---|---|
| `comanche_enhanced` | Widescreen GPU/CPU desktop rendering, interpolated presentation and configurable SDL2 controls |
| `comanche_faithful` | Recovered indexed presentation, DOS keyboard routing and native audio settings |
| `comanche_openxr` | Experimental headset player available through an optional source build |

Windows adds `.exe`. The desktop archives contain the faithful and enhanced
players, matching source and license notices. Each executable fixes its mode.
The players share the canonical simulation, recovered CPU/device work and audio.
The faithful player has a dedicated indexed SDL2 host and link graph.

## Extract and play

Extract `comanche-0.2.0-preview1-rc1-windows-x64.zip` or
`comanche-0.2.0-preview1-rc1-linux-x86_64.zip` fully. Supply the installed DOS
directory containing `COMANCHE.EXE`, `COMANCHE.NAM` and campaign MIS files,
plus the `COMANCHE` payload directory from your CD. Use a separate writable
native profile directory. Keep uppercase resource filenames on Linux.

The supported packed CD executable is 67,221 bytes with SHA-256
`1fb8ec4023d8727a644685d49be64ca608bc2bdc12431cb4a00d6a0e8e653f4d`.

From PowerShell in the extracted Windows package, substitute your own paths.

```powershell
./comanche_enhanced.exe "D:/Games/Comanche" "E:/COMANCHE" "./enhanced-profile"
```

On Linux, install the SDL2 runtime and use the corresponding directories.

```sh
./comanche_enhanced /path/to/installed/COMANCHE /path/to/cd/COMANCHE ./enhanced-profile
```

Use `comanche_faithful` with the same three path arguments for faithful mode.
Windows uses the supplied SDL2.dll and Windows Universal C Runtime. Linux
requires SDL2 2.0.10+, glibc 2.29 and libstdc++ providing GLIBCXX_3.4.26.
Enhanced GPU rendering uses OpenGL 3.2, with automatic CPU fallback.

The earlier keyboard preview remains available as RC10, using the
`comanche_viewer` filename. [RC10 downloads](https://github.com/jckhng/OpenCommanche/releases/tag/v0.1.0-preview1-rc10)

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
| Enhanced | Right mouse drag / middle mouse | Look around / recenter |
| Enhanced | Alt+Enter | Toggle desktop fullscreen |
| Enhanced | Ctrl+F1 / Ctrl+F2 | Cycle FPS limit / toggle vsync request |
| Enhanced | Ctrl+F3 | Controller bindings, calibration and prompts |
| Enhanced | Ctrl+F4 | Master, music and effects/voice volume |
| Debrief | Escape | Return to operations |

Focus loss pauses flight and releases held controls. Resume with P after
returning. Controller and audio pages pause the session. Choose Apply and Save
to commit edits. Audio levels default to 100% and cover startup playback.
Enhanced preferences persist per profile in `host-preferences.cfg`.
[Full controls, presentation options and limits](PREVIEW.md)

## Candidate scope

Enhanced desktop adds widescreen terrain/model rendering, a look-around
cockpit, camera/entity interpolation, configurable controllers, button prompts,
saved window layout, FPS/vsync settings and independent audio levels. Flight
retains nominal 20 Hz pacing and recovered DOS CPU/device timing. Presentation
interpolation adds approximately one simulation interval of visual delay.

Automated Windows/Linux checks cover the canonical state, IRQ/RNG, indexed
images, mixed PCM, settings and completed mission/save/restart flows. Retained
DOSBox-X comparisons cover declared intervals on six completed routes. All
100 missions have bounded native checks. Broader progression and cold-entry
timing/audio qualification remain open. Physical listening for the new volume
controls, gamepad, fullscreen/high-DPI and Linux desktop qualification remain
open. Linux automated qualification uses Ubuntu 20.04 under WSL.

OpenXR has synthetic session/frame and SDL2/OpenGL tests. Headset performance,
terrain stability and comfort require further physical testing. VR development
is deferred. The experimental source-build option remains available.
[Candidate changes and qualification](CHANGELOG.md)

## Build and licensing

Use CMake 3.16+, a C++17 compiler and SDL2 development files. An extracted
runtime archive contains its source under `source/`.
[Windows and Linux build instructions](BUILDING.md)

[Runtime ownership and maintenance](ARCHITECTURE.md)

Developed with DOS RE Harness and DOSBox-X original comparisons. The native
runtime builds independently of the reverse-engineering tools.
[DOS RE Harness](https://github.com/jckhng/dos-re-harness)

Original project code retains the MIT grant in LICENSE-MIT. DBOPL and adapted
DOSBox-X audio/timing components use GPL-2.0-or-later, with license text in
COPYING. Original game data and trademarks remain subject to their owners'
rights. [Third-party notices](THIRD_PARTY.md)
