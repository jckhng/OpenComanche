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

## Refactoring sequence

1. Session ownership and lifecycle separation is the first completed batch.
2. Ground, aircraft, projectile and shared combat operations are separated
   into private entity modules. The controller retains traversal, transaction
   ownership and interrupt-sensitive RNG access order.
3. Audio decoding, sequencing and voice mixing are separated into private
   modules. One implementation owner retains allocation lifetimes, producer
   buffers and device callbacks.
4. Consolidate repeated format readers and recovered constants where their
   byte order, bounds and failure behavior agree. Executable-specific layouts
   currently reside in `src/assets.cpp`.

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
