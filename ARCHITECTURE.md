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

## Refactoring sequence

1. Session ownership and lifecycle separation is the first completed batch.
2. Group the ground, aircraft and projectile update paths in `src/flight.cpp`
   around their existing contracts and shared state. Preserve traversal and
   interrupt-sensitive RNG access order in each extraction.
3. Separate resource decoding, music sequencing and voice mixing in
   `src/audio.cpp`. Trace producer-buffer ownership and device callbacks before
   changing their representation.
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
