# Changelog

Each version's notes appear on its GitHub Release page.

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
