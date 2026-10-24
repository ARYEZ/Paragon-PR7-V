# Paragon PR7-V

[![Build](https://github.com/ARYEZ/Paragon-PR7-V/actions/workflows/build.yml/badge.svg)](https://github.com/ARYEZ/Paragon-PR7-V/actions/workflows/build.yml)
[![Latest release](https://img.shields.io/github/v/release/ARYEZ/Paragon-PR7-V)](https://github.com/ARYEZ/Paragon-PR7-V/releases/latest)

A vocal multi-effects VST3 plugin built around [Neural Amp Modeler](https://github.com/sdatkinson/NeuralAmpModelerCore)
(`.nam`) amp captures, with a cabinet IR loader and built-in effects. Built with [JUCE](https://juce.com).

Paragon PR7-V is the vocal version of [Paragon PR7-G](https://github.com/ARYEZ/Paragon-X1), the guitar processor, and
starts from its 0.32.1 release. It installs alongside PR7-G and keeps its own rigs. Voicing it for vocals is in progress,
so the guide below still describes the guitar version's effects.

![The Paragon design mockup (from when the guitar version was called Paragon X1)](Paragon%20x1.png)

*The design mockup the guitar version was built from.*

## Roadmap

| Phase | Scope | Status |
|---|---|---|
| 1 | Project skeleton, CI builds for Windows/macOS/Linux, input/output gain, level meters | ✅ Done |
| 2 | Load `.nam` amp models and cabinet IRs | ✅ Done |
| 3 | Effect blocks: comp/gate, overdrive, modulation, delay, reverb, with footswitches | ✅ Done |
| 4 | Rigs, presets, banks, tags | ✅ Done |
| 5 | Full pedalboard UI from the mockup | ✅ Done |
| 6 | Expression pedal/morph, tap tempo, undo/redo, tuner | ✅ Done |
| 7 | Detail and polish pass against the mockup | ✅ Done |

## Using it

- **Amp:** click the amp display (or drag a `.nam` file onto it) to load a model. The ◀ ▶ arrows step
  through the other models in the same folder. The LED turns the amp on and off.
- **NORMALISE** levels every model to the same loudness, like the official NAM plugin's "Normalized"
  output. It needs loudness data in the model; older models without it play at their raw level.
- **Cabinet:** load a `.wav`, `.aiff` or `.flac` impulse response the same way. Like the official
  plugin, IRs are turned down by a fixed 18 dB and not trimmed or normalised.
- Models and IRs load in the background and crossfade in, so switching while playing doesn't click.
- **Sample rate:** NAM models are captured at a fixed rate (almost always 48 kHz). At other rates the
  plugin resamples with the same resampler as the official plugin and reports the small added
  latency to your DAW.
- **PRESENCE** (under the cabinet) cuts or boosts the top end after the cab, like an amp's presence knob.
- **Footswitches 1-4** turn the effect blocks on and off. Click a footswitch's name to show its knobs
  below. Signal chain: comp/gate → filter / pitch → overdrive → amp → cabinet → presence → graphic EQ → modulation
  → delay → reverb → lo-fi.
  - **1 Comp / Gate:** a compressor and a noise gate (threshold; all the way down is off). The compressor has two TYPEs:
    **Classic** (threshold, ratio, level) and **Daybreak**, a modern pedal compressor: COMP (how much, with automatic make-up),
    DETAIL (how much pick attack gets through), MIX (parallel compression), INTENSITY (smooth or hard) and LEVEL. The panel shows a
    gain-reduction meter.
  - **Lo-Fi** (last in the chain; no footswitch): telephone, AM radio, megaphone, cassette (wow, flutter, hiss)
    or bitcrusher, for intros and breakdowns. AMOUNT, NOISE, MIX and LEVEL.
  - **Pitch** (first in the signal chain; no footswitch): **TUNING** retunes the whole guitar (-12 to +12 semitones: D
    standard, B, a capo...), the **OCTAVER** blends two octaves down, one down, the dry guitar and one up (POG-style,
    chords track too), and **FREEZE** holds what you just played as an endless pad you can play over (with LEVEL,
    ATTACK and RELEASE).
  - **Glitch** (in the signal chain; no footswitch): granular glitch effects in five MODEs (the fifth, **Killswitch**,
    cuts the end of each step to silence, like flicking a guitar's toggle switch in time): **Pitch Delay** (echoes that each
    step in PITCH), **Sequence** (tempo-locked stutters that repeat the slice just played), **Interfere** (dropouts, backwards
    slivers and buzzing loops) and **Texturizer** (a cloud of grains). LENGTH (in note values with SYNC), DENSITY, PITCH, CHAOS
    (randomises everything together: low is rhythmic, high is unpredictable) and MIX. With SYNC on and the DAW playing,
    Sequence and Interfere start their steps on the song's grid (every 1/16, every bar...), locked to the drums. POSITION puts it after the amp (POST,
    the default: exact chops) or before it (PRE: the chops hit the drive). The expression pedal's GLITCH setting fades it in.
  - **Filter / Pitch** (in the signal chain; no footswitch): an auto-wah (envelope filter), a pitch shifter
    (two octaves down to two up, blended with the dry guitar for harmonies and octaves), or a **Whammy-Wah**: the
    DigiTech XP-100's factory programs 01–28, numbered as on the pedal and played with the expression pedal (WHAMMY-WAH):
    a volume pedal, five wahs, six auto-wahs (the pedal sets sensitivity or attack), eight whammy bends (heel: no shift,
    toe: the interval, as far as six octaves down) and eight harmonies (your note plus a voice the pedal moves between two intervals);
    or a **Talk Box**: the amp's sound, after the cab, shaped into vowels (oo to ee) by the expression pedal (TALK BOX),
    with RESONANCE and MIX.
  - **2 OD / Distortion:** four models: **Screamer**, modelled on the Ibanez TS808 Tube Screamer's circuit;
    **Classic**, Paragon's original overdrive; **Muff Fuzz**, following the Big Muff's two clipping stages and
    mid-scooping tone stack (long, violin-like sustain); and **Face Fuzz**, following the silicon Fuzz Face's uneven
    clipping (raspy and round, cleaning up with the guitar's volume). Drive, tone, level; 8x oversampled to avoid harsh aliasing.
  - **3 Modulation:** eleven types: chorus, flanger, phaser, tremolo, Dimension chorus, Uni-Vibe, rotary
    (Leslie), vibrato, harmonic tremolo, auto-pan and doubler. SYNC locks the rate to the tapped or DAW tempo.
  - **4 Reverb / Delay:** switches both; each also has its own ON switch. The delay syncs to your
    DAW's tempo (or set SYNC to Off for milliseconds), has a tone control for darker repeats, and
    optional ping-pong; it comes in eight types: Digital (the original), Analog, Tape, Mod, Reverse,
    Multi-Tap, Shimmer and Ducking. The reverb comes in eight types: Room, Hall, Cathedral, Plate, Spring,
    Digital, Reverse and Classic (the original), with SIZE, PRE-DELAY, TONE and MIX. Both have a DUCK knob that tucks
    the repeats or reverb under your playing and lets them swell back in the gaps. Both let their
    tails ring out when switched off.
- **Graphic EQ:** 9 bands (63 Hz to 16 kHz, ±12 dB) and a level, after the cabinet. Open it from the
  **EQ** block in the signal chain; the curve shows the EQ's actual response. FLAT resets it. **PRESETS** has twelve
  common starting curves (Metal Scoop, Tight Low End, Mud Cut, Fizz Tamer, Lead Mid Boost, Cut Through the Mix,
  Fat Rhythm, British Crunch, Bright Clean, Funk Scoop, Warm Jazz, Radio), each loudness-matched with LEVEL.
- All effects start switched off, so projects made with earlier versions sound the same.
- **Rigs:** SAVE in the header saves the current sound as a rig; **<** and **>** step through rigs; a
  `*` after the name means it's changed since loading. The **PRESETS** tab browses, searches, files
  (banks A-D), tags and favourites rigs. Factory rigs set the effects and keep your amp and cab.
  INPUT and OUTPUT stay put when you switch rigs; each rig has its own **RIG LEVEL** (on the unit), which adds to OUTPUT.
- **TAP** (header or pedal) sets the delay's tempo from your taps; the BPM turns orange. Click the
  BPM to follow your DAW's tempo again. The TAP light flashes in time.
- **Tuner:** hold TAP, or press TUNER on the pedal. The screen shows the note, how many cents off it
  is (the centre bar turns green when in tune) and the frequency. MUTE silences the output while tuning.
- **Undo / redo:** the arrows in the header, UNDO / REDO on the pedal, or Ctrl+Z / Ctrl+Shift+Z
  (Cmd on a Mac). Each knob move, switch, file or rig load is one step. INPUT, OUTPUT and the
  pedal's position aren't undone.
- **Expression pedal** (right of the unit): drag it, use the mouse wheel, or send MIDI CC 11 (or the CC chosen in SETTINGS) from a
  real expression pedal (route your MIDI track to the plugin). Assign it on the small EXPRESSION screen beside it (click a line, or scroll; it also shows the
  pedal's position) or with EXPRESSION ASSIGN in the dock, to
  **VOLUME**, **WAH / FILTER** (a wah before the drive), **MORPH / FX** (fades modulation, delay and
  reverb in from the heel), **PITCH / WHAMMY** (bends the pitch shifter from no shift at the heel to its
  INTERVAL at the toe), **LO-FI** (fades the lo-fi effect in: full band at the heel, lo-fi at the toe) or
  **WHAMMY-WAH** (plays the FILTER / PITCH block's Whammy-Wah). MIN and MAX set where the heel and toe land. The assignment and range
  are saved with each rig.
- **Bottom panels:** RIG BROWSER jumps to the PRESETS page filtered; SELECTED RIG files and saves the
  current rig; the middle shows the graphic EQ, or the knobs of whichever block you pick (click a
  signal-chain block or a footswitch name); **GRAPHIC EQ** goes back to the EQ.
- **SETTINGS:** the tuner's reference pitch, mute while tuning, and the expression pedal's MIDI CC.
- The paths of the loaded files are saved in your DAW project. If a file is missing when the project
  opens, the slot says so and keeps the path, so moving the file back fixes it.

## Download

Get the latest version from the **[Releases](https://github.com/ARYEZ/Paragon-PR7-V/releases/latest)** page: download the zip for your
computer. (Builds of every push also appear under **Actions** → a run → **Artifacts**.)

Unzip it and copy `Paragon PR7-V.vst3` to your VST3 folder:

| OS | VST3 folder |
|---|---|
| Windows | `C:\Program Files\Common Files\VST3` |
| macOS | `~/Library/Audio/Plug-Ins/VST3` |
| Linux | `~/.vst3` |

**macOS:** the build isn't code-signed yet, so macOS will block it. After copying, run
`xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"Paragon PR7-V.vst3"` once.

## Making a release

1. Set the new version in `CMakeLists.txt` (`project(ParagonX1 VERSION x.y.z)`).
2. Add a `## x.y.z` section to `CHANGELOG.md`.
3. Commit and push.

When a push carries a version that hasn't been released yet, GitHub Actions builds all three
platforms, tags the commit `vx.y.z` and publishes the release with notes from the changelog.

## Building from source

Requirements: CMake 3.24+, a C++20 compiler (Visual Studio 2022, Xcode 15+, or GCC 11+/Clang 14+), and Git.
JUCE, NeuralAmpModelerCore, Eigen and Catch2 are downloaded automatically at pinned versions.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The plugin is written to `build/ParagonX1_artefacts/Release/VST3/`.

On Linux, install JUCE's dependencies first (the list is in `.github/workflows/build.yml`).

To render the editor to an image without a DAW:

```bash
PARAGON_SNAPSHOT=editor.png ./build/tests/ParagonX1Tests "[snapshot]"
```

## License

The Inter typeface (in `resources/fonts`) is by Rasmus Andersson, under the SIL Open Font License.


Paragon PR7-V is free software under the [GNU Affero General Public License v3](LICENSE.md),
which is required by JUCE's open-source licence.

It uses:
- [JUCE](https://github.com/juce-framework/JUCE): AGPLv3
- [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore): MIT
- [Eigen](https://eigen.tuxfamily.org): MPL 2.0
- [AudioDSPTools](https://github.com/sdatkinson/AudioDSPTools) (resampler): MIT, with parts from iPlug2 under the zlib-style iPlug2 licence

"Neural Amp Modeler" is Steven Atkinson's project. Paragon PR7-V is an independent player for `.nam`
files and isn't affiliated with or endorsed by it.
