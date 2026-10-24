# Paragon PR7-V

[![Build](https://github.com/ARYEZ/Paragon-PR7-V/actions/workflows/build.yml/badge.svg)](https://github.com/ARYEZ/Paragon-PR7-V/actions/workflows/build.yml)
[![Latest release](https://img.shields.io/github/v/release/ARYEZ/Paragon-PR7-V)](https://github.com/ARYEZ/Paragon-PR7-V/releases/latest)

A vocal multi-effects VST3 plugin: a floor processor for your voice, with seven stomps, an expression pedal
and rigs. Built with [JUCE](https://juce.com).

Paragon PR7-V is the vocal version of [Paragon PR7-G](https://github.com/ARYEZ/Paragon-X1), the guitar processor, and
started from its 0.32.1 release. It installs alongside PR7-G and keeps its own rigs. It has no amp or cabinet (PR7-G's
NAM amp and IR loader aren't needed for a voice). Voicing the effects for vocals is in progress, so some of the
descriptions below still talk about guitar.

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
| V1 | PR7-V: its own plugin; LOW CUT for the mic; seven stomps, no amp or cabinet | ✅ Done |
| V2 | Master limiter by OUTPUT | ✅ Done |
| V3 | De-esser in DYNAMICS, harmony in PITCH | Next |

## Using it

- **INPUT** sets the level from your mic (the meter shows what comes in; aim for peaks around -12 to -6).
  **LOW CUT** under it is a high-pass filter, first in the chain (see below).
- **The seven stomps** turn the effect blocks on and off: **1 Dynamics, 2 Saturation, 3 Doubler** on the top row,
  **4 Pitch, 5 Modulation, 6 Delay, 7 Reverb** on the bottom. Click a stomp's name to show its knobs below. Signal
  chain: low cut → pitch → dynamics → filter / pitch → saturation → presence → graphic EQ → doubler → modulation →
  delay → reverb → lo-fi → output → limiter. (PITCH works first in the sound, where it tracks the clean voice best, though its stomp is 4.)
  - **1 Dynamics:** a compressor and a noise gate (threshold; all the way down is off). The compressor has two TYPEs:
    **Classic** (threshold, ratio, level) and **Daybreak**, a modern pedal compressor: COMP (how much, with automatic make-up),
    DETAIL (how much of the attack gets through), MIX (parallel compression), INTENSITY (smooth or hard) and LEVEL. The panel shows a
    gain-reduction meter.
  - **2 Saturation:** four models: **Screamer**, modelled on the Ibanez TS808 Tube Screamer's circuit;
    **Classic**, Paragon's original overdrive; **Muff Fuzz**, following the Big Muff's two clipping stages and
    mid-scooping tone stack; and **Face Fuzz**, following the silicon Fuzz Face's uneven clipping. Drive, tone,
    level; 8x oversampled to avoid harsh aliasing.
  - **3 Doubler:** automatic double tracking. It sounds as if you sang the part three times: you in the middle and a
    "take" each side, each a little late and drifting slowly in timing and pitch. RATE is how quickly the takes drift,
    DEPTH how loose they are, MIX how loud. (Until 0.3.0 this was one of MODULATION's types; old rigs and projects
    move to this stomp by themselves.)
  - **4 Pitch:** the stomp switches the **OCTAVER**, which blends two octaves down, one down, the dry voice and one up.
    In its panel, **TUNING** shifts everything -12 to +12 semitones, and **FREEZE** (its own switch) holds what you just
    sang as an endless pad you can sing over (with LEVEL, ATTACK and RELEASE).
  - **5 Modulation:** ten types: chorus, flanger, phaser, tremolo, Dimension chorus, Uni-Vibe, rotary (Leslie),
    vibrato, harmonic tremolo and auto-pan. SYNC locks the rate to the tapped or DAW tempo.
  - **6 Delay:** syncs to your DAW's tempo (or set SYNC to Off for milliseconds), has a tone control for darker repeats,
    and optional ping-pong; eight types: Digital (the original), Analog, Tape, Mod, Reverse, Multi-Tap, Shimmer and
    Ducking.
  - **7 Reverb:** eight types: Room, Hall, Cathedral, Plate, Spring, Digital, Reverse and Classic (the original), with
    SIZE, PRE-DELAY, TONE and MIX. Delay and reverb both have a DUCK knob that tucks the repeats or reverb under your
    singing and lets them swell back in the gaps, and both let their tails ring out when switched off.
- **More blocks in the signal chain** (no stomp: click them in the strip at the bottom):
  - **Filter / Pitch:** an auto-wah (envelope filter), a pitch shifter (two octaves down to two up, blended with the
    dry voice), a **Whammy-Wah** (the DigiTech XP-100's factory programs 01–28, played with the expression pedal), or a
    **Talk Box**: the sound shaped into vowels (oo to ee) by the expression pedal, with RESONANCE and MIX.
  - **Glitch:** granular glitch effects in five MODEs: **Pitch Delay**, **Sequence** (tempo-locked stutters),
    **Interfere**, **Texturizer** and **Killswitch**. LENGTH (in note values with SYNC), DENSITY, PITCH, CHAOS and MIX.
    POSITION puts it after the EQ (POST, the default) or before the saturation (PRE).
  - **Lo-Fi** (last in the chain): telephone, AM radio, megaphone, cassette (wow, flutter, hiss) or bitcrusher, for
    intros and breakdowns. AMOUNT, NOISE, MIX and LEVEL.
- **OUTPUT** is the overall volume. **LIMITER** under it is the master limiter, last in the chain: nothing leaves the
  plugin above its ceiling (-1 dB to start; -12 to 0 dB; all the way up is OFF), so a loud note can't clip in your DAW.
  Its light glows while it's turning peaks down. It adds no delay, and like OUTPUT it stays put when you switch rigs.
- **PRESENCE** (on the unit) cuts or boosts the top end, above about 3.5 kHz: more air on the voice, or less harshness.
- **Graphic EQ:** 9 bands (63 Hz to 16 kHz, ±12 dB) and a level, after the saturation. Open it from the
  **EQ** block in the signal chain; the curve shows the EQ's actual response. FLAT resets it. **PRESETS** has twelve
  common starting curves (Metal Scoop, Tight Low End, Mud Cut, Fizz Tamer, Lead Mid Boost, Cut Through the Mix,
  Fat Rhythm, British Crunch, Bright Clean, Funk Scoop, Warm Jazz, Radio), each loudness-matched with LEVEL.
- All effects start switched off, so projects made with earlier versions sound the same.
- **Rigs:** SAVE in the header saves the current sound as a rig; **<** and **>** step through rigs; a
  `*` after the name means it's changed since loading. The **PRESETS** tab browses, searches, files
  (banks A-D), tags and favourites rigs. (The factory rigs are still PR7-G's guitar rigs for now; vocal ones are coming.)
  INPUT, LOW CUT, OUTPUT and LIMITER stay put when you switch rigs; each rig has its own **RIG LEVEL** (on the unit), which adds to OUTPUT.
- **LOW CUT** (under INPUT): a high-pass filter for the microphone, first in the chain. It takes out
  rumble, hum and the thump of plosives ("p" and "b") below the frequency you set, 12 dB per octave.
  80 Hz to start; up to 120-150 Hz for a thin or close-miked voice; turn it all the way down for OFF.
- **TAP** (header or pedal) sets the delay's tempo from your taps; the BPM turns orange. Click the
  BPM to follow your DAW's tempo again. The TAP light flashes in time.
- **Tuner:** hold TAP, or press TUNER on the pedal. The screen shows the note, how many cents off it
  is (the centre bar turns green when in tune) and the frequency. MUTE silences the output while tuning.
- **Undo / redo:** the arrows in the header, UNDO / REDO on the pedal, or Ctrl+Z / Ctrl+Shift+Z
  (Cmd on a Mac). Each knob move, switch or rig load is one step. INPUT, LOW CUT, OUTPUT, LIMITER and the
  pedal's position aren't undone.
- **Expression pedal** (right of the unit): drag it, use the mouse wheel, or send MIDI CC 11 (or the CC chosen in SETTINGS) from a
  real expression pedal (route your MIDI track to the plugin). Assign it on the small EXPRESSION screen beside it (click a line, or scroll; it also shows the
  pedal's position) or with EXPRESSION ASSIGN in the dock, to
  **VOLUME**, **WAH / FILTER** (a wah before the saturation), **MORPH / FX** (fades the doubler, modulation, delay
  and reverb in from the heel), **PITCH / WHAMMY** (bends the pitch shifter from no shift at the heel to its
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
JUCE and Catch2 are downloaded automatically at pinned versions.

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

It uses [JUCE](https://github.com/juce-framework/JUCE) (AGPLv3).

(Paragon PR7-G, which PR7-V started from, also uses NeuralAmpModelerCore, Eigen and AudioDSPTools for its NAM amp.
PR7-V has no amp, so it doesn't include them.)
