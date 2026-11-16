# Code structure and maintenance

## Runtime ownership

| Owner | Responsibility |
|---|---|
| `GameSession` | Startup, frontend/flight transitions, carried audio, input and session observations |
| `Frontend` | Pilot editing, menus, mission/briefing selection and debrief presentation |
| `TrainingFlight` | Mission lifetime, frame transactions, cockpit pages, effects and recorded flights |
| `FlightController` | Flight, entities, weapons, collisions, objectives and simulation RNG |
| `MissionAudio` | Music/effects, execution clock, IRQ subscriptions, device scheduling and PCM |
| SDL2 host | Device input, window/output, host pause and ordered audio delivery |
| Spatial renderer | Enhanced world presentation and camera interpolation from immutable observations |

The canonical frame includes rendering-dependent targeting and CPU accounting.
Its camera, terrain, entity, model, controller and instrument operations have
an established order. Integer widths, wrap/truncation, aliases, RNG reads and
writes, IRQ delivery and page/palette work form part of that contract.

`SessionPresentation` and `SpatialFrame` hold completed observations.
Additional rendering reads those observations. `MissionAudio` produces PCM
through a separate ordered path. Both presentation modes share the canonical
session, simulation and audio implementation.

## Player boundaries

`comanche_faithful`, `comanche_enhanced` and optional `comanche_openxr` have
separate entry points and fixed launch modes. `launch_options.cpp` rejects
cross-mode selections before loading game resources or writing profiles.
`session_app.cpp` shares profile validation, cold session construction and
the established session loop.

The faithful target links `FaithfulSdlHost`, the indexed-only SDL presenter,
raw DOS key conversion and the SDL audio device. It requests indexed
observations and reads original-format pilot/menu settings. Enhanced renderer,
controller, host-preference and XR objects are outside this link graph.
The enhanced player links `SdlFlightHost` and desktop preferences. XR runtime
and platform graphics bindings belong to `comanche_openxr`.

The enhanced desktop entry point selects `run_presentation`. Its simulation
callback owns `GameSession` on one worker through the existing session loop.
The caller thread owns SDL2 event polling, window/rendering calls and audio
device output. A single completed snapshot feeds repeated interpolated draws.
Buffered input carries ordered DOS bytes, held controls and one-shot presses.
Context/playback changes, control resets and intermediate images are acknowledged
on the device thread. Input context is applied before publishing its snapshot.
Audio clears invalidate pending samples from preceding queue generations.
Shutdown joins the worker before releasing session or device ownership.

`COMANCHECD_BUILD_ENHANCED=OFF` with OpenXR disabled builds the faithful host
independently of enhanced sources and graphics development packages. Shared
simulation, native drawing and audio retain their DOS parity checks. The
faithful host runs the existing keyboard/display/audio regression fixture.
Enhanced behavior belongs in enhanced adapters or explicit enhanced policies.
Changes to shared simulation/audio retain the faithful comparison requirements.

## Host input and preferences

`host/controller_bindings.hpp` defines logical controller slots and `HostAction`
identifiers independently of SDL2 headers. The SDL2 adapter translates device
events into those actions and the established `HostInput` boundary. Future XR
actions can use the same host action vocabulary and simulation input contract.

`host/host_preferences.cpp` owns versioned text parsing and staged replacement
inside the native profile. Command-line overrides share the option validator.
`session_main.cpp` supplies the SDL host with a preference-writer callback.
The desktop host debounces complete snapshots of its presentation settings and
normal window placement, flushes on close and retains failed writes for retry.
`display_settings.cpp` validates portable values and advances steady-clock frame
deadlines. `sdl_display.cpp` owns refresh-rate lookup, window restoration and
SDL-timed waits on the device thread. GPU swap control and SDL renderer
recreation remain in their presentation adapters. The OpenXR worker retains
runtime frame timing and its mirror starts uncapped. Context/focus suppression, trigger neutral rearming
and shared logical-key ownership belong to the input adapter. Original-format
pilot and menu settings retain their existing persistence owners.

## Controller editor and calibration

`controller_configuration.cpp` validates and normalizes physical stick ranges,
formats logical button labels and resolves action prompts from active bindings.
`SdlController` exposes raw device observations and accepts complete validated
configurations. Reconfiguration releases its prior logical keys and requires
neutral axes/button release before gameplay resumes.

`ControllerMenu` owns staged edits, binding capture, center/noise sampling and
travel qualification independently of SDL. The SDL host translates fixed
editor navigation, owns the pause handshake and commits settings through its
profile-writer callback. Explicit host resume supports returning to pilot name
entry while retaining the existing P-key behavior. Setup sends one pause
request and waits for acknowledgment before returning a resume request.

`host_ui.cpp` draws authored bitmap text and prompt strips from host state.
Native output decorates a private image copy using its existing palette.
Spatial CPU/GPU adapters compose the strip after world/cockpit rendering.
These views consume completed observations and contribute no simulation time.

## Enhanced presentation views

`host/spatial_history.cpp` samples camera and entity transforms from retained
immutable observations using one clamped display interval. Identity/phase
changes, projectile pose discontinuities and large jumps reset interpolation.
Canonical state and sprite animation remain owned by the simulation.

`host/camera_view.hpp` defines SDK-independent poses, tangent frusta and view
projection. World X/Z follow map X/Y, with world Y pointing up. Camera space uses
X right, Y up and negative Z forward. Native map coordinates use 2²² fixed-point
units per cell. Display heights use the established terrain/entity scale.
Tracked-pose composition requires an explicit world-units-per-metre factor.

| File | Responsibility |
|---|---|
| `host/spatial_renderer.cpp` | World color/depth ownership, sky, palette and view admission |
| `host/spatial_terrain.cpp` | Stitched distance rings and original-density terrain color sampling |
| `host/spatial_models.cpp` | Directional SG frames, rotor layers and perspective billboard placement |
| `host/spatial_effects.cpp` | Per-eye weather, mirrored water views and recovered tint remapping |
| `host/spatial_raster.hpp` | Six-plane triangle clipping, perspective attributes and depth testing |
| `host/cockpit_scaling.cpp` | Coverage-aware nearest/Scale2x overlay composition |

`render_world` accepts a view, entity poses and palette independently of a
cockpit image. It returns color and positive forward depth in world units.
The renderer owns the output until its next call. Each eye may use an independent
renderer or consume its output before rendering another view. The SDL2 host
composes the native overlay for flat-screen presentation. The optional OpenXR
host owns runtime frame timing, tracked spaces and swapchains. XR action mapping
and 3D cockpit interaction remain future work.

## Spatial cockpit ownership

`host/cockpit_model.cpp` constructs authored approximate tandem-cockpit surfaces
in metres. `host/spatial_cockpit.cpp` clips and depth-tests them with the existing
CPU rasterizer. It maps completed native instrument pixels onto the forward
panel and recognized HUD colors onto a distant image plane. `CockpitRenderer`
exports the palette membership of its decoded HUD sprites and gauge colors
through immutable `SpatialResources` metadata. Canonical drawing, storage and
CPU work stay with the existing renderer.

The body compositor owns a separate metre-scaled depth pass over completed
world color. Weather and reflections belong to the world renderer. Optional
`EffectObservation` data records particles before native forward occlusion,
color maps and the completed reflection phase at the original draw boundary.
The renderer extends the forward particle field through four surrounding
sectors and depth-tests each eye independently. Water uses half-resolution
mirrored sky/terrain/model views for every visible water height, with submerged
geometry clipped and recovered water tints applied. Camera interpolation stays
active through observed effects. Rendering consumes no canonical RNG or clock.
Desktop mouse look owns presentation angles outside native command input. The SDL2 host releases its
relative mouse mode on button release, focus loss and context transitions.

The pilot-front/copilot-aft arrangement follows the Army crew-station study.
Mesh dimensions and the static rear crew figure are authored approximations.
[Army Research Laboratory ARL-TR-3404](https://www.academia.edu/26413822/Evaluation_of_an_Army_Aviators_Ability_to_Conduct_Ingress_and_Egress_of_the_RAH_66_Comanche_Crew_Station_While_Wearing_the_Air_Warrior_Ensemble)

## GPU presentation

`SpatialGeometry` accepts material-tagged triangles from the SG model, particle
and cockpit emitters. Software images have an empty geometry
sink and continue through the CPU rasterizer. The OpenGL driver batches the same
geometry and renders fixed native-cell terrain tiles through vertex texture
fetches. Terrain shape is independent of aircraft and eye motion. Frustum tests
select tiles and instanced batches share one immutable grid. The driver owns
indexed asset textures and color/depth targets, and applies palette,
fog, native HUD coverage and effect maps in shaders. It caches immutable resources
and changed instrument/color tables across eyes and frames.

`GlScene` belongs to the SDL2/OpenGL graphics driver. It renders per-eye mirrored
water views at half resolution, clips submerged geometry, then renders native
instruments and the surrounding cockpit in a separate metre-scaled depth pass.
`GraphicsTarget` describes SDK-independent texture and default-framebuffer
destinations. The final shader performs texture-target color-space conversion
or preserves encoded palette colors for the desktop backbuffer.
`GlDesktopPresenter` owns the desktop SDL2 window/context and native-image
presentation. OpenXR owns its context, swapchains and presentation mailbox.
The shared `comanchecd_glscene` library builds independently of OpenXR headers.
Canonical sources and device clocks have no dependency on this driver.

`desktop_view.cpp` resolves drawable size, letterboxing, proportional render
limits and fixed-vertical-field desktop cameras. Widescreen extends horizontal
coverage. Native UI and flat instruments retain centered 4:3 framing.
Enhanced `--renderer auto` attempts GPU construction before CPU fallback.
The window title identifies the selected backend. Faithful input selects the
existing indexed SDL presenter. Preferences store enhanced renderer and aspect
selections alongside controls. GPU rendering consumes completed observations.

`--xr-renderer gpu|cpu` selects the XR backend. `--xr-profile CSV` records pacing,
image waits, CPU submission and end-frame durations through `OpenXrFrameTiming`.
The GPU is asynchronous. Completed-work measurements require an explicit GPU
synchronization or an external profiler.

## OpenXR ownership

`OpenXrHost` decorates the SDL2 device host. The simulation thread owns keyboard,
controller, audio and the indexed desktop mirror. It publishes immutable
`SessionPresentation` instances through `PresentationMailbox`. XR frame waits
and rendering run on a dedicated thread. One retained observation and one
interpolation sample feed both eyes. Pause freezes interpolation while tracked
head poses continue updating. Worker failures propagate to the main host poll.

`OpenXrGraphics` owns a hidden SDL2 OpenGL window/context and native WGL/GLX
bindings. The context is detached before transfer to the XR thread. Session,
reference spaces and swapchains are destroyed there before the thread joins
and the main thread destroys the context. Original assets and SDK binaries are
external dependencies.

`OpenXrSession` owns primary-stereo session transitions, LOCAL/VIEW spaces,
per-eye swapchains and a separate 4:3 panel swapchain. Spatial cockpit flight
composes its geometry into the eye images and submits one projection layer.
The panel handles menus and invalid tracking. Valid poses/FOVs are
located at predicted display time. A drawing failure releases a successfully
waited image and ends an empty frame before propagating. Hidden frames submit
zero layers. The driver uploads top-down CPU images with an OpenGL row flip,
converts sRGB bytes for linear swapchain formats and flushes before release.

OpenXR selection, world scale, render scale and loader path are launch-only.
Host preferences keep their existing SDL2-only storage contract. The optional
backend uses OpenGL and a platform graphics binding isolated in one driver.

## Session modules

All three implementation files define private operations of `GameSession`.
The class retains ownership of its state and transition lifetimes.

| File | Responsibility |
|---|---|
| `src/session.cpp` | Session/audio lifetime, foreground input polling, flight advance/return and observations |
| `src/session_frontend.cpp` | Frontend construction, key routing, request dispatch and settings application |
| `src/session_cinematics.cpp` | Cold startup, attract sequences, recorded-flight entry and timed movie/audio presentation |

Frontend request dispatch applies settings through one named private operation.
Startup and movie transitions retain the original explicit clock operations
alongside their corresponding state changes.

## Flight and entity modules

`FlightController` owns the player, scene, interrupt targets and update
transactions. `src/flight.cpp` retains controller input, views, weapon firing,
entity traversal and the per-entity RNG tail. Its interrupt-target scope spans
entity callbacks and synchronous sound work.

| File | Responsibility |
|---|---|
| `src/flight_entities.hpp` | Private operation contracts, retained sound sink, fixed-width arithmetic and pose helpers |
| `src/flight_entities.cpp` | Bearings, explosions, enemy projectile construction and allied-target scans |
| `src/flight_ground.cpp` | Ground movement, turrets and support vehicles |
| `src/flight_aircraft.cpp` | Enemy/allied aircraft, wingman formation and launch commands |
| `src/flight_projectiles.cpp` | Ballistic/guided/enemy projectiles, contact damage and artillery |

Entity helpers receive references to the controller's candidate state. The
ordered sound sink invokes its callback at the originating sound call before
execution resumes. Shared arithmetic retains the recovered wrap, shift and
sign-mask behavior. Helper bodies retain their original work-accounting calls
and address comments.

## Audio modules

`MissionAudio::Impl` owns the sample banks, music streams, three voices, echo,
DMA halves and execution clock. Its declaration lives in `src/audio_impl.hpp`.
The implementation modules share that owner through private member operations.

| File | Responsibility |
|---|---|
| `src/audio.cpp` | Audio lifetime, loading/presentation transitions, scheduler callbacks, clock/input forwarding and ordered output |
| `src/audio_decode.hpp`, `src/audio_decode.cpp` | K1 sample-bank decoding and checked little-endian resource reads |
| `src/audio_sequencing.cpp` | MS events, programs/envelopes, channel state, OPL writes and music IRQ work |
| `src/audio_mixing.cpp` | Sound routing, current/queued voices, engine/radio cues, echo/conversion and DMA production |
| `src/audio_playback.cpp` | SB16 output resampling/filtering |

Voices retain pointers into the owner's banks or shared silence sample.
Bank replacement stays alongside the original voice clearing and reset work.
The MS cursor publishes its replacement allocation at the recovered clocked
instruction, allowing intervening callbacks to finish with the preceding stream.

IRQ7 arms the current DMA half before toggling the producer to the other half.
Each produced sample applies its queued requests, mixes the voices and echo,
retires its recovered instruction cost, then advances the digital sample count.
Voice reload and changed-sample costs remain attached to their original sample.
The shared owner also retains output queues and callback captures across moves
of the public `MissionAudio` object.

## Resource readers and executable layout

`src/resource_reader.hpp` supplies the shared bounds check and little-endian
integer read for assets, K1 audio, cockpit sprites, SG scene resources and
KDV/demo playback. Format-local operations retain their widths, defaults,
return types and exception messages. Asset dwords retain their two-word
composition. Empty reads at the end of a buffer consume zero bytes.

`src/cd_executable_layout.hpp` names the supported packed executable's size,
identity, unpacked size and protected-mode-relative resource addresses.
Groups cover frontend/pilot text, settings, scene initialization, cockpit,
terrain and audio. Shared settings flag addresses also supply cockpit defaults
and the startup menu overlay. `src/assets.cpp` applies these addresses after
the executable identity check and constructs the domain tables.

Readers operate on host-side resource bytes. Clocked loading and presentation
work lives at the corresponding `Installation`, `MissionAudio`, `loading_work`
and `mission_work` caller boundaries.

## Refactoring sequence

1. Session ownership and lifecycle separation is the first completed batch.
2. Ground, aircraft, projectile and shared combat operations are separated
   into private entity modules. The controller retains traversal, transaction
   ownership and interrupt-sensitive RNG access order.
3. Audio decoding, sequencing and voice mixing are separated into private
   modules. One implementation owner retains allocation lifetimes, producer
   buffers and device callbacks.
4. Compatible little-endian readers share a private implementation. Named
   CD executable constants supply asset construction, with table and
   malformed-input comparisons alongside retained lifecycle checks.

Each batch has its own baseline, bounded scope and verification result.
Preserve recovered address comments and work-accounting calls through moves.
Public interfaces and compatibility policy receive separate design review.

## Verification

Run CTest after each batch, then the affected integrations with player-supplied
data. Substitute the supported installation and CD directories below.

```sh
ctest --test-dir build --output-on-failure
./build/session_tests /path/to/installed/COMANCHE /path/to/cd/COMANCHE
./build/demo_tests /path/to/installed/COMANCHE /path/to/cd/COMANCHE
./build/startup_input_tests /path/to/installed/COMANCHE /path/to/cd/COMANCHE
./build/spatial_capture_tests /path/to/installed/COMANCHE /path/to/cd/COMANCHE
```

Use `.exe` on Windows. Session checks cover settings, abort/crash return,
natural victory, saves and restart. Demo checks exercise all four attract
flights and their movie/audio transitions. Startup checks exercise delivered
input and audio chunking. Spatial checks compare capture-enabled and indexed
execution through a complete training victory.

For a behavior-preserving refactor, compare fixed-input baseline and candidate
state/clock/RNG checkpoints, indexed images, palettes, PCM and saved bytes.
Retained DOSBox-X evidence continues to define its original qualification
scope. Physical input/output and OpenXR checks have their own qualification.

### Presentation audio gain

`AudioOutputTransform` receives FM and digital fixed-point accumulators plus
the canonical clipped stereo sample at the final device-output boundary.
The empty callback follows the canonical output path. The enhanced host's
`AudioVolume` callback reads one packed atomic set of gains per frame and
returns canonical PCM directly at unity. Source gains apply before clipping
and master gain follows clipping. This callback has no SDL or device-timing
operations. The producer retains FM sequencing, DMA ownership and IRQ work.

`run_session_app` constructs the host before the cold session so saved output
levels cover startup audio. The callback retains shared gain state across the
simulation producer boundary. The SDL main thread owns the paused audio page,
preference validation and persistence. Apply publishes gains after a successful
save. Audio and controller pages share input-release and pause acknowledgment.
The enhanced library owns volume preferences and UI. The faithful host supplies
an empty output callback.
