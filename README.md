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
| V3 | De-esser with LISTEN (DYNAMICS stomp) | ✅ Done |
| V4 | Harmony stage 1, inspired by Waves Harmony: key/scale, 8 voices, graph | ✅ Done |
| V5 | Harmony stage 2: CORRECT (pitch correction of your voice: Natural, Hard) | ✅ Done (0.17.0) |
| V6 | Harmony stage 3: Generate Notes + chord maps (and TUNE VOICE's Notes mode) | ✅ Done (0.19.0) |
| V7 | Harmony stage 4: MIDI note input (As Played / Through Chord, velocity, sustain) | ✅ Done (0.20.0) |
| V8 | Harmony stage 5: Note Map Editor (CHORD Custom, SCALE Custom, Spread/Fold) | ✅ Done (0.21.0) |
| V9 | Harmony: per-voice delay/filter, snapshots, modulators | Next |

## Using it

- **INPUT** sets the level from your mic (the meter shows what comes in; aim for peaks around -12 to -6).
  **LOW CUT** under it is a high-pass filter, first in the chain (see below).
- **The seven stomps** turn the effect blocks on and off: **1 Dynamics, 2 Saturation, 3 Doubler** on the top row,
  **4 Pitch, 5 Modulation, 6 Delay, 7 Reverb** on the bottom. Click a stomp's name to show its knobs below. Signal
  chain: low cut → pitch → dynamics → de-esser → filter / pitch → saturation → presence → graphic EQ → doubler → modulation →
  delay → reverb → lo-fi → output → limiter, with HARMONY's voices joining just before the doubler. (PITCH works first in the sound, where it tracks the clean voice best, though its stomp is 4.) The **PITCH** stomp switches
  HARMONY, the octaver and TUNE VOICE together: off means your plain voice, and pressing it again brings back exactly
  what was on (TUNE VOICE in the mode it had).
  - **1 Dynamics:** a compressor and a noise gate (threshold; all the way down is off). The compressor has three TYPEs:
    **Classic** (threshold, ratio, level); **Daybreak**, a modern pedal compressor: COMP (how much, with automatic make-up),
    DETAIL (how much of the attack gets through), MIX (parallel compression), INTENSITY (smooth or hard) and LEVEL; and
    **Revival**, a three-control vocal leveller inspired by Waves Renaissance Vox: **GATE** (gently eases noise down below it),
    **COMPRESSION** (pull it down for a more even, louder voice) and **GAIN** (a limiter's ceiling: nothing comes out above
    it). The panel shows a gain-reduction meter. The stomp also switches the **de-esser** (its own **DE-ESS** block in the signal chain, right
    after): it turns down "s", "sh" and "t" sounds and leaves vowels exactly as they were. **FREQUENCY** is where esses
    are listened for (6 kHz to start), **AMOUNT** the most they're turned down (6 dB to start), and **LISTEN** plays only
    what's being taken away, so you can sweep FREQUENCY until you hear just the esses. It has its own gain-reduction meter.
  - **2 Saturation:** **MARS**, a multi-band saturator inspired by FabFilter Saturn 2: up to four frequency bands,
    each with its own STYLE (21: tubes, tapes, transformers, amps and effects like Smudge, Breakdown and Destroy), DRIVE
    (its level kept even), DYNAMICS, FEEDBACK, a four-band TONE, MIX and LEVEL; click and drag the bands on its display.
    Since 0.15.0 its knobs can move on their own (**MOD** and **SLOTS** pages): two step LFOs (XLFO), two envelope
    generators, two envelope followers, two MIDI sources, two XY pads and the expression pedal, connected through 16
    slots. Right-click a knob to modulate it.
    (The guitar pedal models PR7-G had went in 0.14.0; rigs and projects that used them load as MARS, set up close.)
  - **3 Doubler:** up to four extra "takes" of your voice, inspired by Waves Doubler. Each voice is a copy that's
    truly a few cents out of tune (its timing kept), a little late, and placed left to right: it sounds like a second
    take sung alongside rather than a chorus. Its panel is a table, a row per voice: **GAIN**, **PAN**, **DELAY**
    (0-100 ms; the detuning itself takes about 13 ms, so shorter settings come out at about that), **FDBK** (repeats),
    **OCT** (an octave down), **DETUNE** (cents), a pitch wobble, **DEPTH** at **RATE**, and **DRIFT** (how loose the take
    is: its timing and tuning wander slowly at random, as a real second take's do). Beside it: your own voice
    (**DIRECT** on, gain, pan), **OUTPUT**, LOW and HIGH shelves on the doubles only, **FB HIGH-PASS** (keeps repeats
    clear) and **MOD RESET** (starts every wobble together). Drag a box up or down; double-click to reset. It starts
    with two strong voices, one each side, drifting. On a mono track the voices stay at full level. (Until 0.3.0 this was one of MODULATION's types; old rigs and projects move to this
    stomp by themselves.)
  - **4 Pitch:** **HARMONY** (inspired by Waves Harmony) and the octaver. The stomp brings back whichever were on; its
    first press switches on HARMONY. Its panel has two views:
    - **HARMONY:** up to eight harmony voices made from your own voice, kept in the song's **KEY** and **SCALE** (set these
      first; they stay put when you change rigs). **SINGING** shows the note it hears. On the **graph** each voice is a dot
      with its interval (+4 a third up, +7 a fifth, U unison): left to right is its pan, bottom to top its level. Click an
      empty spot to add a voice, drag to move it, scroll over it to change its interval, right-click to remove it; the
      tabs **1-8** pick a voice (or add one in the middle). For the picked voice: **ON**, **CORRECT** (keep it in the key:
      when the exact interval isn't in the key, it takes the in-key one of the same kind, a third stays a third),
      **PITCH** (-24 to +24 semitones), **FINE** (cents) and **FORMANT** (the voice's character: lower for a bigger
      singer, higher for a smaller one, without changing the note). The mix: **SOURCE** (your own voice), **HARMONY** (all
      the voices), **SPREAD** (stereo width) and **CORR. FORMANT** (100%: raised voices keep your vowels, like another
      singer; 0%: they move with the pitch, like a sped-up tape). It starts with a third above on the left and a fifth
      above on the right. The harmonies come out a few milliseconds after you (two vocal pulses: about 8 ms on a high
      note to 25 ms on a low one); your own voice isn't delayed.
      **TUNE VOICE** (top right; Waves Harmony calls it CORRECT) tunes your own voice to the nearest note in the KEY and
      SCALE (SCALE **Chromatic**: the nearest semitone, for any song): **Natural** glides to the note so vibrato and slides
      survive, **Hard** jumps to it (the robotic sound). It works with HARMONY off too, and SINGING shows the note it's
      tuning to. While it's on, the whole plugin runs 40 ms late, which it tells your DAW so the vocal is lined back up
      on playback; singing live through it you'll hear yourself 40 ms late. Off, there's no delay at all.
      The **NOTES** tab (after voices 1-8): **GENERATE** turns your singing into steady notes (a new one after a silence,
      or when your pitch moves more than **TOLERANCE** cents for longer than **TIME**; nothing below **MIN NOTE**), and
      **CHORD** turns each note into harmonies, kept in the key: the same intervals on every note (3rd Above, 5th Above,
      Octaves, Power, Choir...) or a chord from it (Triad Below, Seventh, Sus2, Sus4, Add9...). These **note voices**
      play their notes exactly, whatever your own tuning, and show on the graph in blue while they sound; they have
      their own **LEVEL**, **WIDTH**, **FORMANT**, **ATTACK** and **RELEASE**, and come on top of the voices on the graph.
      TUNE VOICE's **Notes** setting tunes your voice hard to the generated note. **MIDI** keys played into PR7-V
      become note voices too: **As Played** (exactly the keys) or **Through Chord** (each key through the CHORD map);
      velocity sets their level and the sustain pedal holds them, and TUNE VOICE's Notes then follows the nearest held
      key. A keyboard strip under the graph shows the keys held (pink) and the generated note (blue).
      **EDIT** (beside CHORD) opens the **Note Map Editor**: CHORD becomes **Custom**, starting as a copy of the map
      you had. Pick a note in **PLAYS**, then click keys to set what it plays; **SPREAD** moves the harmony with the octave
      you sing, **FOLD** keeps it where it's set. **SCALE Custom** lets each note of the scale row be In, moved up
      (**>**) or down (**<**). **FROM CHORD** starts from a built-in map, **ERASE** clears it, **DONE** closes it.
    - **OCTAVE / FREEZE:** **TUNING** shifts everything -12 to +12 semitones; the **OCTAVER** blends two octaves down,
      one down, the dry voice and one up; **FREEZE** (its own switch) holds what you just sang as an endless pad you can
      sing over (with LEVEL, ATTACK and RELEASE).
  - **5 Modulation:** ten types: chorus, flanger, phaser, tremolo, Dimension chorus, Uni-Vibe, rotary (Leslie),
    vibrato, harmonic tremolo and auto-pan. SYNC locks the rate to the tapped or DAW tempo.
  - **6 Delay:** syncs to your DAW's tempo (or set SYNC to Off for milliseconds), has a tone control for darker repeats,
    and optional ping-pong; eight types: Digital (the original), Analog, Tape, Mod, Reverse, Multi-Tap, Shimmer and
    Ducking. Up to 3.5 seconds. Its panel has three pages: **MAIN**; **STEREO** (DUAL: the right side's own time;
    ping-pong's START side; FLIP LEFT / RIGHT); and **COLOUR** (LOW CUT and HIGH CUT on the repeats, MOD DEPTH and RATE,
    ANALOG 1 Warm / 2 Round / 3 Hiss / 4 Grit, LO-FI), inspired by Waves H-Delay. FEEDBACK goes to 200%: past 100% the
    repeats build up and hold.
  - **7 Reverb:** nine types: Room, Hall, Cathedral, Plate, Spring, Digital, Reverse, Chamber and Classic (the original),
    with SIZE, DECAY, PRE-DELAY, TONE and MIX. Inspired by the Lexicon LXP reverbs, its panel has four pages: **MAIN**;
    **DECAY** (BASS RT and CROSSOVER: lows longer or shorter than the rest; ROLLOFF); **SPACE** (DIFFUSION, the EARLY and
    REVERB levels, SPREAD and SHAPE for building, gated and inverse reverbs); and **MOTION** (SPIN and WANDER). Hover over
    the REVERB stomp to see the tail's length in seconds. Delay and reverb both have a DUCK knob that tucks the repeats or reverb under your
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
  (banks A-F), tags and favourites rigs. The factory rigs are for the voice, in six banks: **A** everyday vocals, **B** rock and grit, **C** delays and throws, **D** spaces, **E** character (doubles, harmonies, modulation, lo-fi), **F** signature styles (the vocal sounds of genres and eras: pop-punk, metalcore, prog metal, grunge, emo, nu-metal, deathcore, whisper pop, bedroom indie, 80s synth-pop).
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
