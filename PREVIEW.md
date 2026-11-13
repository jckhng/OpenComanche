# Comanche CD native preview

An experimental C++17/SDL2 reimplementation using your original Comanche CD
installation and CD payloads. Start with Comanche Training, Oil Tank Holiday.
That mission has a retained DOSBox-X comparison through objectives, debrief
and pilot save. Five additional completed routes cover Flying Werewolves,
Deadman's Grotto, Wing Leader, Siberian Chill and The Kilauea Encounter, with
matching declared state, RNG, debrief, saves and mixed audio. All 100 missions
have bounded native checks. Other selectable missions remain experimental.

## Run

Extract the entire archive. Windows needs its supplied SDL2.dll beside the
executable and the Windows Universal C Runtime. Both binaries target x86-64.
The Linux binary requires glibc 2.29 or later, libstdc++ providing
GLIBCXX_3.4.26, and an installed SDL2 runtime, version 2.0.10 or later.
Linux qualification currently uses Ubuntu 20.04 under WSL.

Provide the installed DOS game directory containing COMANCHE.EXE, COMANCHE.NAM
and campaign MIS files, and the COMANCHE payload directory from your CD.
Use the original uppercase resource filenames on case-sensitive filesystems.
The supported COMANCHE.EXE is the 67,221-byte packed CD specimen with SHA-256
`1fb8ec4023d8727a644685d49be64ca608bc2bdc12431cb4a00d6a0e8e653f4d`.

From PowerShell in the extracted directory, use your own paths.

```powershell
./comanche_viewer.exe "D:/Games/Comanche" "E:/COMANCHE" "./profile"
```

On Linux, use the corresponding directories.

```sh
./comanche_viewer /path/to/installed/COMANCHE /path/to/cd/COMANCHE ./profile
```

The separate profile directory stores COMANCHE.NAM and COMANCHE.MNU. Existing
records receive a .bak before replacement. Keep that directory outside the
original installation and retain backups of valued profiles. A first launch
uses the original installation's pilot/settings defaults.

## Controls

| Context | Input | Action |
|---|---|---|
| Menus | Arrows, Enter, Escape | Navigate, accept, return/settings |
| Pilot name | Letters, Shift, Backspace, Enter | Edit and accept |
| Flight | Arrows | Steer |
| Flight | E / D | Increase / decrease throttle |
| Flight | Tab / Enter | Acquire or cycle target |
| Flight | [ / ] | Previous / next weapon |
| Flight | Space | Fire, hold for cannon |
| Flight | Semicolon / apostrophe | Chaff / flare |
| Flight | F1–F6 / F7–F12 | Left / right instrument display |
| Flight | 1 / 6 / 7 / 8 | Cockpit / chase / fixed / fixed view |
| Session | P | Pause/resume, outside pilot name entry |
| Flight | Escape | Open in-flight settings |
| Flight | Alt-A | Abort to debrief |
| Debrief | Escape | Return to operations |
| Window | Close | Exit |

Focus loss pauses and releases held controls. Resume with P after returning.
The default faithful input profile uses the keyboard. Views 2–5, J calibration,
the M shortcut and legacy input-mode/peripheral settings are unavailable in this
preview. They display a notice in the window title and retain the session.
Use [ / ] and Space for the supported weapon selection and firing path.
These notices are preview behavior. The corresponding DOS children still need
recovery. Supported commands clear the notice.

## Enhanced SDL2 input

Append `--mode enhanced` to enable SDL2 GameController input. Both input profiles
use the same simulation. Enhanced flight selects spatial rendering by default.
Enhanced input adds analog
steering and buffers short action presses until the next simulation tick.
Keyboard Space, Tab and Enter taps are buffered in enhanced flight. Held fire
continues normally. Pause, focus loss and context changes cancel queued edges
and require held actions to be released before rearming.

```sh
./comanche_viewer /path/to/installed/COMANCHE /path/to/cd/COMANCHE ./profile --mode enhanced
```

| Context | Controller input | Action |
|---|---|---|
| Menus | D-pad, A, B | Navigate, accept, return/settings |
| Flight | Left stick | Steer, with a per-axis deadzone |
| Flight | D-pad Up / Down | Increase / decrease throttle |
| Flight | A / right trigger | Fire |
| Flight | Y | Acquire target |
| Flight | X / B | Chaff / flare |
| Flight | Left / right shoulder | Previous / next weapon |
| Flight | Back | Open settings |
| Session | Start | Pause/resume outside pilot name entry |

Pilot names use the keyboard. Disconnect pauses flight. A held menu button
requires release before becoming a flight action. Returning focus or changing
context requires neutral steering/trigger axes. An optional
`--controller-mappings FILE` loads a platform-filtered SDL2 mapping database.
The original DOS joystick settings remain separate from this host input
profile. Physical controller qualification remains open. The mapping boundary
uses SDL2's GameController API. [SDL2 controller documentation](https://wiki.libsdl.org/SDL2/SDL_GameControllerOpen)

| Enhanced option | Default | Meaning |
|---|---|---|
| `--stick-deadzone 0..32766` | 8000 | Per-axis deadzone in signed SDL2 stick units |
| `--stick-sensitivity 1..400` | 100 | Steering gain in percent, clamped to the recovered control range |
| `--invert-y on\|off` | off | Invert gamepad pitch |
| `--scale nearest\|linear\|integer` | integer | Image scaling and viewport policy |
| `--scene-renderer spatial\|native` | spatial | High-resolution world or recovered indexed presentation |
| `--interpolation on\|off` | on | Presentation-only camera and entity interpolation |
| `--cockpit-scale nearest\|sharp` | sharp | Native overlay sampling or mask-aware Scale2x edges |

These options require `--mode enhanced`. Keyboard steering takes precedence
over analog steering. Controller neutral detection uses the selected deadzone.

## Controller bindings and preferences

Enhanced launches load `host-preferences.cfg` from the native profile directory.
Command-line values override saved values and persist after successful SDL2
host initialization. An initial enhanced launch creates the file. It stores
bindings, steering axes, stick tuning, mapping-file location, scaling, scene
renderer and interpolation. Select `--mode enhanced` on each launch to use it.

```sh
./comanche_viewer /path/to/installed/COMANCHE /path/to/cd/COMANCHE ./profile --mode enhanced --bind flight.a=target --bind flight.x=fire --steer-x rightx --steer-y righty
```

Repeat `--bind CONTEXT.CONTROL=ACTION` to change individual controls. Contexts
are `menu` and `flight`. Button names are `a`, `b`, `x`, `y`, `back`, `guide`,
`start`, `leftstick`, `rightstick`, `leftshoulder`, `rightshoulder`, `dpup`,
`dpdown`, `dpleft` and `dpright`. Flight also accepts `lefttrigger` and
`righttrigger`. Several controls can share an action. `none` unbinds a control.

| Context | Actions |
|---|---|
| Menu/name entry | `accept`, `back`, `up`, `down`, `left`, `right`, `pause`, `none` |
| Flight | `fire`, `target`, `chaff`, `flare`, `previous-weapon`, `next-weapon`, `throttle-up`, `throttle-down`, `settings`, `pause`, `none` |

Pause retains its name-entry suppression. Buttons and triggers retain release
ownership, focus/context suppression and neutral-axis rearming after remapping.
`--steer-x` and `--steer-y` select `leftx`, `lefty`, `rightx`, `righty` or `none`.
Keyboard steering retains precedence. Triggers emit an action on crossing the
press threshold. A trigger bound to `fire` also supplies held fire.

The text preference file accepts `#` comment lines and partial overrides with
`version=1`. For example, these lines select the right stick and move fire.

```ini
version=1
steer-x=rightx
steer-y=righty
bind.flight.a=target
bind.flight.x=fire
```

The existing tuning/presentation option names are file keys, without `--`.
Mapping paths in this file resolve relative to the profile directory.
Command-line mapping paths resolve relative to the working directory and save
as absolute paths. An empty `controller-mappings=` restores built-in mappings.
Deleting the file restores the enhanced defaults. Invalid files report an error
before launching an enhanced session. Equivalent preferences preserve comments
and formatting. Changes save a complete replacement and retain the previous
file as `host-preferences.cfg.bak`.

Host preferences have a separate file and lifetime from the DOS pilot/settings
records. Faithful launches use their existing keyboard and DOS settings route.
Physical remapped-controller qualification remains open.

## Enhanced SDL2 presentation

Enhanced windows request high-DPI output. Integer scaling uses the largest
whole-number multiple of 320×240 that fits the output, with letterboxing.
Outputs smaller than the native image use fractional downscaling to keep the
whole cockpit visible. Nearest scaling fits the 4:3 image continuously. Linear
filtering applies to indexed presentation through the selected SDL2 driver,
including its software fallback behavior. Spatial output retains a
nearest-scaled cockpit and HUD.
[SDL2 integer scaling](https://wiki.libsdl.org/SDL2/SDL_RenderSetIntegerScale),
[SDL2 texture filtering](https://wiki.libsdl.org/SDL2/SDL_HINT_RENDER_SCALE_QUALITY)

Alt+Enter toggles desktop fullscreen in enhanced mode. Its Enter is consumed
by the host presentation shortcut. F1–F12 retain their instrument bindings.
The faithful launch profile keeps nearest scaling and its existing key route.

Enhanced flight renders the 1024×1024 toroidal height/color maps into an
output-sized color/depth buffer. The CPU terrain mesh uses distance-dependent
rings with stitched detail boundaries and bilinear vertex heights. Perspective
texture sampling retains the original color-map density. Distance haze uses
quarter/half RGB fog blends at the recovered distance-table boundaries.
Panoramic and flat sky settings follow the selected scenery policy. SG models
use depth-tested directional/animated billboards with their original frames,
origins, transparency, mirroring, elevation tables and rotor layers.

At the default window size, the world is 960×720. The selected 4:3 viewport
sets the resolution after high-DPI and integer-scaling rules. The CPU renderer
caps its output at 3200×2400. The cockpit artwork, HUD and instrument monitors
retain native source resolution. A separate compositor uses the scene coverage
mask, including HUD colors shared with terrain. Enhanced spatial presentation
defaults to mask-aware Scale2x edges at outputs of at least 640×480. Native-sized
and smaller outputs use nearest sampling. `--cockpit-scale nearest` selects
the original pixel blocks at every size. The choice persists in host preferences. Crash presentation, scripted
screen-effect frames and reflected-water scenes retain indexed presentation.
`--scene-renderer native` selects that presentation throughout the session.

Camera and entity interpolation span the previous and current world
observations over one shared simulation interval, adding one interval of visual
latency. Position, heading and rotor phase follow the shortest wrapped path.
Pause freezes presentation time. Mission/resource/view changes, frame gaps and
large jumps reset history. Entity activation, phase/model changes and broken
projectile pose histories snap to the current observation. Sprite animation
frames and native instruments use the current canonical state.
`--interpolation off` displays current camera and entity transforms.

The session optionally exports immutable terrain/model resources and frame
transforms at its original world-drawing boundary. Capturing and rendering
these observations preserves CPU/IRQ/RNG accounting, simulation and mixed PCM.
The spatial renderer accepts a separate camera and owns no SDL2 devices or
session clock. Its camera interface supports right-handed quaternion poses,
asymmetric tangent frusta and explicit near/far clipping. World-only output
provides RGBA color and positive forward depth in world units. Per-eye pose
composition accepts tracked metre offsets with an explicit world-units-per-metre
scale. This supports software eye views and separate cockpit composition.
The optional OpenXR adapter consumes these views on its own presentation thread.
Clipping fast paths, incremental raster weights and reused terrain vertex rows
reduce CPU rendering work. Renderer performance depends on output resolution.

Automated Windows and Linux checks cover spatial rendering, camera wrapping,
compositor coverage and continuous indexed/spatial capture parity. SDL2 route
checks cover pause, focus, settings, mission completion, debrief, save, restart
and abort. Physical controller, fullscreen, high-DPI and interpolated-motion
qualification for this development branch remain open. The scripted Windows
pilot intermittently stalls acquiring an objective. Completed route witnesses
cover the declared flows, while that automation remains timing-sensitive.

## Experimental OpenXR headset host

Optional builds require OpenXR SDK headers, an OpenXR loader, OpenGL development
libraries and SDL2. Linux also requires X11 development libraries and SDL2's
X11/GLX video driver. Windows uses WGL through the SDL2-created context.
Graphics requirements are queried before context/session creation.

```sh
cmake -S source -B build -DCOMANCHECD_BUILD_OPENXR=ON -DCOMANCHECD_OPENXR_INCLUDE_DIR=/path/to/OpenXR-SDK/include
cmake --build build --target comanche_viewer
./build/comanche_viewer --openxr-info
./build/comanche_viewer /path/to/installed/COMANCHE /path/to/cd/COMANCHE ./xr-profile --mode enhanced --xr on --xr-world-scale 8 --xr-render-scale 0.5
```

`--xr-world-scale` explicitly supplies map cells per metre. The example's value
of 8 is a trial scale. The reconstructed renderer has no qualified physical
metre conversion. `--xr-render-scale` accepts 0.1 through 1 and defaults to 0.5.
Each eye uses the runtime's recommended aspect ratio with a 1024-pixel dimension
cap. Both values are launch-only. Use a separate native profile when testing XR.

The loader must be available as `openxr_loader.dll` on Windows or
`libopenxr_loader.so.1` on Linux. `--xr-loader FILE` selects an installed or
locally built loader explicitly. The information query also accepts that option.
Install and select a vendor runtime and connect its headset before launching.
On Linux, select `SDL_VIDEODRIVER=x11` when the desktop uses Wayland.

The XR thread owns session events, frame waits, predicted-time view location,
image acquire/wait/release and layer submission. An immutable mailbox holds one
scene across both eyes while the simulation and SDL2 audio continue separately.
The desktop uses an indexed mirror with immediate presentation. Its keyboard
and gamepad remain the input devices. XR focus loss releases controls and pauses
playback. Press P after focus returns, including an initially paused startup.

Flight submits a stereo world layer and a separate native cockpit/HUD quad
fixed in aircraft reference space. Menus, indexed effects, reflected-water and
crash scenes use a head-locked 4:3 screen. Invalid tracking suppresses the world
layer. Local reference-space changes reanchor tracked poses at their effective
predicted display time. Screen transitions retain original indexed artwork.

Synthetic tests cover session restart, hidden frames, tracking loss, reference
changes, frame ordering, waited-image ownership and exception cleanup. Real
SDL2/OpenGL checks cover texture orientation, channels, alpha and color space.
Physical headset validation, frame-rate/comfort qualification, XR actions and
3D cockpit interaction remain open. The world renderer still runs on the CPU.
[Khronos frame submission](https://github.com/KhronosGroup/OpenXR-Guide/blob/main/chapters/frame_submission.md)

## Preview limits

- SDL2 prebuffers 100 ms of audio at startup and after clearing its queue.
  This host playback latency protects against renderer waits. The recovered
  CPU clock and generated PCM retain their original ordering.
- Flight pacing has a 20 Hz ceiling and accounts for longer recovered CPU/device
  work. Each frame drains its complete generated audio block. Presentation
  queue resets discard unplayed transition audio, including buffered loading
  music.
- The five completed comparison routes match their declared mixed-audio
  intervals. Siberian Chill matches all 2,296 observed IRQ0 entry timestamps
  from first flight loop through first debrief. These comparisons condition
  the initial clock state and retain their recorded input calendars.
- Oil Tank Holiday's continuous cold-entry audio, broader cold-menu timing,
  final-mission victory/unlock and alternate-campaign progression remain
  unqualified. Kilauea's loss comparison uses an initial progress fixture.
- Keyboard bindings above describe the native preview. Original command and
  modal coverage is incomplete. Some settings effects remain unverified.
- Rendering fidelity outside retained routes and broader hardware testing
  remain open.

When reporting a problem, include the archive version, OS, selected campaign
and mission, steps, and the console error. Keep original assets and personal
pilot files local.

## Source and notices

The archive includes matching source under source/. Build it with CMake 3.16+,
a C++17 compiler and SDL2 development files.

```sh
cmake -S source -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target comanche_viewer
```

When SDL2 is outside CMake's search path, add
`-DSDL2_DIR=/path/to/SDL2/cmake` to the configure command. Use the development
package matching your compiler and architecture.

Original project code retains the MIT notice in LICENSE-MIT. The application
links GPL-2.0-or-later DBOPL and adapted DOSBox-X audio code. COPYING and
source/THIRD_PARTY.md describe those components. SDL2's notice is SDL2-LICENSE.txt.
Source includes the retained third-party notices and build files.
