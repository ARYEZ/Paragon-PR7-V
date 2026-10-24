# Paragon PR7-V: notes for Claude

This repository starts as a copy of **Paragon PR7-G** (the guitar processor, repo `ARYEZ/Paragon-X1`), full history included, at PR7-G **0.32.1**. It is to become **Paragon PR7-V**, the **vocal** processor. A third sibling, **Paragon PR7-B** (bass, repo `ARYEZ/Paragon-PR7-B`), started the same way.

Everything below describes how the project is built, tested and released, and how the owner likes to work.

## First: make it its own plugin (do this before anything else)

**Done in 0.1.0** (plugin code `Pr7v`). Kept as a record of what makes PR7-V separate from PR7-G.

Until these are done, PR7-V *is* PR7-G as far as a DAW is concerned. The two would replace each other and share rigs and settings.

1. **CMakeLists.txt** (`juce_add_plugin`):
   - `PRODUCT_NAME` becomes "Paragon PR7-V".
   - `BUNDLE_ID` becomes `com.paragon.paragonpr7v`.
   - `PLUGIN_CODE` becomes a new code: four characters with exactly one upper case, e.g. `Pr7v`. Never PR7-G's `Px1a` or PR7-B's code.
   - Keep `PLUGIN_MANUFACTURER_CODE Prgn` (same maker).
   - Restart the version at `0.1.0` (`project(ParagonX1 VERSION ...)`; the CMake target name can stay).
2. **Rigs folder** (`source/rigs/RigLibrary.cpp`): "Paragon PR7-V".
   - Remove the carry-over from the old guitar folders. Vocal users shouldn't inherit guitar rigs.
   - Update the rename test in `tests/RigTests.cpp` to match.
3. **Settings file** (`source/ui/Finish.cpp`): `applicationName` becomes "Paragon PR7-V", so PR7-V remembers its own finish (Pearl White, White + Neon or Pearl Black).
4. **Logo** (`source/ui/Logo.cpp`): the mark's "-G" becomes "-V", drawn in the same slanted, speed-cut lettering.
5. **Text:** the About text and MIDI hint in `source/ui/DockPanels.cpp`, the loader thread name in `PluginProcessor.h`, and `README.md` all say PR7-V.
6. **Release workflow** (`.github/workflows/build.yml`, `.github/release-install.md`):
   - Bundle paths become `Paragon PR7-V.vst3`.
   - Zips become `ParagonPR7-V-${TAG}-...`.
   - The release title becomes "Paragon PR7-V ${TAG}".
7. **CHANGELOG.md:** start fresh at 0.1.0 ("The vocal processor, based on Paragon PR7-G 0.32.1").
8. **Re-enable releases.** The `release` job's `if:` was set to `false && ...` in the seed commit so this repo didn't publish a guitar release. Put it back to `github.event_name == 'push'` in the same commit as the version bump.

## The project

- An AGPLv3 **JUCE VST3** plugin: a NAM (Neural Amp Modeler) player with a full multi-effects chain, drawn as a floor processor with an expression pedal.
  - Three finishes in SETTINGS → FINISH: Pearl White, Pearl White + Neon, Pearl Black (see `source/ui/Finish.h`).
- **The chain:** INPUT → LOW CUT → PITCH → (WAH pedal) → COMP/GATE → FILTER/PITCH → GLITCH (Pre) → OD/DIST → AMP (NAM) → CAB (IR) → PRESENCE → TALK BOX → GRAPHIC EQ → GLITCH (Post) → MOD → DELAY → REVERB → LO-FI.
- **Code layout:** DSP in `source/dsp/effects/`, UI in `source/ui/`, parameters in `source/Parameters.*`, rigs in `source/rigs/`, tests in `tests/`.
- **Parameters:**
  - Saved by ID and choices by **index**: only ever *append* choices, never reorder.
  - New parameters get a new version hint number.
  - Migrations for changed defaults live in `setStateInformation` and `Rig::fromXml`.
- **Shared DSP pieces:** `BypassFade` and `TailWatch` (EffectUtils.h), `PitchShifter` (correlation-spliced, used by Whammy-Wah, Shimmer, PITCH and Glitch).

## Build, test, check

- **Build:** `cmake -B build && cmake --build build -j8`.
- **Tests (Catch2):** `xvfb-run -a build/tests/ParagonX1Tests`. All must pass. New features get tests that check what the feature really does to the sound (levels, frequencies, harmonics, pitch), not just that it runs.
- **pluginval:** `xvfb-run -a <pluginval> --strictness-level 10 --validate-in-process "build/ParagonX1_artefacts/Release/VST3/<name>.vst3"`. Check its **exit code**, not just "SUCCESS". CI runs it too.
- **Screenshots** for checking the UI, via `xvfb-run -a build/tests/ParagonX1Tests "[snapshot]"` with environment variables:
  - `PARAGON_SNAPSHOT` (output path), `PARAGON_SNAPSHOT_WIDTH` (e.g. 2560 for 2x).
  - `PARAGON_SNAPSHOT_BLOCK=n` (open block n's panel), `PARAGON_SNAPSHOT_PARAMS=id=value,...`.
  - `PARAGON_SNAPSHOT_EXPRESSION`, `PARAGON_SNAPSHOT_PAGE=presets|settings`.
  - `PARAGON_SNAPSHOT_FINISH=black|neon`.
  - Render, look, crop and zoom before claiming a visual change is right.

## Releases

- Bump `project(ParagonX1 VERSION x.y.z)` in CMakeLists.txt and add a matching `## x.y.z` section at the top of CHANGELOG.md, then push.
- CI builds Windows, macOS and Linux and publishes a GitHub Release with the three zips, using that changelog section as the notes.
- After pushing, check the release published (all three zips) about 15 minutes later.
- The owner installs from GitHub Releases on **Windows** and works in **BeatMaker**.

## How the owner likes to work

- **Explain the why**, specifically: what was wrong, what changed, and the trade-offs. Plain language; the owner is a musician, not a DSP engineer.
- **Be honest about limits:**
  - Say when something is our design rather than a copy ("modelled on", "inspired by").
  - Say when a detail is a guess.
  - Say what can't be checked here (audio by ear).
  - Ask for source material, such as manuals, program charts or gear rundowns, rather than invent specifics.
- **No trademarked names or branding** on our models: "Screamer (TS)", "Muff Fuzz", "Face Fuzz", "Daybreak" (compressor), "GLITCH". "Inspired by" in descriptions is fine. For vocals that includes pitch-correction and harmony products.
- **Show it:** send a rendered screenshot of any visual change.
- **"merge it"** means: open a PR from the working branch to main and merge it, then reset the working branch to the new main.
- Commit messages explain what and why.

## Ideas for the vocal version (to plan with the owner)

- **The input:** a microphone, not a guitar.
  - Mic-level input gain and a **high-pass filter** first in the chain (rumble and plosives).
  - The guitar tuner becomes a pitch display, or goes.
- **Vocal staples:**
  - **Pitch correction:** key and scale, speed from natural to hard "robot" correction.
  - **Harmony:** one or two voices a third, fifth or octave away, following the key.
  - **De-esser.**
  - **Doubler:** short delays with slight pitch and timing drift for a double-tracked sound.
  - **Formant shift:** "chipmunk" to "giant" without changing the pitch.
  - **Vocoder or robot** voice.
  - **Megaphone / telephone / radio** lo-fi voices (LO-FI is a start).
- **Already useful as they are:**
  - The Daybreak compressor and the gate.
  - The graphic EQ (bands may move up for voice).
  - Delay and reverb: slap, plate, hall and throws.
  - Mod: chorus and flanger.
- **The NAM amp and cab:** become optional "colour", or load **mic preamp** captures instead of guitar amps.
- **Pitch tools to adapt:** PITCH's octaver and FREEZE, and the expression pedal (ideal for harmony level, delay throws and pitch bends live).
- **Factory rigs** for vocals: clean pop, rock grit, rap/trap, worship, lo-fi radio, robot, and song-inspired voices.
