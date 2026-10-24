#include "FactoryRigs.h"

#include "Parameters.h"

namespace
{
    Rig make (const juce::String& name, int bank, int number, const juce::String& tags,
              std::initializer_list<std::pair<const char*, float>> changes)
    {
        Rig rig;
        rig.name = name;
        rig.bank = bank;
        rig.number = number;
        rig.tags = juce::StringArray::fromTokens (tags, ",", {});
        // Only the changes from the defaults: loading a rig resets every
        // parameter it doesn't mention to its default (every effect off).
        for (const auto& [id, value] : changes)
            rig.parameters[id] = value;
        return rig;
    }

    // Delay sync choices (index into effects::Delay::syncNames).
    constexpr float syncOff = 0, quarter = 3, eighth = 6, dottedEighth = 7;
    // Modulation types (index into effects::Modulation::typeNames).
    constexpr float chorus = 0, flanger = 1, tremolo = 3, dimension = 4, uniVibe = 5, rotary = 6, vibrato = 7,
                    harmonicTremolo = 8, autoPan = 9;
    // FILTER / PITCH types.
    constexpr float autoWah = 0, pitchShifter = 1, whammyWah = 2, talkBoxType = 3;
    constexpr float pedalTalkBox = 8;   // the expression pedal's TALK BOX setting
    constexpr float daybreakComp = 1;   // COMP / GATE's TYPE
    constexpr float glitchSequence = 1, glitchTexturizer = 3, glitchSixteenth = 3;   // GLITCH's MODE and LENGTH
    constexpr float pedalGlitch = 7;   // the expression pedal's GLITCH setting
    constexpr float pedalWah = 2;      // the expression pedal's WAH / FILTER setting
    constexpr float glitchKillswitch = 4;
    constexpr int E = 4;               // bank E: rigs inspired by particular songs
    constexpr float muffFuzz = 2, faceFuzz = 3;   // OD / DISTORTION's MODEL
    constexpr float digitalDelay = 0;
    constexpr float xpCryWah = 1, xpUpOctave = 12, xpOctDownOctUp = 20, xpUp4thUp5th = 23;   // Whammy-Wah programs 02, 13, 21 and 24
    constexpr float pedalWhammyWah = 6;   // the expression pedal's WHAMMY-WAH setting
    // Delay types (index into effects::Delay::typeNames).
    constexpr float analogDelay = 1, tapeDelay = 2, modDelay = 3, reverseDelay = 4, multiTap = 5, shimmer = 6, ducking = 7;
    // Reverb types (index into effects::Reverb::typeNames).
    constexpr float room = 1, hall = 2, cathedral = 3, plate = 4, spring = 5, digital = 6, reverse = 7;
    // LO-FI types.
    constexpr float telephone = 0, amRadio = 1, megaphone = 2, cassette = 3, bitcrusher = 4;
}

std::vector<Rig> makeFactoryRigs()
{
    using namespace ParamIDs;
    constexpr int A = 0, B = 1, C = 2, D = 3;

    return {
        make ("Clean Slate", A, 1, "Clean", {}),

        make ("Studio Clean", A, 2, "Clean", {
            { compOn, 1 }, { compThreshold, -24 }, { compRatio, 3 }, { compLevel, 3 },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.4f }, { reverbMix, 0.2f } }),

        make ("Chorus Clean", A, 3, "Clean,Modulation", {
            { compOn, 1 }, { compThreshold, -22 }, { compRatio, 3 }, { compLevel, 2 },
            { modOn, 1 }, { modType, chorus }, { modRate, 0.6f }, { modDepth, 0.5f }, { modMix, 0.5f },
            { reverbOn, 1 }, { reverbType, plate }, { reverbMix, 0.25f } }),

        make ("Edge of Breakup", A, 4, "Crunch,Drive", {
            { driveOn, 1 }, { driveGain, 2 }, { driveTone, 6 }, { driveLevel, 4 },
            { reverbOn, 1 }, { reverbType, room }, { reverbMix, 0.2f } }),

        // Daybreak on a clean: smooth, even and sustaining, with half the dry
        // guitar mixed back in so every pick attack still speaks.
        make ("Daybreak Clean", A, 5, "Clean,Compressor", {
            { compOn, 1 }, { compType, daybreakComp }, { compAmount, 6 }, { compDetail, 6 }, { compMix, 0.5f }, { compLevel, 1 },
            { modOn, 1 }, { modType, chorus }, { modRate, 0.4f }, { modDepth, 0.3f }, { modMix, 0.25f },
            { reverbOn, 1 }, { reverbType, plate }, { reverbMix, 0.2f } }),

        make ("Lead Boost", B, 1, "Lead,Drive", {
            { compThreshold, -20 }, { gateThreshold, -60 }, { compOn, 1 }, { compRatio, 2 },
            { driveOn, 1 }, { driveGain, 4 }, { driveTone, 6 }, { driveLevel, 6 },
            { delayOn, 1 }, { delayType, analogDelay }, { delaySync, dottedEighth }, { delayFeedback, 0.3f }, { delayMix, 0.2f },
            { reverbOn, 1 }, { reverbType, plate }, { reverbMix, 0.25f }, { rigLevel, 2 } }),

        // A low-gain overdrive in front of a high-gain amp tightens the bass
        // and sharpens the attack: the classic metal "boost" trick.
        make ("High Gain Tight", B, 2, "High Gain,Rhythm", {
            { compOn, 1 }, { compThreshold, 0 }, { gateThreshold, -55 },
            { driveOn, 1 }, { driveGain, 1 }, { driveTone, 6 }, { driveLevel, 8 },
            { presence, 2 } }),

        make ("Ambient Swells", C, 1, "Ambient,Clean", {
            { compOn, 1 }, { compThreshold, -26 }, { compRatio, 4 }, { compLevel, 4 },
            { modOn, 1 }, { modType, chorus }, { modRate, 0.25f }, { modDepth, 0.6f }, { modMix, 0.35f },
            { delayOn, 1 }, { delayType, shimmer }, { delaySync, quarter }, { delayFeedback, 0.6f }, { delayMix, 0.35f },
            { delayTone, 5 }, { delayPingPong, 1 },
            { reverbOn, 1 }, { reverbType, cathedral }, { reverbSize, 0.9f }, { reverbTone, 5 }, { reverbMix, 0.45f } }),

        make ("Slapback", C, 2, "Rockabilly,Delay", {
            { delayOn, 1 }, { delayType, tapeDelay }, { delaySync, syncOff }, { delayTime, 110 }, { delayFeedback, 0.1f },
            { delayMix, 0.3f }, { delayTone, 5 },
            { reverbOn, 1 }, { reverbType, spring }, { reverbSize, 0.25f }, { reverbMix, 0.15f } }),

        make ("Surf Tremolo", C, 3, "Surf,Modulation", {
            { modOn, 1 }, { modType, tremolo }, { modRate, 6 }, { modDepth, 0.6f },
            { reverbOn, 1 }, { reverbType, spring }, { reverbSize, 0.7f }, { reverbTone, 7 }, { reverbMix, 0.4f } }),

        make ("Jet Flanger", D, 1, "Modulation,Drive", {
            { driveOn, 1 }, { driveGain, 3 },
            { modOn, 1 }, { modType, flanger }, { modRate, 0.2f }, { modDepth, 0.8f }, { modMix, 0.5f } }),

        make ("Leslie Spin", D, 2, "Modulation,Clean", {
            { compOn, 1 }, { compThreshold, -20 }, { compRatio, 3 },
            { modOn, 1 }, { modType, rotary }, { modRate, 6.5f }, { modDepth, 0.7f }, { modMix, 0.85f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.4f }, { reverbMix, 0.2f } }),

        make ("Machine Gun Vibe", D, 3, "Modulation,Drive,Lead", {
            { driveOn, 1 }, { driveGain, 6 }, { driveTone, 5 }, { driveLevel, 3 },
            { modOn, 1 }, { modType, uniVibe }, { modRate, 1.2f }, { modDepth, 0.75f }, { modMix, 0.5f },
            { reverbOn, 1 }, { reverbMix, 0.18f } }),

        make ("Dimension Clean", D, 4, "Clean,Modulation", {
            { compOn, 1 }, { compThreshold, -22 }, { compRatio, 3 }, { compLevel, 2 },
            { modOn, 1 }, { modType, dimension }, { modRate, 0.4f }, { modDepth, 0.5f }, { modMix, 0.5f },
            { delayOn, 1 }, { delayType, modDelay }, { delaySync, dottedEighth }, { delayFeedback, 0.25f }, { delayMix, 0.15f },
            { reverbOn, 1 }, { reverbSize, 0.6f }, { reverbMix, 0.25f } }),

        make ("Warble Vibrato", D, 5, "Modulation,Lo-Fi", {
            { modOn, 1 }, { modType, vibrato }, { modRate, 5.5f }, { modDepth, 0.35f },
            { reverbOn, 1 }, { reverbType, spring }, { reverbSize, 0.5f }, { reverbTone, 4 }, { reverbMix, 0.3f } }),

        make ("Octave Lead", B, 3, "Lead,Pitch,Drive", {
            { compOn, 1 }, { compThreshold, -20 }, { compRatio, 3 },
            { fpOn, 1 }, { fpType, pitchShifter }, { fpInterval, 12 }, { fpMix, 0.45f },
            { driveOn, 1 }, { driveGain, 5 }, { driveTone, 6 }, { driveLevel, 2 },
            { delayOn, 1 }, { delayType, ducking }, { delaySync, dottedEighth }, { delayFeedback, 0.3f }, { delayMix, 0.2f },
            { reverbOn, 1 }, { reverbMix, 0.2f } }),

        make ("Harmony Fifths", B, 4, "Lead,Pitch", {
            { fpOn, 1 }, { fpType, pitchShifter }, { fpInterval, 7 }, { fpMix, 0.4f },
            { driveOn, 1 }, { driveGain, 4 }, { driveTone, 5 },
            { reverbOn, 1 }, { reverbSize, 0.6f }, { reverbMix, 0.25f } }),

        make ("Funk Envelope", C, 6, "Funk,Filter,Clean", {
            { compOn, 1 }, { compThreshold, -26 }, { compRatio, 4 }, { compLevel, 4 },
            { fpOn, 1 }, { fpType, autoWah }, { fpSensitivity, 6 }, { fpResonance, 6 }, { fpMix, 1 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.3f }, { reverbMix, 0.15f } }),

        make ("Wide Doubler", D, 6, "Rhythm,Modulation", {
            { driveOn, 1 }, { driveGain, 3 }, { driveTone, 5 },
            { doublerOn, 1 }, { doublerRate, 0.8f }, { doublerDepth, 0.4f }, { doublerMix, 0.7f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.35f }, { reverbMix, 0.15f } }),

        // LO-FI: for intros and breakdowns. Assign the expression pedal to
        // LO-FI to sweep from the full band (heel) into the effect (toe).
        make ("Whammy Lead", B, 7, "Lead,Pitch,Drive,Expression", {
            { fpOn, 1 }, { fpType, whammyWah }, { fpXpProgram, xpUpOctave }, { fpMix, 1 },
            { expressionTarget, pedalWhammyWah },
            { driveOn, 1 }, { driveGain, 6 }, { driveTone, 6 }, { driveLevel, 3 },
            { delayOn, 1 }, { delaySync, dottedEighth }, { delayFeedback, 0.25f }, { delayMix, 0.18f },
            { reverbOn, 1 }, { reverbMix, 0.2f } }),

        make ("XP Wah Crunch", B, 8, "Wah,Drive,Expression", {
            { fpOn, 1 }, { fpType, whammyWah }, { fpXpProgram, xpCryWah }, { fpMix, 1 },
            { expressionTarget, pedalWhammyWah },
            { driveOn, 1 }, { driveGain, 4 }, { driveTone, 5 }, { driveLevel, 4 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.3f }, { reverbMix, 0.15f } }),

        make ("Harmony Bends", B, 9, "Lead,Pitch,Expression", {
            { fpOn, 1 }, { fpType, whammyWah }, { fpXpProgram, xpUp4thUp5th }, { fpMix, 0.8f },
            { expressionTarget, pedalWhammyWah },
            { driveOn, 1 }, { driveGain, 5 }, { driveTone, 6 },
            { delayOn, 1 }, { delaySync, quarter }, { delayFeedback, 0.3f }, { delayMix, 0.2f },
            { reverbOn, 1 }, { reverbSize, 0.6f }, { reverbMix, 0.25f } }),

        // The late-'90s nu-metal screech: the XP-100's program 21 (an octave
        // down and an octave up, blended with the guitar), rocked while a
        // high note rings, into a tight Screamer boost and a high-gain amp
        // (load a Rectifier-style capture). The EQ is the Tight Low End
        // preset, so the octave-down voice doesn't turn to mud.
        make ("Nu-Metal Screech", B, 10, "High Gain,Pitch,Expression", {
            { compOn, 1 }, { compThreshold, 0 }, { gateThreshold, -55 },
            { fpOn, 1 }, { fpType, whammyWah }, { fpXpProgram, xpOctDownOctUp }, { fpMix, 0.85f },
            { expressionTarget, pedalWhammyWah },
            { driveOn, 1 }, { driveGain, 1 }, { driveTone, 6 }, { driveLevel, 8 },
            { presence, 2 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -4 }, { "eq250", -1 }, { "eq1k", 1 }, { "eq2k", 2 }, { "eq4k", 2 }, { "eq8k", 1 },
            { eqLevel, -0.5f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.25f }, { reverbMix, 0.1f } }),

        // Tempo-locked stutters on a crunch, faded in with the pedal: rock
        // forward on a transition or the end of a riff.
        make ("Stutter Edit", C, 10, "Glitch,Drive,Expression", {
            { glitchOn, 1 }, { glitchMode, glitchSequence }, { glitchSync, 1 }, { glitchDivision, glitchSixteenth },
            { glitchDensity, 6 }, { glitchChaos, 2 }, { glitchMix, 1 },
            { expressionTarget, pedalGlitch },
            { driveOn, 1 }, { driveGain, 5 }, { driveTone, 6 }, { driveLevel, 4 },
            { reverbOn, 1 }, { reverbType, room }, { reverbMix, 0.12f } }),

        // A slow cloud of grains under a clean tone: chords smear into a pad.
        make ("Grain Cloud", D, 10, "Glitch,Ambient,Clean", {
            { compOn, 1 }, { compThreshold, -24 }, { compRatio, 3 },
            { glitchOn, 1 }, { glitchMode, glitchTexturizer }, { glitchSync, 0 }, { glitchTime, 250 },
            { glitchDensity, 4 }, { glitchChaos, 5 }, { glitchMix, 0.45f },
            { delayOn, 1 }, { delaySync, quarter }, { delayFeedback, 0.4f }, { delayMix, 0.25f },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.8f }, { reverbMix, 0.35f } }),

        //======================================================================
        // Bank E: inspired by particular songs. Our settings for the sound,
        // not the artists' own (which are their own gear, played their way).

        // Rage Against the Machine, "Killing in the Name" solo: a Whammy an
        // octave up, rocked to the toe for the squeal, into a cranked crunch
        // with no reverb. (For a higher squeal, PROGRAM 14 Up 2 Octaves.)
        make ("Killing Solo (RATM)", E, 1, "Song,Lead,Pitch,Expression", {
            { fpOn, 1 }, { fpType, whammyWah }, { fpXpProgram, xpUpOctave }, { fpMix, 1 },
            { expressionTarget, pedalWhammyWah },
            { driveOn, 1 }, { driveGain, 3 }, { driveTone, 6 }, { driveLevel, 6 },
            { presence, 1 } }),

        // Rage Against the Machine, "Bulls on Parade" scratch: a wah on the
        // pedal, rocked as you rub the muted strings over the pickup, and
        // the toggle-switch chop (GLITCH Killswitch, 1/16, on the song's
        // grid when the DAW plays).
        make ("Bulls Scratch (RATM)", E, 2, "Song,Wah,Glitch,Expression", {
            { expressionTarget, pedalWah },
            { glitchOn, 1 }, { glitchMode, glitchKillswitch }, { glitchSync, 1 }, { glitchDivision, glitchSixteenth },
            { glitchDensity, 5 }, { glitchChaos, 0 }, { glitchMix, 1 },
            { driveOn, 1 }, { driveGain, 4 }, { driveTone, 6 }, { driveLevel, 5 },
            { presence, 1 } }),

        // Pink Floyd. David Gilmour's sounds as his gear guides describe them;
        // the rest is in his hands (vibrato, bends, note choice).

        // "Comfortably Numb" solos: a Big Muff-style fuzz (sustain about 70%,
        // tone 30-40%) into a delay of about 370 ms, through a subtly mixed
        // rotating speaker (his Yamaha RA-200), with studio reverb.
        make ("Numb Solo (Floyd)", E, 3, "Song,Lead,Fuzz", {
            { driveOn, 1 }, { driveModel, muffFuzz }, { driveGain, 7 }, { driveTone, 3.5f }, { driveLevel, 1 },
            { modOn, 1 }, { modType, rotary }, { modRate, 0.9f }, { modDepth, 0.5f }, { modMix, 0.3f },
            { delayOn, 1 }, { delayType, digitalDelay }, { delaySync, syncOff }, { delayTime, 370 }, { delayFeedback, 0.3f }, { delayMix, 0.25f },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.6f }, { reverbMix, 0.18f } }),

        // "Us and Them": the neck pickup, clean, through a slow rotating
        // speaker (a Leslie dominated it; a Uni-Vibe is close too), with echo.
        // For the louder chorus, stomp footswitch 2: a Fuzz Face-style fuzz
        // is set up and waiting, as Gilmour kicked his in.
        make ("Us and Them (Floyd)", E, 4, "Song,Clean,Modulation,Fuzz", {
            { driveOn, 0 }, { driveModel, faceFuzz }, { driveGain, 6 }, { driveTone, 6 }, { driveLevel, 0 },
            { modOn, 1 }, { modType, rotary }, { modRate, 0.7f }, { modDepth, 0.6f }, { modMix, 0.6f },
            { delayOn, 1 }, { delayType, tapeDelay }, { delaySync, syncOff }, { delayTime, 420 }, { delayFeedback, 0.25f }, { delayMix, 0.2f },
            { reverbOn, 1 }, { reverbType, plate }, { reverbMix, 0.25f } }),

        // "Run Like Hell": muted eighths into a long, clean delay (about
        // 380 ms, five or six repeats) that makes the gallop, with flanging
        // and light compression.
        make ("Run Like Hell (Floyd)", E, 5, "Song,Clean,Delay,Modulation", {
            { compOn, 1 }, { compThreshold, -22 }, { compRatio, 3 }, { compLevel, 2 },
            { modOn, 1 }, { modType, flanger }, { modRate, 0.3f }, { modDepth, 0.5f }, { modMix, 0.4f },
            { delayOn, 1 }, { delayType, digitalDelay }, { delaySync, syncOff }, { delayTime, 380 }, { delayFeedback, 0.55f }, { delayMix, 0.3f },
            { reverbOn, 1 }, { reverbType, room }, { reverbMix, 0.12f } }),

        // "Money" solos: the bridge pickup into a Fuzz Face-style fuzz, with
        // a multi-head echo in the spirit of his Binson Echorec.
        make ("Money Solo (Floyd)", E, 6, "Song,Lead,Fuzz,Delay", {
            { driveOn, 1 }, { driveModel, faceFuzz }, { driveGain, 7 }, { driveTone, 7 }, { driveLevel, 1 },
            { delayOn, 1 }, { delayType, multiTap }, { delaySync, syncOff }, { delayTime, 330 }, { delayFeedback, 0.3f }, { delayMix, 0.22f },
            { reverbOn, 1 }, { reverbType, room }, { reverbMix, 0.15f } }),

        // Tool, "Jambi" solo: a talk box on a tight, heavy tone. Load a
        // high-gain amp capture; the pedal opens the "mouth" from oo to ee.
        make ("Jambi Talk Box (Tool)", E, 7, "Song,Lead,Talk Box,Expression", {
            { fpOn, 1 }, { fpType, talkBoxType }, { fpResonance, 6 }, { fpMix, 1 },
            { expressionTarget, pedalTalkBox },
            { driveOn, 1 }, { driveGain, 3 }, { driveTone, 6 }, { driveLevel, 6 },
            { delayOn, 1 }, { delayType, digitalDelay }, { delaySync, eighth }, { delayFeedback, 0.2f }, { delayMix, 0.12f },
            { reverbOn, 1 }, { reverbType, room }, { reverbMix, 0.1f } }),

        // Peter Frampton, "Show Me the Way": the talk box on a warm, lightly
        // driven tone, the most famous talk box sound there is.
        make ("Show Me the Way (Frampton)", E, 8, "Song,Lead,Talk Box,Expression", {
            { fpOn, 1 }, { fpType, talkBoxType }, { fpResonance, 5 }, { fpMix, 1 },
            { expressionTarget, pedalTalkBox },
            { driveOn, 1 }, { driveModel, 0 }, { driveGain, 3 }, { driveTone, 5 }, { driveLevel, 2 },
            { reverbOn, 1 }, { reverbType, plate }, { reverbMix, 0.2f } }),

        make ("Phone Intro", B, 5, "Lo-Fi,Clean", {
            { compOn, 1 }, { compThreshold, -22 }, { compRatio, 3 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.35f }, { reverbMix, 0.2f },
            { lofiOn, 1 }, { lofiType, telephone }, { lofiAmount, 6 }, { lofiNoise, 3 }, { lofiLevel, 2 } }),

        make ("Megaphone Verse", B, 6, "Lo-Fi,Drive", {
            { driveOn, 1 }, { driveGain, 5 }, { driveTone, 6 },
            { lofiOn, 1 }, { lofiType, megaphone }, { lofiAmount, 6 }, { lofiNoise, 2 } }),

        make ("Old Radio", C, 7, "Lo-Fi,Vintage", {
            { modOn, 1 }, { modType, tremolo }, { modRate, 4.5f }, { modDepth, 0.3f },
            { reverbOn, 1 }, { reverbType, spring }, { reverbSize, 0.5f }, { reverbMix, 0.3f },
            { lofiOn, 1 }, { lofiType, amRadio }, { lofiAmount, 5 }, { lofiNoise, 5 }, { lofiLevel, 1 } }),

        make ("Cassette Dreams", D, 7, "Lo-Fi,Ambient", {
            { compOn, 1 }, { compThreshold, -24 }, { compRatio, 3 },
            { modOn, 1 }, { modType, chorus }, { modRate, 0.4f }, { modDepth, 0.4f }, { modMix, 0.35f },
            { delayOn, 1 }, { delayType, tapeDelay }, { delaySync, dottedEighth }, { delayFeedback, 0.35f }, { delayMix, 0.25f },
            { reverbOn, 1 }, { reverbType, digital }, { reverbSize, 0.8f }, { reverbMix, 0.35f },
            { lofiOn, 1 }, { lofiType, cassette }, { lofiAmount, 5 }, { lofiNoise, 4 } }),

        make ("8-Bit Crunch", D, 8, "Lo-Fi,Drive", {
            { driveOn, 1 }, { driveGain, 3 },
            { lofiOn, 1 }, { lofiType, bitcrusher }, { lofiAmount, 6 }, { lofiNoise, 0 }, { lofiMix, 0.8f } }),

        // A reverse reverb: each note swells up out of nowhere.
        make ("Reverse Swell", C, 8, "Ambient,Reverb", {
            { compOn, 1 }, { compThreshold, -24 }, { compRatio, 3 },
            { delayOn, 1 }, { delaySync, quarter }, { delayFeedback, 0.3f }, { delayMix, 0.15f },
            { reverbOn, 1 }, { reverbType, reverse }, { reverbSize, 0.5f }, { reverbMix, 0.55f }, { reverbPreDelay, 0 } }),

        // Delays to try: a rising shimmer pad, and reverse echoes.
        make ("Shimmer Pad", D, 9, "Ambient,Delay,Clean", {
            { compOn, 1 }, { compThreshold, -26 }, { compRatio, 4 }, { compLevel, 3 },
            { delayOn, 1 }, { delayType, shimmer }, { delaySync, quarter }, { delayFeedback, 0.65f }, { delayMix, 0.4f },
            { delayTone, 6 }, { delayPingPong, 1 },
            { reverbOn, 1 }, { reverbType, cathedral }, { reverbSize, 0.7f }, { reverbMix, 0.4f } }),

        make ("Backwards Echo", C, 9, "Psychedelic,Delay", {
            { driveOn, 1 }, { driveGain, 3 },
            { delayOn, 1 }, { delayType, reverseDelay }, { delaySync, eighth }, { delayFeedback, 0.4f }, { delayMix, 0.5f },
            { reverbOn, 1 }, { reverbType, plate }, { reverbMix, 0.2f } }),

        // These two follow the tempo (tapped, or the DAW's).
        make ("Brownface Trem", C, 4, "Modulation,Clean,Vintage", {
            { modOn, 1 }, { modType, harmonicTremolo }, { modSync, eighth }, { modDepth, 0.7f },
            { reverbOn, 1 }, { reverbType, spring }, { reverbSize, 0.55f }, { reverbTone, 6 }, { reverbMix, 0.3f } }),

        make ("Wide Pan", C, 5, "Modulation,Ambient", {
            { compOn, 1 }, { compThreshold, -24 }, { compRatio, 3 },
            { modOn, 1 }, { modType, autoPan }, { modSync, quarter }, { modDepth, 0.8f },
            { delayOn, 1 }, { delayType, multiTap }, { delaySync, dottedEighth }, { delayFeedback, 0.35f }, { delayMix, 0.2f },
            { reverbOn, 1 }, { reverbType, cathedral }, { reverbSize, 0.8f }, { reverbMix, 0.35f } }),
    };
}
