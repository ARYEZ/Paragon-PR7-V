# Changelog

Each version's notes appear on its GitHub Release page.

## 0.10.0

- **A bigger DELAY, inspired by Waves H-Delay.** Our own design, following how its user guide describes it. The eight types (Digital, Analog, Tape, Mod, Reverse, Multi-Tap, Shimmer, Ducking) stay, and every one of them gets the new controls. The DELAY panel now has three pages:
  - **MAIN:** as before.
  - **STEREO:**
    - **DUAL:** the right side gets its own time or note value, for example 1/4 on the left and 1/8 dotted on the right, for wide, rhythmic echoes.
    - **START:** which side ping-pong's first echo comes from.
    - **FLIP LEFT / RIGHT:** turns one side's echoes upside down (phase), for an extra-wide, hollow sound. Careful on a mono track: it can cancel.
  - **COLOUR:**
    - **LOW CUT and HIGH CUT:** filters on the repeats. Each repeat goes through them again, so a few passes thin and darken the echoes the way old units did. Turn LOW CUT up to keep echoes from muddying your low notes; turn HIGH CUT down to tuck them behind your voice. Off at their ends.
    - **MOD DEPTH and RATE:** sweeps the echo time, for a chorus or flange on the echoes (flanging on short times).
    - **ANALOG:** four characters of our own (the guide doesn't say what its four do): **1 Warm** (subtle), **2 Round** (warmer and softer), **3 Hiss** (2 with tape hiss), **4 Grit** (driven, dark and noisy).
    - **LO-FI:** an early digital delay's narrow band and grainy 12-bit sound.
- **FEEDBACK up to 200%:** past 100% the repeats build up instead of fading, into an endless swell, held at about full level rather than running away. Bring it back under 100% to let it die away.
- **Up to 3.5 seconds** of delay (from 2.5).
- **Your rigs sound the same:** every new control starts where it does nothing.
- **Not built:** H-Delay's LINK (moving both filters together) and its OUTPUT knob (MIX does that job here).

## 0.9.0

- **A stronger DOUBLER.** You found the doubles too subtle on a voice. There were four reasons, all fixed:
  - **The takes sat too far back.** The starting settings followed the Waves Doubler guide's recipe (-6 dB), made for tucking a double behind a lead in a mix. Now each take starts at **-2 dB**, nearly as loud as you.
  - **They never moved.** A copy that's always exactly as late and exactly as out of tune, your ear blends back into your voice: it hears one voice, a bit wider. A real second take wanders. New **DRIFT** on each voice (our own addition): the take's timing wanders slowly at random, up to 6 ms early or late, and its tuning up to 6 cents, each voice its own way. It starts at 50%; 0% is the steady copy it used to be.
  - **Voice 1 was really closer than its DELAY said.** The detuning takes about 13 ms by itself, so its 9.4 ms came out at about 13: almost on top of your voice. The takes now start at **18 and 31 ms**, more clearly apart, and further out of tune (**9 cents**, from 4).
  - **On a mono track the takes were halved.** Panned hard to one side, each lost 6 dB more when the two sides were added together. Now each take keeps its full level on a mono output.
  - Voices 3 and 4 (still off to start) are set up as a wider pair: -6 dB, 60% left and right, 24 and 40 ms, 14 cents.
- **Your saved rigs and projects sound as they did:** they keep the settings they were saved with and come back with DRIFT at 0. Only a newly switched-on DOUBLER (and the Wide Doubler factory rig) starts with the stronger settings.
- **Honest limits:** a take whose timing moves bends its pitch a little while it moves, as a singer's does; DRIFT glides slowly so that stays within a few cents (measured: about 8 either way at most, at 100%). How "doubled" it sounds is a matter for your ears: try DRIFT between 30% and 80%.

## 0.8.0

- **Revival: a vocal compressor in three moves.** A third TYPE for the DYNAMICS compressor, after Classic and Daybreak. Inspired by Waves Renaissance Vox: our own design, following how its user guide describes it.
  - **COMPRESSION:** one fader that does the work of threshold and ratio together. Pull it down and the voice gets more even and louder: soft words come up, loud ones are held. At 0 it does nothing; at -36 dB it's a heavy squeeze. Set this first.
  - **GAIN:** the ceiling of a limiter at the end. Nothing comes out above it, however far down COMPRESSION goes, so it can't clip. Lower it to hit the limiter harder (a denser sound) or as a level control for the whole block.
  - **GATE:** set it last. Below it, quiet sounds (room noise, breaths, headphone spill) are eased down gently rather than chopped off: 6 dB under GATE is turned down about 6 dB. All the way down is OFF. Set it just under your quietest sung words.
  - With Revival picked, its own GATE and GAIN take the place of the noise gate and LEVEL.
  - The meter shows the gate and compression together. No delay, as with everything else in the chain.
- **Honest limits:** the guide explains what the three controls do but not how fast it reacts or the exact curve, so those are our choices: it grabs in 3 ms and lets go in 60 to 400 ms (slower while it's been working hard, so held notes don't pump). It won't sound identical to the original; please judge it by ear.

## 0.7.0

- **A new DOUBLER: four voices, each set your way.** Inspired by Waves Doubler: our own design, following how its user guide describes it.
  - **Real detuning:** each voice is a copy of your voice moved a few cents out of tune with its timing kept, rather than a delay swept back and forth (which is what a chorus does). A copy that's a little late and a little out of tune sounds like a second take sung alongside you.
  - **The panel is a table,** a row per voice: **GAIN**, **PAN**, **DELAY** (0-100 ms), **FDBK** (repeats of the voice), **OCT** (an octave down), **DETUNE** (cents; 2-12 sounds like a double) and a slow pitch wobble, **DEPTH** at **RATE**. Drag a box up or down; double-click it to reset.
  - **Beside it:** your own voice (**DIRECT** on, gain and pan), **OUTPUT** for everything, a **LOW** and a **HIGH** shelf on the doubles only (thin them out or brighten them without touching your voice), **FB HIGH-PASS** to keep repeats clear, and **MOD RESET** to start every voice's wobble together.
  - It starts with two voices, one each side (9.4 ms and 4 cents flat on the left, 23.7 ms and 4 cents sharp on the right). Voices 3 and 4 are set up and waiting: switch them on for a fuller stack.
  - The expression pedal's MORPH still fades the doubles in from the heel.
- **Honest limits:**
  - The detuning has a delay of its own, about 13 ms, so DELAY settings below that come out at about 13 ms.
  - Not built (yet): Waves' ALIGN, its delay RANGE switch and its graphs.
  - Rigs and projects using the old doubler (RATE, DEPTH, MIX) open with two voices set to sound close to it, but not identical: it's a different engine.

## 0.6.0

- **HARMONY, on the PITCH stomp.** Up to eight harmony voices made from your own voice, kept in your song's key. Inspired by Waves Harmony: our own design, following how its user guide describes it. This is the first stage; more of it is coming.
  - **KEY and SCALE:** set them for the song first (13 scales, from Major and the minors to the modes, pentatonics and Blues). They stay put when you change rigs.
  - **The graph:** each voice is a dot showing its interval (+4 a third up, +7 a fifth, U unison). Left to right is its pan, bottom to top its level. Click an empty spot to add a voice, drag it to move it, scroll over it to change its interval, right-click to remove it. Tabs 1-8 pick a voice.
  - **Each voice:** ON, **CORRECT** (keep it in the key), **PITCH** (-24 to +24 semitones), **FINE** (cents) and **FORMANT** (a bigger or smaller-sounding singer on the same note).
  - **In the key:** with CORRECT on, a voice lands in tune with the key, even when you're a little off. When its exact interval isn't in the key, it takes the in-key note of the same kind: a third stays a third, major or minor as the key wants, as a backing singer would sing it.
  - **Sounds like a person:** raised and lowered voices keep your vowels, rather than going chipmunk or monster. **CORR. FORMANT** turns that down for the sped-up-tape sound on purpose.
  - **The mix:** SOURCE (your own voice), HARMONY (all the voices), SPREAD (how wide).
  - **SINGING** shows the note it hears you sing.
  - It starts with a third above on the left and a fifth above on the right. The PITCH stomp turns it on.
  - The harmonies take their tone from your voice after DYNAMICS, SATURATION and the EQ, then go through the DOUBLER, MODULATION, DELAY and REVERB with you.
  - Your own voice still has no delay. The harmony voices come a few milliseconds after it (about 8 ms on a high note to 25 ms on a low one), like singers standing a few metres away.
- **The PITCH stomp remembers.** It switches HARMONY and the octaver: pressing it off remembers which were on, and pressing it on brings back exactly those. The DYNAMICS stomp does the same with the compressor and de-esser. PITCH's panel has two views, HARMONY and OCTAVE / FREEZE.
- **TUNING** (in OCTAVE / FREEZE) now reads in semitones ("+2 st") instead of guitar tunings like "E standard" and "Capo 2".

## 0.5.0

- **De-esser, on the DYNAMICS stomp.** It turns down "s", "sh" and "t" sounds and leaves everything else exactly as it was.
  - The DYNAMICS stomp now switches the compressor and the de-esser together. The de-esser's knobs are in its own **DE-ESS** block in the signal chain, right after DYNAMICS (and it comes after the compressor in the sound, so it catches esses the compressor has brought up).
  - **FREQUENCY:** where it listens for esses, 2 to 12 kHz (6 kHz to start).
  - **AMOUNT:** the most it turns them down, up to 18 dB (6 dB to start).
  - **LISTEN:** plays only what's being taken away: the esses, and silence in between. Sweep FREQUENCY until LISTEN has just the esses and none of your voice.
  - A gain-reduction meter shows how much it's taking off each ess.
  - It judges esses against the rest of your voice rather than by loudness, so it treats a quiet "s" and a loud one the same. Between esses your voice comes through untouched.

## 0.4.0

- **Master LIMITER under OUTPUT.** The last thing in the chain: nothing leaves the plugin above its ceiling, so a loud note or a shout can't clip in your DAW.
  - On at **-1 dB** to start, the usual safety margin. Turn it down (to -12 dB) to hold the level lower; all the way up is **OFF**.
  - A **light** beside it glows while it's turning peaks down.
  - **No delay:** most limiters hold the sound back a few milliseconds to see peaks coming, which you'd feel singing live. This one reacts on the spot instead, then lets go over a tenth of a second. The trade-off: on a sudden spike, the very first instant is shaped rather than faded; as a safety net that's the better deal.
  - Both sides turn down together, so the stereo picture stays put. Like OUTPUT, it stays the same when you change rigs.
  - Below the ceiling it does nothing at all to the sound.

## 0.3.0

- **Seven stomps, laid out for singing.** The top row is **1 Dynamics, 2 Saturation, 3 Doubler**; the bottom row **4 Pitch, 5 Modulation, 6 Delay, 7 Reverb**. Click a stomp's name to show its knobs.
  - The top row sits in the gaps of the bottom row, three over four, as on floor units, so your foot can't catch two switches at once.
  - **Delay and Reverb have their own switches** now (they used to share one).
  - **Pitch** switches the octaver; FREEZE keeps its own switch in the Pitch panel. Pitch still works first in the sound, where it follows a clean voice best.
- **Doubler is its own stomp (3).** It was one of Modulation's types and sounds exactly the same: you in the middle and a slightly loose "take" each side. Rigs and projects that used it switch to the new stomp by themselves. Modulation keeps its other ten types.
- **No amp or cabinet.** The NAM amp, the cabinet IR loader and the AMP, CABINET and RIG panels are gone: a voice doesn't need them, and rigs are still on the screen and the header.
  - **No latency:** nothing in the chain delays your voice any more, so singing through it live feels immediate.
  - Lighter: the whole chain uses less CPU, and the download is about a fifth smaller.
  - Rigs and projects that loaded a model or IR still open; the files are ignored.
- **Vocal looks:** a microphone at the start of the signal chain, and "VOCAL MULTI-FX PROCESSOR" on the unit.

## 0.2.0

- **LOW CUT for the microphone.** A new knob under INPUT: a high-pass filter, first in the chain, that clears out what a mic picks up below the voice.
  - It takes out rumble through the mic stand, hum, and the low thump of plosives ("p" and "b"), before the compressor or anything else can react to them.
  - Turn it to where the cut starts: **80 Hz** to begin with (below the lowest sung notes), up to 120-150 Hz for a thin or close-miked voice. All the way down is **OFF**.
  - 12 dB per octave: a natural-sounding slope that leaves the body of the voice alone.
  - Like INPUT, it's set for your mic and room rather than a sound, so it stays put when you change rigs.
- The INPUT meter shows what the mic sends, before LOW CUT, for setting the input level.

## 0.1.0

- **The vocal processor, based on Paragon PR7-G 0.32.1.** Everything PR7-G 0.32.1 has, under its own name, ready to be voiced for vocals.
- **Its own plugin.** Paragon PR7-V has its own name, ID and plugin code, so your DAW sees it as a separate plugin: it installs alongside Paragon PR7-G (and PR7-B) instead of replacing it, and projects using either keep the right one.
- **Its own rigs.** Rigs are saved in their own Paragon PR7-V folder. Guitar rigs from PR7-G aren't copied over.
- **Its own finish setting.** The finish you pick in SETTINGS (Pearl White, Pearl White + Neon or Pearl Black) is remembered for PR7-V separately from PR7-G.
- **The PR7-V logo** on the header and the unit's badge, in the same slanted, speed-cut lettering.
