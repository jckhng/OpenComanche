# Changelog

## Enhanced SDL2 development

- Add GPU desktop rendering through SDL2/OpenGL with automatic CPU fallback,
  persistent renderer selection and SDK-independent graphics targets.
- Add widescreen flight with fixed vertical framing, proportional render
  limits and centered native UI/flat instruments. Preserve faithful defaults.
- Keep desktop builds independent of OpenXR headers. Defer further VR work
  pending physical testing.

- Add an SDL2/OpenGL XR scene renderer for sky, terrain, SG models, weather,
  water reflections and cockpit geometry. Share the existing geometry emitters
  and cache indexed resources, palette maps and native instrument textures.
  Use fixed world-aligned terrain tiles to prevent moving LOD height warping.
- Add launch-only XR GPU/CPU selection and frame-timing CSV output.

- Render weather and water reflections per eye from read-only native draw
  observations. Extend weather around the aircraft and reflect terrain/models
  at visible water heights using recovered tint tables and reflection phase.
  Keep interpolation active through effects in cockpit and external views.

- Add an authored look-around cockpit with canopy, consoles, seats and a static
  rear crew figure. Reuse live DOS instrument pixels and exact HUD colors.
- Add desktop mouse look/recenter and a persistent flat/spatial cockpit choice.
  Compose the spatial cockpit in XR eye images.

- Reduce CPU raster work through clipping fast paths, incremental barycentric
  weights and reused terrain vertex rows.
- Add an optional threaded SDL2/OpenGL OpenXR host with stereo session/frame
  submission, tracked views, cockpit/menu layers and focus recovery.

- Interpolate entity positions/headings/rotors with the camera and reset
  discontinuous observations. Add persistent sharp cockpit/HUD scaling.
- Add SDK-independent full-pose/asymmetric camera views, world-only output
  and a clipped CPU terrain/billboard renderer for future per-eye presentation.

- Configurable context-specific controller button/trigger actions and steering
  axes, with persistent enhanced host preferences and command-line overrides.

- Share compatible resource integer readers and name CD executable layouts,
  preserving format errors, decoded tables and clocked loading boundaries.

- Separate K1 decoding, MS/OPL sequencing and voice/DMA mixing into private
  audio modules, retaining shared storage and recovered callback timing.

- Group ground, aircraft, projectile and shared combat implementations into
  private source modules, preserving controller dispatch and instruction order.

- Separate session core, frontend/settings and cinematic lifecycle operations,
  with private ownership and baseline-equivalent behavior.

- Configurable controller deadzone, sensitivity and pitch inversion, with
  keyboard precedence and neutral rearming on context/focus changes.
- Buffered keyboard fire/target taps with pause/context cancellation.
- Separate image presenter with high-DPI output, integer/nearest/driver-filtered
  scaling, small-window fallback and enhanced Alt+Enter fullscreen.
- Optional OpenXR admission retains the immutable presentation boundary.
- Display-sized terrain rendering with bilinear heights, original panoramic sky,
  distance haze and depth-tested directional/animated SG billboards.
- Immutable opt-in world snapshots, exact native cockpit/HUD coverage and
  presentation-only camera interpolation with toroidal wrap and pause handling.
- Native presentation fallback for scripted effects, reflected-water scenes
  and crashes. Optional `--scene-renderer native` and `--interpolation off`.
- Shared canonical simulation, recovered CPU/device work, indexed rasterization
  and audio generation across both host modes.

## 0.1.0-preview1-rc10 — 2026-11-13

Initial limited public preview for Windows x64 and Linux x86-64 using SDL2.

- Native startup, pilot/name entry, operation/mission selection, briefing,
  flight, objectives, debrief and profile persistence.
- Recovered indexed scene/cockpit rendering, keyboard controls and native
  music/effects playback.
- Live-audio correction drains complete generated flight blocks and budgets
  their sample duration. Presentation queue resets discard unplayed transition
  PCM, including buffered loading music.
- Windows physical checks pass for menu/name input, flight controls, audio,
  pause/focus recovery, resize, Oil Tank Holiday victory/debrief/save and restart.
  The saved pilot gains one victory and retains identical record bytes across
  restart.
- Both runtime archives rebuild from included source. Windows passes 45 native
  suites. Linux passes 44 with the existing controller skip. Automated training,
  debrief/save, separate-process restart and abort pass on both platforms.
- Archive hashes, matching source, dependency notices, original-file exclusion,
  private-path scans and archive-path checks pass.

Start with Begin a Mission, Comanche Training, Oil Tank Holiday. Other selectable
missions remain experimental. Five additional completed retained routes have
declared DOSBox-X state/RNG/debrief/save/mixed-audio comparisons. All 100 missions
have bounded native checks.

Continuous cold-entry training audio, broader original-command/progression
coverage, independent clean-machine and physical Linux/controller checks remain
open. OpenXR gameplay remains planned. PREVIEW.md records the detailed scope.

| Runtime ZIP | SHA-256 |
|---|---|
| comanche-preview1-rc10-windows.zip | `7d91b5b68cc54fc40919e0068a4a40612d924e9435d5cc4e0e7ef2fd5fb9a9d2` |
| comanche-preview1-rc10-linux.zip | `58c6268be1b70364bf1162fa774a486bc1cdc714c8b7c4fa37917e585bc67a9d` |
