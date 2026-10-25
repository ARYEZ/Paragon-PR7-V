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

    // Choices, by their index in each list (see the effects' typeNames).
    constexpr float classicComp = 0, daybreakComp = 1, revival = 2;                     // DYNAMICS' TYPE
    constexpr float screamer = 1;                                                        // SATURATION's MODEL
    constexpr float chorus = 0, flanger = 1, phaser = 2, dimension = 4, rotary = 6,
                    harmonicTremolo = 8, autoPan = 9;                                    // MODULATION's TYPE
    constexpr float digitalDelay = 0, analogDelay = 1, tapeDelay = 2, reverseDelay = 4, shimmer = 6;   // DELAY's TYPE
    constexpr float syncOff = 0, quarter = 3, eighth = 6, dottedEighth = 7, sixteenth = 9;  // DELAY and MOD SYNC
    constexpr float warm = 1, roundAnalog = 2, hiss = 3, grit = 4;                           // DELAY's ANALOG
    constexpr float room = 1, hall = 2, cathedral = 3, plate = 4, spring = 5, digital = 6,
                    reverse = 7, chamber = 8;                                            // REVERB's TYPE
    constexpr float telephone = 0, amRadio = 1, megaphone = 2, cassette = 3, bitcrusher = 4;   // LO-FI's TYPE
    constexpr float pedalMorph = 3;                                                      // the expression pedal's MORPH FX
}

// The factory rigs, all for the voice (since 0.12.0; the guitar rigs PR7-V
// started with are gone). Five banks:
//
//   A  everyday vocals: polished, ready-to-sing starting points
//   B  rock and grit
//   C  delays and throws (the H-Delay-inspired controls)
//   D  spaces (the LXP-inspired reverb controls and CHAMBER)
//   E  character: doubles, harmonies, modulation and lo-fi voices
//
// Most start with a compressor and the de-esser, as a vocal chain would.
std::vector<Rig> makeFactoryRigs()
{
    using namespace ParamIDs;
    constexpr int A = 0, B = 1, C = 2, D = 3, E = 4;

    return {
        // ---- A: everyday vocals ------------------------------------------
        make ("Clean Slate", A, 1, "Clean", {}),

        // The all-rounder: levelled, de-essed, a little air, on a short plate.
        make ("Studio Vocal", A, 2, "Clean,Studio,Reverb", {
            { rigLevel, 2 },   // even with the other rigs
            { compOn, 1 }, { compType, revival }, { revivalCompression, -14 },
            { deessOn, 1 }, { deessFreq, 6500 }, { deessAmount, 6 },
            { eqOn, 1 }, { "eq250", -2 }, { "eq4k", 1.5f }, { "eq16k", 2 },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.4f }, { reverbMix, 0.18f }, { reverbPreDelay, 30 },
            { reverbRolloff, 10000 } }),

        // Modern pop: squeezed, bright, doubled, with a tucked dotted-eighth echo.
        make ("Pop Lead", A, 3, "Pop,Doubler,Delay", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -18 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq125", -2 }, { "eq500", -1 }, { "eq4k", 2 }, { "eq8k", 2 },
            { doublerOn, 1 }, { "dv1Gain", -8 }, { "dv2Gain", -8 },
            { delayOn, 1 }, { delaySync, dottedEighth }, { delayFeedback, 0.25f }, { delayMix, 0.14f }, { delayDuck, 0.6f },
            { delayLowCut, 300 }, { delayHighCut, 6000 },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.45f }, { reverbMix, 0.18f }, { reverbPreDelay, 40 } }),

        // Close and warm, for ballads: gentle compression, a small chamber.
        make ("Intimate", A, 4, "Ballad,Clean,Reverb", {
            { rigLevel, 4 },   // even with the other rigs
            { compOn, 1 }, { compType, classicComp }, { compThreshold, -22 }, { compRatio, 2.5f }, { compLevel, 4 },
            { deessOn, 1 }, { deessAmount, 5 },
            { eqOn, 1 }, { "eq250", 1 }, { "eq2k", 1 }, { "eq16k", 2 },
            { reverbOn, 1 }, { reverbType, chamber }, { reverbSize, 0.3f }, { reverbMix, 0.15f }, { reverbPreDelay, 10 } }),

        // Up-front and even, the way a voice sits on the radio.
        make ("Radio Ready", A, 5, "Pop,Studio", {
            { rigLevel, -6 },   // even with the other rigs
            { compOn, 1 }, { compType, revival }, { revivalCompression, -24 }, { revivalGain, -1 },
            { deessOn, 1 }, { deessFreq, 6500 }, { deessAmount, 8 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -3 }, { "eq2k", 2 }, { "eq4k", 3 },
            { presence, 2 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.3f }, { reverbMix, 0.1f }, { reverbDecay, 0.8f } }),

        // Talking, not singing: even, clear, the room noise gated down, dry.
        make ("Podcast Voice", A, 6, "Spoken,Clean", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -20 }, { revivalGate, -50 },
            { deessOn, 1 }, { deessAmount, 5 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq250", -1 }, { "eq4k", 1 } }),

        // ---- B: rock and grit ----------------------------------------------
        // A rock lead: a little Screamer edge, a tape slap and a plate.
        make ("Rock Lead", B, 1, "Rock,Drive,Delay", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -20 },
            { deessOn, 1 }, { deessAmount, 6 },
            { driveOn, 1 }, { driveModel, screamer }, { driveGain, 2.5f }, { driveTone, 6 }, { driveLevel, -4 },
            { eqOn, 1 }, { "eq1k", 2 },
            { delayOn, 1 }, { delayType, tapeDelay }, { delaySync, syncOff }, { delayTime, 120 }, { delayFeedback, 0.15f },
            { delayMix, 0.18f }, { delayAnalog, warm },
            { reverbOn, 1 }, { reverbType, plate }, { reverbMix, 0.15f } }),

        // Shouted and driven, mids pushed forward, a tight room.
        make ("Grit Shout", B, 2, "Rock,Drive", {
            { compOn, 1 }, { compType, daybreakComp }, { compAmount, 7 }, { compDetail, 4 },
            { driveOn, 1 }, { driveGain, 5 }, { driveTone, 5 }, { driveLevel, -1 },
            { eqOn, 1 }, { "eq125", -4 }, { "eq1k", 3 }, { "eq2k", 2 }, { "eq8k", -2 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.25f }, { reverbMix, 0.12f } }),

        // A bullhorn with a dirty slap: garage, punk and breakdowns.
        make ("Garage Megaphone", B, 3, "Rock,Lo-Fi,Drive", {
            { compOn, 1 }, { compType, daybreakComp }, { compAmount, 6 },
            { driveOn, 1 }, { driveGain, 3 }, { driveLevel, -4 },
            { lofiOn, 1 }, { lofiType, megaphone }, { lofiAmount, 6 }, { lofiNoise, 2 },
            { delayOn, 1 }, { delayType, tapeDelay }, { delaySync, syncOff }, { delayTime, 90 }, { delayFeedback, 0.2f },
            { delayMix, 0.2f }, { delayAnalog, grit } }),

        // Four loose takes stacked, a gang-vocal wall.
        make ("Gang Vocal", B, 4, "Rock,Doubler", {
            { rigLevel, 2 },   // even with the other rigs
            { compOn, 1 }, { compType, revival }, { revivalCompression, -18 },
            { driveOn, 1 }, { driveGain, 2 }, { driveLevel, -3 },
            { doublerOn, 1 }, { "dv3On", 1 }, { "dv4On", 1 },
            { "dv1Drift", 0.75f }, { "dv2Drift", 0.75f }, { "dv3Drift", 0.8f }, { "dv4Drift", 0.8f },
            { doublerOutput, -4 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.4f }, { reverbMix, 0.15f } }),

        // Big chorus vocal: doubled, wide echoes (1/4 left, dotted 1/8 right), a hall.
        make ("Arena Rock", B, 5, "Rock,Doubler,Delay,Reverb", {
            { rigLevel, -6 },   // even with the other rigs
            { compOn, 1 }, { compType, revival }, { revivalCompression, -20 },
            { deessOn, 1 }, { deessAmount, 7 },
            { doublerOn, 1 },
            { delayOn, 1 }, { delaySync, quarter }, { delayDual, 1 }, { delaySyncRight, dottedEighth },
            { delayFeedback, 0.3f }, { delayMix, 0.15f }, { delayDuck, 0.5f }, { delayHighCut, 5000 },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.6f }, { reverbMix, 0.22f }, { reverbPreDelay, 50 },
            { reverbDecay, 1.2f }, { reverbBassRt, 0.7f } }),

        // ---- C: delays and throws ------------------------------------------
        // The 50s slap: one quick tape repeat.
        make ("Slapback", C, 1, "Vintage,Delay", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -14 },
            { delayOn, 1 }, { delayType, tapeDelay }, { delaySync, syncOff }, { delayTime, 110 }, { delayFeedback, 0.1f },
            { delayMix, 0.3f }, { delayAnalog, warm },
            { reverbOn, 1 }, { reverbType, spring }, { reverbSize, 0.4f }, { reverbMix, 0.12f } }),

        // The pop throw: dotted eighths that duck under you and bloom in the gaps.
        make ("Dotted Throw", C, 2, "Pop,Delay", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -16 },
            { deessOn, 1 }, { deessAmount, 6 },
            { delayOn, 1 }, { delaySync, dottedEighth }, { delayFeedback, 0.4f }, { delayMix, 0.3f }, { delayDuck, 0.75f },
            { delayLowCut, 250 }, { delayHighCut, 5000 },
            { reverbOn, 1 }, { reverbType, plate }, { reverbMix, 0.15f } }),

        // Echoes bouncing left and right, softly swirling.
        make ("Ping-Pong Echo", C, 3, "Delay,Stereo", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -16 },
            { delayOn, 1 }, { delaySync, quarter }, { delayPingPong, 1 }, { delayFeedback, 0.45f }, { delayMix, 0.22f },
            { delayDuck, 0.5f }, { delayHighCut, 6000 }, { delayModDepth, 0.15f }, { delayModRate, 0.4f },
            { reverbOn, 1 }, { reverbType, hall }, { reverbMix, 0.15f } }),

        // Dub: long, dark, hissy tape repeats that keep going, into a spring.
        make ("Dub Echo", C, 4, "Reggae,Delay,Vintage", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -16 },
            { delayOn, 1 }, { delayType, tapeDelay }, { delaySync, quarter }, { delayFeedback, 0.8f }, { delayMix, 0.3f },
            { delayAnalog, hiss }, { delayLowCut, 200 }, { delayHighCut, 3000 },
            { reverbOn, 1 }, { reverbType, spring }, { reverbSize, 0.6f }, { reverbMix, 0.2f } }),

        // A wide stereo echo: different times on each side.
        make ("Wide Dual Echo", C, 5, "Delay,Stereo", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -16 },
            { delayOn, 1 }, { delaySync, quarter }, { delayDual, 1 }, { delaySyncRight, dottedEighth },
            { delayFeedback, 0.3f }, { delayMix, 0.22f }, { delayDuck, 0.6f }, { delayHighCut, 7000 },
            { reverbOn, 1 }, { reverbType, plate }, { reverbMix, 0.15f } }),

        // An early digital delay: grainy, gritty repeats.
        make ("Lo-Fi Echo", C, 6, "Lo-Fi,Delay", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -16 },
            { delayOn, 1 }, { delaySync, eighth }, { delayFeedback, 0.5f }, { delayMix, 0.25f }, { delayLoFi, 1 },
            { delayAnalog, grit }, { delayHighCut, 4000 },
            { reverbOn, 1 }, { reverbType, room }, { reverbMix, 0.12f } }),

        // Feedback past 100%: the echoes build into an endless swell. Stop
        // singing and it keeps going; switch the delay off to let it fade.
        make ("Endless Echo", C, 7, "Ambient,Delay,Experimental", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -16 },
            { delayOn, 1 }, { delaySync, quarter }, { delayFeedback, 1.15f }, { delayMix, 0.18f }, { delayDuck, 0.8f },
            { delayLowCut, 300 }, { delayHighCut, 4000 }, { delayAnalog, roundAnalog },
            { reverbOn, 1 }, { reverbType, hall }, { reverbMix, 0.25f } }),

        // Throws on the pedal: heel dry, toe into the echoes and the hall.
        make ("Pedal Throw", C, 8, "Delay,Expression,Live", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -16 },
            { delayOn, 1 }, { delaySync, eighth }, { delayFeedback, 0.45f }, { delayMix, 0.4f }, { delayHighCut, 6000 },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.6f }, { reverbMix, 0.35f },
            { expressionTarget, pedalMorph } }),

        // Each echo played backwards, sucking into the next word.
        make ("Backwards Echo", C, 9, "Psychedelic,Delay", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -16 },
            { delayOn, 1 }, { delayType, reverseDelay }, { delaySync, quarter }, { delayFeedback, 0.3f }, { delayMix, 0.3f },
            { reverbOn, 1 }, { reverbType, hall }, { reverbMix, 0.2f } }),

        // ---- D: spaces -------------------------------------------------------
        // The classic vocal plate, lows kept short and the sizzle rolled off.
        make ("Vocal Plate", D, 1, "Reverb,Studio", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -14 },
            { deessOn, 1 }, { deessAmount, 6 },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.5f }, { reverbMix, 0.25f }, { reverbPreDelay, 40 },
            { reverbBassRt, 0.8f }, { reverbRolloff, 9000 } }),

        // A concert hall: a long, warm tail behind a clear voice.
        make ("Big Hall", D, 2, "Reverb,Ballad", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -16 },
            { deessOn, 1 }, { deessAmount, 6 },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.7f }, { reverbMix, 0.3f }, { reverbPreDelay, 60 },
            { reverbDecay, 1.3f }, { reverbBassRt, 0.7f }, { reverbCrossover, 250 }, { reverbRolloff, 8000 } }),

        // The echo chamber: dense, smooth and short, the old studio sound.
        make ("Echo Chamber", D, 3, "Reverb,Vintage", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -14 },
            { reverbOn, 1 }, { reverbType, chamber }, { reverbSize, 0.55f }, { reverbMix, 0.25f }, { reverbPreDelay, 25 } }),

        // A stone cathedral: a long gap, then a huge tail.
        make ("Cathedral Choir", D, 4, "Reverb,Worship,Ambient", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -12 },
            { doublerOn, 1 }, { "dv1Gain", -10 }, { "dv2Gain", -10 },
            { reverbOn, 1 }, { reverbType, cathedral }, { reverbSize, 0.7f }, { reverbMix, 0.32f }, { reverbPreDelay, 80 },
            { reverbBassRt, 0.8f } }),

        // Worship wash: a shimmering octave-up echo into a long, moving hall.
        make ("Worship Wash", D, 5, "Worship,Ambient,Delay,Reverb", {
            { rigLevel, -3 },   // even with the other rigs
            { compOn, 1 }, { compType, revival }, { revivalCompression, -16 },
            { deessOn, 1 }, { deessAmount, 6 },
            { delayOn, 1 }, { delayType, shimmer }, { delaySync, quarter }, { delayFeedback, 0.5f }, { delayMix, 0.18f },
            { delayDuck, 0.6f },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.8f }, { reverbMix, 0.35f }, { reverbPreDelay, 40 },
            { reverbDecay, 1.4f }, { reverbSpin, 1.5f } }),

        // The 80s gated reverb: a burst that builds and stops dead.
        make ("Gated 80s", D, 6, "80s,Reverb,Experimental", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -16 },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.6f }, { reverbMix, 0.35f }, { reverbPreDelay, 0 },
            { reverbDecay, 0.2f }, { reverbSpread, 200 }, { reverbShape, 0.85f } }),

        // The reverse reverb: each word swells up out of nowhere.
        make ("Reverse Swell", D, 7, "Ambient,Reverb,Experimental", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -16 },
            { reverbOn, 1 }, { reverbType, reverse }, { reverbSize, 0.5f }, { reverbMix, 0.45f }, { reverbPreDelay, 0 } }),

        // A floating pad: a very long, lush, moving space with a shimmer on top.
        make ("Floating", D, 8, "Ambient,Reverb,Delay", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -16 },
            { delayOn, 1 }, { delayType, shimmer }, { delaySync, quarter }, { delayFeedback, 0.55f }, { delayMix, 0.2f },
            { delayPingPong, 1 }, { delayDuck, 0.5f },
            { reverbOn, 1 }, { reverbType, cathedral }, { reverbSize, 0.8f }, { reverbMix, 0.4f }, { reverbDecay, 1.6f },
            { reverbSpin, 2 }, { reverbWander, 2 }, { reverbDiffusion, 1.2f } }),

        // ---- E: character ----------------------------------------------------
        // The full doubler: four loose takes around you.
        make ("Big Double", E, 1, "Doubler,Pop", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -16 },
            { deessOn, 1 }, { deessAmount, 6 },
            { doublerOn, 1 }, { "dv3On", 1 }, { "dv4On", 1 },
            { "dv1Drift", 0.7f }, { "dv2Drift", 0.7f }, { "dv3Drift", 0.7f }, { "dv4Drift", 0.7f }, { doublerOutput, -3 },
            { reverbOn, 1 }, { reverbType, plate }, { reverbMix, 0.15f } }),

        // A double an octave down under you, for a deep, thick voice.
        make ("Octave Double", E, 2, "Doubler,Pitch", {
            { rigLevel, -3 },   // even with the other rigs
            { compOn, 1 }, { compType, revival }, { revivalCompression, -16 },
            { doublerOn, 1 }, { "dv3On", 1 }, { "dv3Octave", 1 }, { "dv3Gain", -8 }, { "dv3Pan", 0 }, { "dv3Detune", 0 },
            { reverbOn, 1 }, { reverbType, chamber }, { reverbMix, 0.18f } }),

        // Harmonies a third and a fifth above you, kept in the song's key
        // (set KEY and SCALE in the PITCH panel first).
        make ("Harmony Stack", E, 3, "Harmony,Pitch,Pop", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -16 },
            { deessOn, 1 }, { deessAmount, 6 },
            { harmonyOn, 1 }, { harmonyLevel, -3 },
            { reverbOn, 1 }, { reverbType, hall }, { reverbMix, 0.22f } }),

        // A lush, wide chorus on the voice, into a plate.
        make ("Dream Chorus", E, 4, "Modulation,Dream Pop", {
            { rigLevel, 2 },   // even with the other rigs
            { compOn, 1 }, { compType, revival }, { revivalCompression, -16 },
            { modOn, 1 }, { modType, dimension }, { modRate, 0.5f }, { modDepth, 0.5f }, { modMix, 0.4f },
            { delayOn, 1 }, { delaySync, dottedEighth }, { delayFeedback, 0.3f }, { delayMix, 0.15f }, { delayDuck, 0.5f },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.6f }, { reverbMix, 0.25f } }),

        // A rotating speaker on the voice: the 60s psychedelic sound.
        make ("Rotary Voice", E, 5, "Modulation,Vintage,Psychedelic", {
            { rigLevel, 5 },   // even with the other rigs
            { compOn, 1 }, { compType, revival }, { revivalCompression, -14 },
            { modOn, 1 }, { modType, rotary }, { modRate, 1.2f }, { modDepth, 0.6f }, { modMix, 0.6f },
            { reverbOn, 1 }, { reverbType, spring }, { reverbMix, 0.2f } }),

        // A slow phaser sweep and a soft echo.
        make ("Phaser Dream", E, 6, "Modulation,Psychedelic", {
            { rigLevel, 3 },   // even with the other rigs
            { compOn, 1 }, { compType, revival }, { revivalCompression, -14 },
            { modOn, 1 }, { modType, phaser }, { modRate, 0.3f }, { modDepth, 0.6f }, { modMix, 0.5f },
            { delayOn, 1 }, { delaySync, quarter }, { delayFeedback, 0.3f }, { delayMix, 0.15f },
            { reverbOn, 1 }, { reverbType, hall }, { reverbMix, 0.2f } }),

        // A jet flanger: the voice whooshing through it.
        make ("Jet Voice", E, 7, "Modulation,Experimental", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -16 },
            { modOn, 1 }, { modType, flanger }, { modRate, 0.2f }, { modDepth, 0.7f }, { modMix, 0.5f },
            { reverbOn, 1 }, { reverbType, plate }, { reverbMix, 0.15f } }),

        // The voice panning from side to side in time with the song.
        make ("Wide Pan", E, 8, "Modulation,Stereo", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -16 },
            { modOn, 1 }, { modType, autoPan }, { modSync, quarter }, { modDepth, 0.7f },
            { reverbOn, 1 }, { reverbType, hall }, { reverbMix, 0.2f } }),

        // A pulsing tremolo in time, into a spring: haunting, vintage.
        make ("Tremolo Ghost", E, 9, "Modulation,Vintage", {
            { rigLevel, 3 },   // even with the other rigs
            { compOn, 1 }, { compType, revival }, { revivalCompression, -14 },
            { modOn, 1 }, { modType, harmonicTremolo }, { modSync, eighth }, { modDepth, 0.6f },
            { reverbOn, 1 }, { reverbType, spring }, { reverbSize, 0.55f }, { reverbMix, 0.28f } }),

        // On the phone: a thin, boxy line for an intro or a verse.
        make ("Telephone", E, 10, "Lo-Fi", {
            { rigLevel, -6 },   // even with the other rigs
            { compOn, 1 }, { compType, revival }, { revivalCompression, -18 },
            { lofiOn, 1 }, { lofiType, telephone }, { lofiAmount, 6 }, { lofiNoise, 3 }, { lofiLevel, 3 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.3f }, { reverbMix, 0.1f } }),

        // An old valve radio: crackle, a narrow band, a spring.
        make ("Old Radio", E, 11, "Lo-Fi,Vintage", {
            { rigLevel, -5 },   // even with the other rigs
            { compOn, 1 }, { compType, revival }, { revivalCompression, -16 },
            { lofiOn, 1 }, { lofiType, amRadio }, { lofiAmount, 5 }, { lofiNoise, 5 }, { lofiLevel, 2 },
            { reverbOn, 1 }, { reverbType, spring }, { reverbSize, 0.5f }, { reverbMix, 0.25f } }),

        // A worn tape: wobble, hiss and a soft echo.
        make ("Cassette Memory", E, 12, "Lo-Fi,Dream Pop", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -16 },
            { modOn, 1 }, { modType, chorus }, { modRate, 0.35f }, { modDepth, 0.4f }, { modMix, 0.35f },
            { delayOn, 1 }, { delayType, tapeDelay }, { delaySync, dottedEighth }, { delayFeedback, 0.35f }, { delayMix, 0.2f },
            { delayAnalog, hiss },
            { reverbOn, 1 }, { reverbType, digital }, { reverbSize, 0.7f }, { reverbMix, 0.3f },
            { lofiOn, 1 }, { lofiType, cassette }, { lofiAmount, 5 }, { lofiNoise, 4 } }),

        // A metallic, robotic edge: a fast flanger, crushed bits and a tight echo.
        // (Not a vocoder: our own take with what's here.)
        make ("Metallic Voice", E, 13, "Lo-Fi,Experimental", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -18 },
            { modOn, 1 }, { modType, flanger }, { modRate, 0.1f }, { modDepth, 0.9f }, { modMix, 0.6f },
            { delayOn, 1 }, { delayType, analogDelay }, { delaySync, syncOff }, { delayTime, 25 }, { delayFeedback, 0.6f },
            { delayMix, 0.3f },
            { lofiOn, 1 }, { lofiType, bitcrusher }, { lofiAmount, 4 }, { lofiNoise, 0 }, { lofiMix, 0.6f } }),

        // Sixteenth-note echoes for rap and trap ad-libs, kept tight.
        make ("Trap Ad-Lib", E, 14, "Rap,Delay", {
            { rigLevel, -3 },   // even with the other rigs
            { compOn, 1 }, { compType, revival }, { revivalCompression, -20 },
            { deessOn, 1 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq125", -2 }, { "eq4k", 2 }, { "eq8k", 2 },
            { delayOn, 1 }, { delaySync, sixteenth }, { delayFeedback, 0.35f }, { delayMix, 0.2f }, { delayPingPong, 1 },
            { delayHighCut, 5000 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.35f }, { reverbMix, 0.12f } }),
    };
}
