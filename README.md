# OpenCommanche

A portable C++17/SDL2 reimplementation of Comanche CD using player-supplied
original installation and CD data. RC10 is a limited keyboard preview for
Windows x64 and Linux x86-64.

Start with **Begin a Mission → Comanche Training → Oil Tank Holiday**.
Other selectable missions remain experimental.

## Enhanced SDL2 development

Current source builds produce dedicated players. Each executable fixes its
mode and accepts the original installation, CD payload and native profile paths.

| Executable | Role |
|---|---|
| `comanche_faithful` | Recovered indexed presentation, DOS keyboard routing and audio |
| `comanche_enhanced` | Desktop GPU/CPU rendering, widescreen and configurable SDL2 controls |
| `comanche_openxr` | Optional OpenXR headset host |

Windows adds `.exe`. The faithful player links its own SDL2 host. Enhanced
rendering, controller bindings and OpenXR runtime code have separate link
dependencies. Simulation and audio are shared and covered by fidelity checks.
Published RC10 archives retain their `comanche_viewer` filename.

The `enhanced-sdl2` branch adds configurable gamepad response, buffered
keyboard fire/target taps, high-DPI integer scaling, optional filtered scaling
and Alt+Enter desktop fullscreen. Build this branch to try these features.

```sh
./build/comanche_enhanced /path/to/installed/COMANCHE /path/to/cd/COMANCHE ./enhanced-profile --scale integer --stick-deadzone 4000 --stick-sensitivity 150 --invert-y off
```

Enhanced desktop flight uses SDL2/OpenGL 3.2 for terrain, directional SG models,
weather, water and cockpit rendering, with camera/entity interpolation.
The default window is 1280×720. Widescreen expands the horizontal field of view
while retaining vertical framing and native instrument proportions.
`--renderer cpu` selects software world rendering. Automatic GPU admission
falls back to CPU when OpenGL is unavailable. `--aspect original` selects 4:3.
These choices persist in the enhanced profile. Desktop GPU builds need SDL2
and OpenGL development files. OpenXR support is a separate build option.
The recovered cockpit and HUD
supply the live instrument and HUD artwork for an authored spatial cockpit.
The fitted instrument panel sits inside sloping dashboard shoulders, with inward-
leaning windshield supports following the DOS artwork. Its framing is independent
of the world-aligned HUD projection.
Hold right mouse and drag to look around. Middle mouse recenters the view.
The canopy, consoles, seats and static rear crew figure are approximate enhanced
geometry. `--cockpit flat` selects the earlier overlay presentation.
Use `--cockpit-scale nearest` for native pixel blocks in flat presentation.
Use `--scene-renderer native` for the indexed
presentation or `--interpolation off` for discrete camera steps.

The player retains nominal 20 Hz flight pacing and recovered DOS CPU/device
timing. Enhanced camera and entity interpolation runs between simulation
updates, with approximately one update of visual delay. The enhanced desktop
runs its session on a worker while the main thread handles SDL2 input, rendering
and audio output. Completed snapshots feed independently paced draws. The GPU
backend requests display vsync.

Enhanced controller bindings and tuning/display preferences persist in
`host-preferences.cfg` inside the native profile. Use `--bind flight.x=fire`
for a saved action override.
[Controller configuration](PREVIEW.md#controller-bindings-and-preferences)

The faithful player uses its dedicated indexed SDL2 host. Enhanced world observations feed
portable GPU and CPU renderers through a full-pose, asymmetric-projection camera
interface and separate cockpit composition. SDL2 owns device output.
Optional OpenXR builds add an experimental SDL2/OpenGL headset host with
tracked stereo world views, a separate cockpit/menu layer and a presentation
thread. VR development is deferred pending further physical testing.
Scripted screen effects and crash presentation use the recovered indexed image.
[Enhanced controls and rendering](PREVIEW.md#enhanced-sdl2-presentation)

## Download and play

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
| Enhanced | Alt+Enter | Toggle desktop fullscreen |
| Debrief | Escape | Return to operations |

Focus loss pauses flight and releases held controls. Resume with P after
returning. The detailed preview guide covers name entry, displays,
countermeasures and experimental SDL2 controller input.
[Full controls and preview limits](PREVIEW.md)

## Build from source

The source tree builds directly from this repository with CMake 3.16+, a C++17
compiler and SDL2 development files. Build outputs stay in `build/`.
[Windows and Linux build instructions](BUILDING.md)

[Runtime ownership and maintenance plan](ARCHITECTURE.md)

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
