# Changelog

Each version's notes appear on its GitHub Release page.

## Next (not released yet: waits for the TUNE VOICE release, then goes out with it)

- **AEON replaces the generic flanger:** a flanger modelled on the Boss BF-2, built stage by stage from its schematic with the real component values. It's the same model as in PR7-B 0.11.0, and the first in a plan to model a specific pedal for each modulation effect.
  - **The pedal's four knobs:** MANUAL (the delay time), DEPTH, RATE and RES (resonance). MIX is greyed out, because the pedal always mixes dry and wet 50/50, which makes the notches deep.
  - **The delay:** a bucket-brigade chip (BBD) model, 13 ms down to 1 ms across MANUAL, the pedal's specified range. The old flanger only reached 0.5 to 5.5 ms, so AEON sweeps much lower and deeper. At full DEPTH, MANUAL does nothing, as the pedal's manual says.
  - **The sweep:** a triangle LFO, from one cycle every 16 s to 10 per second. At fast RATE settings the sweep gets narrower, as on the pedal, where a capacitor smooths it.
  - **RES:** rings the peaks hardest in the treble, the BF-2's metallic "zing", without howling.
  - **Mono, like the pedal:** both sides get the same sound. The old flanger swept the left and right sides a quarter-cycle apart.
  - **Your sound:** rigs and projects that used the old flanger now get AEON, with MANUAL and RES at 5. Expect it to be louder (up to about 6 dB on the comb's peaks), because dry and wet add at full level.
  - **Two details are estimates,** because the schematic can't show them: how much signal the BBD chip and its output stage lose, and where the factory set the RES trimmer. Both are set so full RES rings hard without howling.
- **Jet Voice (E7) and Metallic Voice (E13)** now use AEON: RES well up for the whoosh, nearly full with a short delay for the metallic edge.

## 0.16.0

- **A new bank of factory rigs, F: signature styles.** Sixteen rigs for the vocal sounds of genres and eras, named for the style rather than any artist:
  - **Pop-Punk Lead** (squeezed, bright, double-tracked tight), **Pop-Punk Chorus** (four takes wide, a third above tucked in) and **Pop-Rock Ballad** (warm and close, a long plate, a dotted-eighth echo).
  - **Ethereal Metalcore Clean** (airy, on a wide ping-pong echo and a big hall that ducks out of the way), **Metalcore Scream** (two bands of MARS that tear up harder the louder you scream) and **Haunted Clean** (a ghostly octave above, glitch texture, a cathedral).
  - **Prog Tenor Stack** (harmonies a third and a fifth above, doubled, a dual echo), **Prog Scream** (mid-forward, an octave-down take underneath) and **Soaring Prog Chorus** (four takes, an octave-up harmony, wide echoes, a long hall).
  - **Grunge Double**, **Emo Anthem**, **Nu-Metal Shout**, **Deathcore Growl** (an octave-down take at full weight beneath), **Whisper Pop**, **Bedroom Indie** and **Synth-Pop 80s**.
- **The harmony rigs follow your song's key.** Set KEY and SCALE in the PITCH panel; a rig change keeps them.
- **They're our reading of each style** (doubling, compression, delays, saturation, as such records are commonly made), not anyone's actual settings. Like all the factory rigs, their levels are balanced against each other.

## 0.15.1

- **Vocal presets for the GRAPHIC EQ.** The PRESETS menu now starts with a **VOCAL** group of 13 curves for a voice:
  - **Vocal Clarity:** rumble out, a little mud cut, presence and air. A good first try.
  - **Air & Shine:** a lift at the very top for a breathy pop sheen.
  - **Presence Push:** upper mids forward, to cut through a busy band.
  - **Boxy Cut:** takes out the cardboard-box sound of a small room or a close mic.
  - **Nasal Fix:** a dip around 1 kHz, where a voice can sound honky.
  - **Harsh Tamer:** softens a hard, piercing top (and some sibilance).
  - **Warm & Close:** chest and warmth up, the top softened.
  - **Deep Voice** and **Bright Voice:** starting points for lower and higher voices.
  - **Rap Upfront:** punchy and forward, consonants pushed.
  - **Spoken Word:** for podcasts and voice-overs: clear, never boomy or harsh.
  - **Backing Vocals:** thinner and softer, so harmonies sit behind the lead.
  - **Telephone:** only the middle of the voice, like a phone line.
- **Matched in loudness:** each preset's LEVEL makes up for what its curve adds or takes away over a voice's range (100 Hz to 8 kHz), so switching presets compares tone, not volume.
- **Where they come from:** our own curves, from the moves engineers commonly make on vocals, not copies of any product's presets. They're starting points: every voice and mic is different, so tweak the faders to taste.
- **The guitar curves from PR7-G** are still there, under **INSTRUMENT**.

## 0.15.0

- **MARS can move by itself: modulation (stage 2), inspired by FabFilter Saturn 2's.** Almost every MARS knob can be moved by a modulation source as you sing. Two new pages: **MOD** (the sources) and **SLOTS** (what moves what).
- **The sources:**
  - **XLFO 1 and 2:** step LFOs. 1 to 16 steps, each with its own value, GLIDE, CURVE (Linear, Sqr, Sqrt, Sine) and RANDOM. RATE in Hz, or SYNC to the song (16 bars to 1/64, with OFFSET x0.5 to x2; locked to the bar while the song plays). Also BALANCE (one half of the wave quicker than the other), GLIDE for all steps, PHASE, and a MIDI note can restart it (Retrigger or Legato). Edit the steps on the display: click one to pick it, drag it up or down, + and - to add (a copy) or remove one, and drag the triangle for PHASE. They start as a smooth wave.
  - **EG 1 and 2:** envelope generators (DELAY, ATTACK, DECAY, SUSTAIN, HOLD, RELEASE, each curve bendable), set off by your voice going over THRESHOLD, by one band, or by MIDI notes. RANGE centred makes it swing both ways around the sustain. Drag the points and curve dots on the display.
  - **EF 1 and 2:** envelope followers, following how loud your voice (or a band) is, with ATTACK, RELEASE and GAIN. TRANSIENT mode reacts only to attacks, such as word starts and consonants.
  - **MIDI 1 and 2:** velocity, pitch bend, mod wheel, aftertouch or any controller from your DAW, through a RESPONSE curve.
  - **XY 1 and 2:** a pad to move by hand (or automate), or a single SLIDER.
  - **The expression pedal** as a source too, whatever the pedal is assigned to. This one is our addition.
- **16 slots,** each a SOURCE, a TARGET and an AMOUNT (-100% to +100%). Targets are every band's DRIVE, DYNAMICS, FEEDBACK, FB FREQ, BASS, MID, TREBLE, PRESENCE, MIX and LEVEL; the CROSSOVERS, INPUT, OUTPUT and MIX; the XLFOs', EGs' and EFs' own settings; and other slots' amounts (for example, an XY pad that sets how much an XLFO does).
- **Right-click any of those knobs** to modulate it with a source; it takes the next free slot at +50%. The same menu removes a slot. Saturn 2 lets you drag a source onto a knob instead.
- **A modulated knob shows it:** a thin ring in the source's colour (yellow when more than one kind moves it), with a bright arc to where the knob is right now. The band display's drive bars and crossovers move too.
- **Our design, where the manual doesn't say:** the curves, how the followers read level (-48 dB to 0 dB), what TRANSIENT listens for, and the fixed number of sources (Saturn adds as many as you like). Not built: XLFO Snap (Saturn's arpeggiator), renaming sources, the external side-chain trigger, and dragging sources onto knobs.
- **Five new factory rigs** (bank E): **Breathing Tape** (louder singing pushes the tape harder), **Throb** (a saturated tube pulsing in eighth notes), **Sweeping Radio** (a slow tempo sweep of MARS's crossover), **Pedal Grit** (the expression pedal turns the grit up) and **Stutter Steps** (an eight-step rhythmic gate).
- **MARS starts where its knobs are set.** When the plugin loaded, MARS's levels used to fade up from silence over the first 50 ms, and with a slot in use its knobs glided up from zero. Now they start at their settings.
- **CPU:** the modulation moves the knobs about 3000 times a second. With all 16 slots busy on four heavy bands, MARS used about 7% of one core in our tests. Without modulation it runs exactly as before.

## 0.14.1

- **No more screws on the face plate, and no MIN light.** The four screw heads in its corners and the MIN light beside the expression pedal are gone, for a cleaner front (released on its own as 0.9.1, and now joined with everything since). The dock's EXPRESSION ASSIGN screen still shows where the pedal sits.

## 0.14.0

- **SATURATION is MARS.** The four guitar pedal models PR7-V inherited from PR7-G (Classic, Screamer, Muff Fuzz and Face Fuzz) are gone, and with them the MODEL menu: SATURATION now always opens MARS.
- **Your rigs and projects still load.** Any that used one of the old pedals is set up in MARS to sound close to it. It's a different engine, so close rather than identical:
  - **Screamer** becomes two bands: below 720 Hz left clean, above it Saturated Tube with a mid push. That's the Tube Screamer's trick of keeping the bass clean while driving the mids.
  - **Muff Fuzz** becomes American Amp with the mids scooped; **Face Fuzz** becomes Broken Tube; **Classic** becomes Saturated Tube.
  - The old DRIVE, TONE and LEVEL carry over to the band's DRIVE, TREBLE and LEVEL.
  - If the drive was off and never touched, MARS starts as normal.
- **The four factory rigs that used the old pedals** now use MARS, set up for a voice: **Rock Lead** (edge on the mids only, the lows clean), **Grit Shout** (British Amp with extra presence), **Garage Megaphone** (Crunchy Amp) and **Gang Vocal** (Warm Tube). Their levels are balanced with the other rigs.

## 0.13.0

- **MARS: a multi-band saturator for SATURATION.** Pick MARS as SATURATION's MODEL. Inspired by FabFilter Saturn 2: our own design, following how its manual describes it.
  - **Up to four bands.** Your voice is split by frequency, and each band is saturated its own way: for example, clean lows so nothing gets muddy, gritty mids for bite, smooth tape on the highs. The bands add back up exactly when nothing is done to them.
  - **The display:** each band is a column, its DRIVE a bar. Click a band to edit it, drag a crossover line to move it, drag up or down in a band for its DRIVE, double-click to split a band (the new one copies it), right-click to remove one. BANDS sets how many.
  - **21 STYLES**, in Saturn 2's families: **Tube** (Clean, Warm, Saturated, Broken), **Tape** (Clean, Warm, Saturated, Extreme), **Transformer** (Subtle, Warm, Lush), **Amp** (American, British, Smooth, Crunchy, Screaming) and **FX** (Smudge: the sound smeared and stretched; Breakdown: pitched down and crushed; Foldback; Rectify; Destroy: bits and sample rate crushed).
  - **Each band:** DRIVE (its level stays about the same as you turn it up, so you hear the character, not just louder), DYNAMICS (left: a gate; right: pumping compression), FEEDBACK and FB FREQ (the band rings, like a mic near its speaker; it stops when you stop singing), BASS, MID, TREBLE, PRESENCE, MIX (clean against saturated) and LEVEL.
  - **GLOBAL page:** INPUT, OUTPUT, MIX, QUALITY (oversampling: Off, Good 4x, Superb 8x; cleaner highs on heavy drive, for more CPU) and the crossovers' SLOPE (6 to 48 dB per octave). Still no latency.
  - **Three new factory rigs:** Tape Warmth (A07), Multiband Grit (B06) and Broken Transmission (E15).
- **Honest limits:**
  - Saturn 2's manual describes what each style family is for, not how it's built, so all 21 curves are our own.
  - Its modulation system (LFOs, envelopes, XY pads) isn't built yet: that's the next stage. Nor are band solo/mute and linear-phase mode.
  - SATURATION sits in the mono part of the vocal chain, so there are no pan or mid/side controls.
  - At the heaviest (four bands of the most complex styles, Superb), it uses about a fifth of one CPU core; one or two bands at Good, far less.
- The four pedal models are unchanged, and rigs that use them sound the same.

## 0.12.0

- **42 factory rigs for the voice.** The guitar rigs PR7-V started with are gone; your own saved rigs are untouched. They show off the new delay, reverb, doubler and Revival compressor. Most start with a compressor and the de-esser, as a vocal chain would. Five banks:
  - **A: everyday vocals.** Clean Slate, Studio Vocal, Pop Lead, Intimate, Radio Ready, Podcast Voice.
  - **B: rock and grit.** Rock Lead, Grit Shout, Garage Megaphone, Gang Vocal (four loose takes), Arena Rock.
  - **C: delays and throws.** Slapback, Dotted Throw, Ping-Pong Echo, Dub Echo, Wide Dual Echo, Lo-Fi Echo, Endless Echo (feedback past 100%: it keeps swelling until you switch the delay off), Pedal Throw (the expression pedal fades the echoes and hall in), Backwards Echo.
  - **D: spaces.** Vocal Plate, Big Hall, Echo Chamber, Cathedral Choir, Worship Wash, Gated 80s, Reverse Swell, Floating.
  - **E: character.** Big Double, Octave Double, Harmony Stack (set KEY and SCALE in the PITCH panel first), Dream Chorus, Rotary Voice, Phaser Dream, Jet Voice, Wide Pan, Tremolo Ghost, Telephone, Old Radio, Cassette Memory, Metallic Voice, Trap Ad-Lib.
- **Even levels:** every rig was played through and its RIG LEVEL set so they sit within a few dB of each other, so switching doesn't make you jump. Clean Slate (everything off) is quieter, because it has no compressor lifting your voice.
- They're starting points tuned by measurement, not by ear: adjust to your voice and mic, and save your own versions.

## 0.11.0

- **Includes 0.10.0's bigger DELAY** (its notes are below): 0.10.0 was never published, because its macOS build failed its tests.
- **A bigger REVERB, inspired by the Lexicon LXP reverbs.** Our own design, following how their manual describes them. The REVERB panel now has four pages, like a rack reverb's edit pages:
  - **MAIN:** as before, plus **DECAY**: how long the tail rings, as a percentage of what TYPE and SIZE give (200% is twice as long, 50% half). It's a percentage rather than seconds so that switching TYPE still gives each space its own natural length. **Hover over the REVERB stomp to see the length in seconds.**
  - **DECAY page:**
    - **BASS RT and CROSSOVER:** below CROSSOVER, the lows ring longer (above 1.0x) or shorter (below 1.0x) than the rest. Real halls hold their lows longer; for a voice, shorter lows (around 0.7x) keep the reverb from muddying it.
    - **ROLLOFF:** a high cut on the reverb's output, to take the sizzle off. Off at the top.
  - **SPACE page:**
    - **DIFFUSION:** how fast the first echoes melt into a smooth wash. Turn it down to hear them as separate repeats.
    - **EARLY and REVERB:** the first reflections and the tail, each at its own level. All EARLY and no REVERB gives a tight, close room sound.
    - **SPREAD and SHAPE:** the sound fed into the reverb over a stretch of time (SPREAD, up to 300 ms) rather than all at once; SHAPE puts most of it early, evenly (50%) or late. With SHAPE high and DECAY short, the reverb builds and then stops: the gated sound of the 80s, or an inverse reverb.
  - **MOTION page:** **SPIN** (how fast the tail's movement is) and **WANDER** (how far it moves). Less makes it still and pure, more makes it lush and chorused.
- **New type: CHAMBER.** An echo chamber, the original studio reverb: a small, hard room that turns dense straight away with no slap off its walls. Shorter than a hall, smoother than a room; a classic on vocals.
- The new controls work on Room, Hall, Cathedral, Plate, Digital, Reverse and Chamber. Classic and Spring keep their own sound (their controls on the new pages are greyed).
- **Your rigs sound the same:** every new control starts where it changes nothing.
- **Not built:** the LXP's individual echo and reflection editors, Tap Slope, and its moving display. TONE still does the job of its RT Hi Cut.

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

## 0.9.1

- **No more screws on the face plate.** The four screw heads in its corners are gone, for a cleaner front.
- **The MIN light beside the expression pedal is gone too.** It lit when the pedal was fully back on its heel; the dock's EXPRESSION ASSIGN screen already shows where the pedal sits.

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
