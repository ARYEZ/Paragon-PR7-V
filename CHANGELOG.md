# Changelog

Each version's notes appear on its GitHub Release page.

## 0.28.0

- **The rack: one unit under the floorboard.** The faceplate, the controls beside it and EXPRESSION ASSIGN are now one wide PR7 rack unit: the "brain" to go with the floorboard processor. Your idea; the layout is ours.
  - **On the left, the block's display:** the faceplate's screens, live as before (the delay's repeats, the reverb's shape, the compressor's needle or meter, MARS's tubes, the note you sing...). The knobs and keys it used to *draw* are gone, as the real ones are right beside it.
  - **In the middle, the block's real controls,** in the PR7's own parts: the floorboard's big knobs with each value on a small orange screen, menus on screens, switches as small keys with a light stripe, pages (MAIN / STEREO / COLOUR...) as keys, names stamped on the face. Every one works as before (they're the same controls, with the same settings, automation and undo).
  - **HARMONY, the GRAPHIC EQ, the DOUBLER and MARS** are whole editors (graphs, tables, faders), so each sits on a big screen set into the face. They're laid out as before, a little wider, then scaled to fit the rack (the text is about four-fifths its old size): nothing in them is squeezed or cut off.
  - **At the right end, EXPRESSION ASSIGN:** its list screen, MIN / MAX, and its scroll arrows as the unit's keys.
  - **Printed red lines** divide the three parts, as the lines on the floorboard do.
  - **All three finishes:** Pearl White, Pearl White + Neon (neon knobs and keys) and Pearl Black.
- **Readouts fit their screens:** big seven-segment numbers (6.0 kHz, 0.80 Hz...) now shrink to fit a narrow screen instead of running off its edge.

## 0.27.0

- **HARMONY's GLOBAL controls were meant to be 0.26.0.** That build stopped on Linux with a crash inside the plugin checker, so 0.26.0 was never published. They arrive in this release (see 0.26.0 below).
- **HARMONY stage 8: SNAPSHOTS.** Inspired by the snapshots in Waves Harmony's guide: store up to 32 sets of harmony voices and switch between them instantly.
  - **Opening them:** the **SNAPSHOTS** button in the top-right corner of the HARMONY graph opens the snapshot page.
  - **The slots:** 32 of them, each showing the voices it holds (for example "+4 +7", or a flattened voice's note). The one in use is lit.
  - **Click a stored slot** to bring it back.
  - **STORE** saves the voices as they are now into the picked slot (the first empty one if none is picked).
  - **COPY, PASTE and DELETE** work on the picked slot; right-click a slot for the same.
- **What a snapshot holds** (as in the guide, "harmony controls only"):
  - **Stored:** every voice's settings (on, pitch, fine, formant, correct, pan, level, FLATTEN, delay, feedback, filter) and the note voices' sound.
  - **Left alone:** KEY, SCALE, the GLOBAL controls, the note generator and chord map, and the mix.
- **Switch them from your DAW:** the new **SNAPSHOT** parameter (–, 1 to 32) can be automated or controlled by MIDI. Moving it brings that snapshot back, which is handy for changing harmonies between song sections. An empty slot does nothing.
- **Saved with your rig and your project,** and storing, pasting or deleting can be undone.
- **Our design where the guide doesn't say:**
  - A recall switches straight to the stored settings.
  - Recalling from the DAW happens within about a 30th of a second of the parameter moving, not on the exact sample.
  - Loading a rig or project never recalls a snapshot by itself.
- **Not built:** the guide's Snapshot Selection *knob* on the panel. Its job is done by the SNAPSHOT parameter from your DAW, and by clicking a slot.

## 0.26.0

- **HARMONY stage 7: GLOBAL controls.** Inspired by the Global Controls section of Waves Harmony's guide. The new **GLOBAL** button sits beside the HARMONY switch.
- **The GLOBAL view** shows four new controls above the mix (SOURCE, HARMONY, SPREAD, CORR. FORMANT, which the guide also counts as global):
  - **GLIDE:** how long the harmony voices take to move to a new note, from 1 ms (straight there, as before) to 1 second. **SYNC** sets it as a note length at the song's tempo instead.
  - **PITCH:** moves every harmony voice up or down, up to an octave either way. It's applied after the key correction, so a third stays a third, an octave up.
  - **FORMANT:** changes the character of every harmony voice at once (lower for a bigger singer, higher for a smaller one), on top of each voice's own FORMANT.
- **GLOBAL\* tells you a global setting is changed,** like the guide's asterisk.
- **Everything starts where it changes nothing,** so rigs and projects sound as before.
- **Our design where the guide doesn't say:**
  - **Your own voice:** PITCH and FORMANT move only the harmony voices, not yours. "The entire process" could be read either way, and leaving your lead alone seemed the musical choice.
  - **What GLIDE smooths:** the whole pitch curve, so a long GLIDE also softens how closely the voices follow your vibrato.
  - **How long a glide takes:** GLIDE is the time to reach the new note.
  - **The note voices** keep their own quick 30 ms glide as a minimum.
  - **SYNC values:** PR7-V's usual note lengths.
- **Also:** the SINGING note on the HARMONY panel adapts to the space, showing the note and TUNE VOICE's target on one line if needed.

## 0.25.0

- **FACEPLATE stage 3: every block now has its own faceplate,** built from the same PR7 parts as the others.
- **MODULATION** is named for its TYPE ("CHORUS", "UNI-VIBE", "ROTARY"…).
  - A screen draws the wobble as a wave at its RATE and DEPTH, with a dot riding it. It keeps time with RATE, not the audio.
  - RATE shows in seven-segment Hz, or as the note length while synced.
  - DEPTH and MIX are on knobs.
- **FILTER / PITCH** is named for its TYPE:
  - Auto-Wah shows its filter's peak (sharper with RESONANCE), with SENS and MIX knobs.
  - Talk Box shows its vowel filter, with RES and MIX knobs.
  - Pitch Shifter shows its interval, with a MIX knob.
  - Whammy-Wah shows its program, with a MIX knob.
- **DE-ESSER:**
  - A live meter of how much it's turning the esses down, and the band it works on.
  - FREQUENCY in kHz.
  - An AMOUNT knob and a LISTEN key.
- **GLITCH** is named for its MODE.
  - A 16-step pattern, with more steps active the higher DENSITY is, and a light stepping through it at LENGTH. It keeps time with LENGTH, not the audio. PRE or POST is marked.
  - LENGTH in ms, or the note length.
  - DENSITY and MIX knobs.
- **DOUBLER:** its four voices on a screen, across by pan and down by delay (bigger when louder), with DIRECT marked, and a key per voice lit while it's on.
- **GRAPHIC EQ:** the EQ's own response curve (the same calculation its panel draws), its LEVEL, and a LEVEL knob.
- **LO-FI** is named for its TYPE. A wave shows what AMOUNT does (squashed, or stepped for Bitcrusher) and what NOISE adds, with AMOUNT and MIX knobs.
- **Settings or audio:** DE-ESSER (like DYNAMICS and MARS) shows live audio. The others draw from their settings, so they change as you turn the controls.

## 0.24.0

- **FACEPLATE stage 2: DELAY, REVERB and PITCH get their own faceplates,** built from the same PR7 parts as the first ones.
- **DELAY** is named for its TYPE ("TAPE ECHO", "ANALOG DELAY", "SHIMMER DELAY"…).
  - A screen shows the repeats laid out in time, each as loud as FEEDBACK leaves it. With DUAL on there's a row for each side, and PING-PONG is marked.
  - A light runs along the repeats at the delay time. It keeps time with your delay setting, not the audio itself.
  - TIME shows in the main display's seven-segment digits, or as the note length while synced.
  - FEEDBACK and MIX are on the unit's big knobs.
- **REVERB** is named for its TYPE ("HALL", "PLATE", "SPRING"…).
  - A screen draws its shape: the PRE-DELAY gap, the early reflections (as tall as EARLY), then the tail fading over the real decay time. SPRING ripples, and REVERSE swells up and cuts off.
  - DECAY shows in seconds in seven-segment digits.
  - SIZE and MIX are on knobs.
- **PITCH (HARMONY)** has two screens and three keys:
  - **Note screen:** the note you're singing, with TUNE VOICE's target beside it when it's on, and the KEY and SCALE.
  - **Voices screen:** the harmony voices by pan and interval, like a small version of the HARMONY graph. A flattened voice sits at its note, and note voices show as rings.
  - **Keys:** small keys for HARMONY, OCTAVE and FREEZE, lit while each is on.
- **Still the shared rack unit:** MODULATION, FILTER / PITCH, DE-ESSER, GLITCH, DOUBLER, GRAPHIC EQ and LO-FI, until stage 3.

## 0.23.1

- **The faceplates are now part of the PR7 line** (the owner's request): every one is built from the PR7's own parts instead of their own looks.
  - **The chassis:** each faceplate is a small PR7 unit, with the same pearl rim, satin face and glow, in your chosen finish (Pearl White, Pearl White + Neon or Pearl Black). It changes when you change the finish in SETTINGS.
  - **Screens:** the unit's own LCD screens, with readings in its orange LED light. Classic's gain-reduction needle is drawn in that light on a screen. The meters are rows of LED bars like the tuner's, and MARS's tubes glow behind a screen.
  - **Knobs, keys and faders:** Daybreak's COMP, DETAIL and MIX are the unit's big knobs (neon on the neon finishes). INTENSITY is one of the unit's small black keys, its light stripe lit while it's on. Revival's faders are the graphic EQ's.
  - **Lights and lettering:** the ON light is the LED bar from above the footswitches. Names are in the PARAGON badge's lettering, with a red printed line under each, as "VOCAL MULTI-FX PROCESSOR" is printed on the unit. Labels are stamped in the unit's ink.
- **Unchanged:** what each faceplate shows. The main unit looks exactly as before; it's now drawn by the same shared code as the faceplates, so the two always match.

## 0.23.0

- **The FACEPLATE** replaces the RIG BROWSER and SELECTED RIG panels under the unit. Both only repeated things available elsewhere: the PRESETS page, and the unit's own display and SAVE / SAVE AS buttons.
- **It shows the block you select as a piece of hardware** beside its controls, following the block's TYPE, with live meters:
  - **DYNAMICS, Classic:** a studio rack compressor with a VU-style needle showing gain reduction, and its THRESHOLD, RATIO and LEVEL in windows.
  - **DYNAMICS, Daybreak:** a boutique pedal with a sunrise finish, its COMP, DETAIL and MIX knobs, the INTENSITY switch and an LED ladder for gain reduction.
  - **DYNAMICS, Revival:** a vocal leveller that draws its GATE, COMPRESSION and GAIN as one input-to-output curve, with faders and a reduction meter.
  - **SATURATION (MARS):** a valve unit with a tube for each band, glowing as hard as the band is driven, its STYLE underneath.
  - **Every other block:** a rack unit with its icon, name, TYPE and an ON lamp, until it gets its own faceplate (DELAY, REVERB and PITCH/HARMONY are next).
- **Our own designs:** all are drawn in code, in the PR7's colours. None copies a real product's look; Revival is only *inspired by* the vocal leveller it's modelled on.
- **Unchanged:** the controls and EXPRESSION ASSIGN are where they were.

## 0.22.0

- **HARMONY stage 6: each voice gets its own DELAY, FEEDBACK and FILTER, and FLATTEN.** Inspired by the Harmony Panel and Notes Panel in Waves Harmony's guide.
- **FLATTEN** (beside ON and CORRECT) holds a voice on one note, whatever you sing: a drone or pedal note under your melody.
  - While it's on, the PITCH knob becomes **NOTE** (C1 to C5; it starts at C3, as in the guide).
  - CORRECT still keeps that note in the key, and FINE still applies.
  - On the graph, a flattened voice shows its note name (e.g. C3) instead of an interval.
- **FX** (the new button after NOTES) swaps the controls under the tabs for the selected voice's:
  - **DELAY:** brings the voice in later, from Off up to 1 second, or **SYNC**ed to a note length at the song's tempo (DELAY greys out while it is).
  - **FEEDBACK:** repeats of the delayed voice, from none to almost endless.
  - **FILTER:** low-pass, high-pass or band-pass, with **CUTOFF** from 20 Hz to 20 kHz.
  - On the NOTES tab, FX sets the note voices' shared DELAY, FEEDBACK and FILTER. Their echoes carry on after you let go of a key.
- **Everything starts switched off,** so your rigs and projects sound exactly as before.
- **Our design where the guide doesn't say:**
  - Each voice is filtered first, then delayed, so every repeat has the filtered tone.
  - The filter is 12 dB per octave.
  - DELAY can be Off (the guide starts at 1 ms).
  - The longest echo is 2 seconds, even when synced. 1/1 is capped at 2 seconds below 120 BPM.
  - At 100% FEEDBACK the repeats fade very slowly rather than building up forever.
  - Choosing HIGH-PASS with CUTOFF at 20 kHz (or LOW-PASS at 20 Hz) would silence the voice, so CUTOFF moves to where you can hear it (20 Hz for high-pass, 1 kHz for band-pass).
  - Turning a voice OFF stops its echoes too ("turns the harmony off completely", as the guide says).
- **Not the guide's full range:** SYNC uses PR7-V's usual note lengths (1/1 down to 1/16, with dotted and triplet ones), not the guide's 1/64 triplet to 1 bar.
- **Layout:** the HARMONY graph is a little narrower (196 rather than 212 points) so the voice row has room for FLATTEN with full-size knobs.

## 0.21.2

- **The SIGNAL CHAIN now matches the rest of the PR7 family** (as on the PR7-E and PR7-G). Every block is coloured by its place in the chain, along one red, hot pink and orange gradient: red at PITCH, hot pink in the middle (EQ, DOUBLER), orange at LO-FI. Before, each block had its own unrelated colour. Switched-off blocks still show their colour dimmed.
- **The edit panels match their blocks:** each panel's header icon and the bars above its controls use its block's colour.
  - The GRAPHIC EQ's faders and response curve take the EQ block's hot pink.
  - PR7-V's own panels follow too:
    - HARMONY's graph, voices and SINGING note take the PITCH block's red, while the NOTES tab's note voices stay blue so generated notes stand out.
    - MARS's header icon matches SATURATION, while its bands keep their own colours so you can tell them apart.
    - DOUBLER's voice numbers match its block.
  - The knob rings and buttons keep the plugin's orange.
- **Nothing changes in the sound.**

## 0.21.1

- **Fixed: PITCH wouldn't turn off once TUNE VOICE was on.** TUNE VOICE (HARMONY's pitch correction) works without HARMONY, but the PITCH stomp didn't switch it. With the stomp's light out, your voice stayed tuned and 40 ms late, so PITCH seemed stuck on.
- **The PITCH stomp now switches TUNE VOICE too,** with HARMONY and the octaver. Off means your plain voice, with no delay. Pressing it again brings back exactly what was on, TUNE VOICE in the mode it had (Natural, Hard or Notes). The stomp's light, its hover text and the PITCH block in the signal chain all show when TUNE VOICE is on.
- **Unchanged:** HARMONY's own switch in the panel still switches only the harmony voices. That way you can still use TUNE VOICE on its own, with no harmonies.

## 0.21.0

- **HARMONY stage 5: the Note Map Editor.** Inspired by Waves Harmony's Note Map Editor. Click **EDIT** beside CHORD on the NOTES tab to make your own map of what each note plays.
  - **CHORD** gains a 19th map, **Custom**: yours. The first time you click EDIT it starts as a copy of the map you were using, so you change it rather than starting from nothing.
  - **PLAYS:** pick a note (C to B), then click keys on the keyboard to add or remove what that note plays (a purple dot). Each note can play anything up to just under two octaves above or below, any number of notes. A sung note that's out of the key has its own row too, so it can have a different harmony.
  - **SPREAD / FOLD:** SPREAD (to start) moves the harmony with the octave you sing in. FOLD keeps the notes where you set them on the keyboard, whatever octave you sing.
  - **SCALE Custom** (a new scale): the SCALE row sets each note In, **>** (moved up to the next note in the scale) or **<** (moved down). It's kept relative to KEY, so it follows a key change. With any other scale, the row shows what that scale does.
  - **FROM CHORD** starts from any built-in map, and **ERASE** clears the map. A blue ring shows each note as it will sound once the scale is applied. Keys held on a MIDI keyboard light up pink.
  - Played MIDI keys set to **Through Chord** go through your map as well.
- **Our design where the guide doesn't say:** the range (±23 semitones from the note), and that FOLD moves a note an octave nearer when it's more than two octaves from what you sing. That's the most a harmony voice can shift, and past it the note would come out wrong.
- **Saved with your rig/project:** the map and SPREAD/FOLD. The Custom scale boxes are global, like KEY and SCALE, so changing rigs keeps your scale.
- **Not built yet:** per-note velocity and level in the map, and named, saved maps.
- **Also:** CHORD on the NOTES tab is a little narrower to make room for EDIT, so long map names are cut short in the box; the full names are in its list.

## 0.20.0

- **HARMONY stage 4: MIDI notes.** Inspired by Waves Harmony's MIDI keyboard input. Send MIDI to PR7-V (a MIDI track or keyboard routed to it in your DAW) and the keys you play become harmony voices made from your own voice.
- **MIDI**, on the NOTES tab, decides how:
  - **As Played** (to start): exactly the keys you hold, so a chord you play is the chord that's sung. Out-of-key keys stay as played.
  - **Through Chord:** each key goes through the CHORD map and into the KEY and SCALE, as a generated note does.
- **Velocity** sets each note's level: soft keys are quieter. The **sustain pedal** holds the notes.
- **Played and generated notes share the eight note voices.** A voice keeps its note while it's still wanted, so a held key doesn't restart when another is added.
- **TUNE VOICE: Notes** now follows the keyboard: your voice is tuned hard to the nearest held key (in whichever octave is nearest), the classic played tuned-vocal effect. With no keys held, it tunes to the generated note as before.
- **A keyboard strip** along the bottom of the NOTES graph shows the keys held (pink) and the generated note (blue). The graph is narrower on the NOTES tab, to make room for its controls.
- **Our design where the guide doesn't say:** As Played not correcting keys into the key, and the velocity curve (from 30% for the softest key to 100% for the hardest). MIDI works only with HARMONY on, except TUNE VOICE's Notes, which works either way.
- **Check your DAW:** to play harmonies from a keyboard, route a MIDI track or input to PR7-V. How you do that depends on the DAW.

## 0.19.0

- **GENERATE NOTES and CHORD maps were meant to be 0.18.0,** but another 0.18.0 (AEON, below) was published first, so they arrive in this one, along with AEON.
- **HARMONY stage 3: GENERATE NOTES and CHORD maps.** Inspired by Waves Harmony's Note Generator and Note Mapper. It's on the HARMONY panel's new **NOTES** tab, after voices 1-8.
- **GENERATE NOTES** turns your singing into steady notes:
  - A new note starts after a silence, or when your pitch moves more than **TOLERANCE** (cents) away for longer than **TIME**. Slides and quick embellishments don't make new notes.
  - **TOLERANCE Off:** only a silence starts a new note.
  - **MIN NOTE:** anything lower is ignored.
- **CHORD** turns each note into harmonies, always kept in your KEY and SCALE (a third stays a third). There are 18 maps:
  - **The same intervals on every note:** 3rd Above, 3rd Below, 5th Above, 3rd + 5th Above, 3rd Above + 4th Below, Octave Up, Octave Down, Octaves Up + Down, Power (5th + Octave), Three-Part Stack, Choir.
  - **A chord from each note:** Triad Below (you sing the top), Seventh Chord, Sus2, Sus4, Add9, Open Fifths, Gospel Stack.
- **Note voices** play their notes exactly, steady even if you drift a little. They come on top of the voices on the graph, which keep following you as before, and have their own **LEVEL**, **WIDTH** (low notes left, high right), **FORMANT**, **ATTACK** and **RELEASE**. They show on the graph in blue, with their note names, while they sound.
- **TUNE VOICE: Notes** tunes your voice hard to the generated note, so held notes stay dead steady (and, like Natural and Hard, the plugin runs 40 ms late while it's on).
- **New factory rig:** **Auto Choir** (E22): every note you sing becomes five, from an octave down to an octave up. Set KEY and SCALE first.
- **Our design where the guide differs or doesn't say:**
  - The 18 chord maps are ours.
  - Note voices have their own eight voices rather than sharing eight with the graph's voices.
  - TIME is in milliseconds (100 ms to start). Waves can sync it to the tempo and starts at a half note.
- **Not built yet:** the per-key Note Map Editor (your own chord maps) and syncing TIME to the tempo.

## 0.18.0

- **Everything in one build:** AEON, plus TUNE VOICE (0.17.0), MARS modulation, the vocal EQ presets and bank F (0.15.0 to 0.16.0).
- **AEON replaces the generic flanger:** a flanger modelled on the Boss BF-2, built stage by stage from its schematic with the real component values. It's the same model as in PR7-B 0.11.0, and the first in a plan to model a specific pedal for each modulation effect.
  - **The pedal's four knobs:** MANUAL (the delay time), DEPTH, RATE and RES (resonance). MIX is greyed out, because the pedal always mixes dry and wet 50/50, which makes the notches deep.
  - **The delay:** a bucket-brigade chip (BBD) model, 13 ms down to 1 ms across MANUAL, the pedal's specified range. The old flanger only reached 0.5 to 5.5 ms, so AEON sweeps much lower and deeper. At full DEPTH, MANUAL does nothing, as the pedal's manual says.
  - **The sweep:** a triangle LFO, from one cycle every 16 s to 10 per second. At fast RATE settings the sweep gets narrower, as on the pedal, where a capacitor smooths it.
  - **RES:** rings the peaks hardest in the treble, the BF-2's metallic "zing", without howling.
  - **Mono, like the pedal:** both sides get the same sound. The old flanger swept the left and right sides a quarter-cycle apart.
  - **Your sound:** rigs and projects that used the old flanger now get AEON, with MANUAL and RES at 5. Expect it to be louder (up to about 6 dB on the comb's peaks), because dry and wet add at full level.
  - **Two details are estimates,** because the schematic can't show them: how much signal the BBD chip and its output stage lose, and where the factory set the RES trimmer. Both are set so full RES rings hard without howling.
- **Jet Voice (E7) and Metallic Voice (E13)** now use AEON: RES well up for the whoosh, nearly full with a short delay for the metallic edge.

## 0.17.0

- **Pitch correction for your voice: TUNE VOICE (HARMONY stage 2).** Inspired by the CORRECT button in Waves Harmony. It's in the HARMONY panel's header (the PITCH stomp's panel) and tunes your own voice to the nearest note in the song's **KEY** and **SCALE**:
  - **Natural:** glides to the note, so vibrato, slides and expression mostly survive. A gentle tidy-up.
  - **Hard:** jumps straight to the note, turning slides into steps: the robotic tuned-vocal sound.
  - **Off:** as before.
- **Set the key first.** A note outside the KEY and SCALE goes to the nearest one inside it. If you don't know the key, choose SCALE **Chromatic**: every note counts, and the voice just goes to the nearest semitone.
- **It works with HARMONY off too,** so you can use it purely to tune your voice. With HARMONY on, the harmony voices stay in time with your tuned voice.
- **SINGING** now also shows the note it's tuning you to.
- **Your vowels stay yours:** it moves the pitch, not the voice's character.
- **The trade-off: a 40 ms delay while it's on.** Tuning needs a moment of your voice to work with. PR7-V tells your DAW, which lines the vocal back up on playback. Singing live through it, you'll hear yourself 40 ms late. With TUNE VOICE off there's no delay at all, as before.
- **Two new factory rigs:** **Tuned Lead** (A08, Natural) and **Hard Tune** (E21, the robotic sound).
- **Our design where the guide doesn't say:** the glide speeds (Natural about 60 ms, Hard about 3 ms) and how a note is held so a voice right between two notes doesn't flicker. Waves' NOTES mode (tuning to played or generated notes) comes with MIDI input and Generate Notes in a later stage.

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
