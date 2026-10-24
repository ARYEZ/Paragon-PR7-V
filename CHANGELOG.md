# Changelog

Each version's notes appear on its GitHub Release page.

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
