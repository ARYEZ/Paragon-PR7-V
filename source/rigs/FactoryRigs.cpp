#include "FactoryRigs.h"

#include "Parameters.h"
#include "dsp/effects/HarmonyModulation.h"

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

    // HARMONY's modulators on controls (stage 9-10): each in the control's
    // next free slot, at its depth (a pitch's in 24ths: 1/24 is a semitone).
    struct ModOn { const char* target; int source; float depth; };
    Rig withMods (Rig rig, std::initializer_list<ModOn> mods)
    {
        for (const auto& [target, source, depth] : mods)
        {
            auto& slots = rig.harmonyModSlots[target];
            for (auto& slot : slots)
                if (slot.source == 0)
                {
                    slot = { source, depth };
                    break;
                }
        }
        return rig;
    }
    // A drawn shape (SHAPE Drawn) for modulator `mod` (0 = M1).
    Rig withShape (Rig rig, int mod, const std::array<float, 32>& points)
    {
        rig.harmonyModShapes[(size_t) mod] = points;
        return rig;
    }
    using HMod = effects::HarmonyModulation;
    // M1-M4's choices, by index.
    constexpr float lfo = 0, seq = 1;                                                    // TYPE
    constexpr float sineShape = 0, smoothRandomShape = 8, drawnShape = HMod::drawnShape; // SHAPE
    constexpr float syncTrigger = HMod::sync, polyTrigger = HMod::poly;                  // TRIGGER
    constexpr float modOneBar = 4, modEighth = 10, modSixteenth = 13;                    // RATE SYNC
    constexpr float outward = 0;                                                         // SP MODE
    constexpr float threePartStack = 9, gospelStack = 17;                                // more CHORD maps
    constexpr float thirdAndFifth = 3, octaveUpMap = 5;                                  // and two more

    // Choices, by their index in each list (see the effects' typeNames).
    constexpr float classicComp = 0, daybreakComp = 1, revival = 2, photonComp = 3, abyssComp = 4;   // DYNAMICS' TYPE
    constexpr float photonEight = 1, photonAll = 4;                                         // PHOTON's RATIO
    constexpr float chorus = 0, flanger = 1, phaser = 2, dimension = 4, rotary = 6,
                    harmonicTremolo = 8, autoPan = 9;                                    // MODULATION's TYPE
    constexpr float digitalDelay = 0, analogDelay = 1, tapeDelay = 2, reverseDelay = 4, shimmer = 6;   // DELAY's TYPE
    constexpr float syncOff = 0, quarter = 3, eighth = 6, dottedEighth = 7, sixteenth = 9;  // DELAY and MOD SYNC
    constexpr float warm = 1, roundAnalog = 2, hiss = 3, grit = 4;                           // DELAY's ANALOG
    constexpr float room = 1, hall = 2, cathedral = 3, plate = 4, spring = 5, digital = 6,
                    reverse = 7, chamber = 8;                                            // REVERB's TYPE
    constexpr float telephone = 0, amRadio = 1, megaphone = 2, cassette = 3, bitcrusher = 4,
                    cellPhone = 5, walkieTalkie = 6, gramophone = 7;                       // LO-FI's TYPE
    constexpr float pedalMorph = 3;                                                      // the expression pedal's MORPH FX
    // SATURATION (MARS)'s styles (index into effects::Mars::styleNames).
    constexpr float cleanTubeStyle = 0, warmTubeStyle = 1, saturatedTubeStyle = 2, brokenTubeStyle = 3, warmTapeStyle = 5,
                    saturatedTapeStyle = 6, americanAmpStyle = 11, britishAmpStyle = 12, crunchyAmpStyle = 14,
                    screamingAmpStyle = 15, destroyStyle = 20;
    constexpr float sequenceGlitch = 1, interfereGlitch = 2, texturizer = 3;             // GLITCH's MODE
    constexpr float thirtySecondGlitch = 1, sixteenthGlitch = 3;                         // GLITCH's LENGTH (sync)
    constexpr float naturalTune = 1, hardTune = 2;                                       // HARMONY's CORRECT (TUNE VOICE)
    constexpr float choirMap = 10;                                                       // HARMONY's CHORD map: Choir
    // MARS's modulation (effects::MarsModulation): sources, targets, XLFO SYNC.
    constexpr float xlfo1 = 1, envelopeFollower1 = 5, expressionPedal = 13;
    constexpr float band1Drive = 1, band1Level = 10, band2Drive = 11, crossover1 = 41;
    constexpr float twoBars = 4, oneBar = 5, eighthNote = 8;
}

// The factory rigs, all for the voice (since 0.12.0; the guitar rigs PR7-V
// started with are gone). Five banks:
//
//   A  everyday vocals: polished, ready-to-sing starting points
//   B  rock and grit
//   C  delays and throws (the H-Delay-inspired controls)
//   D  spaces (the LXP-inspired reverb controls and CHAMBER)
//   E  character: doubles, harmonies, modulation and lo-fi voices
//   F  signature styles: the vocal sounds of genres and eras (pop-punk,
//      metalcore, prog, grunge...), named for the style, not any artist
//   G  moving harmonies: HARMONY's modulators (0.33.0-0.34.0) at work, one
//      idea each; all but the last work from your voice alone
//   H  an album's vocal sounds (0.45.0): one rig per sound on one record,
//      alternating airy layered cleans and screams, named for the sound
//      (the release notes say which songs inspired them; nothing in the
//      plugin names an artist, album or song)
//   I  a second album's vocal sounds (0.47.0): modern progressive metal,
//      high stacked tenor cleans and tight double-tracked screams, all
//      layered with the DOUBLER (no HARMONY)
//   J  a third album's vocal sounds (0.48.0): early-2000s space rock,
//      filtered and far-away voices, shouted choruses, long spaces
//   K  a fourth album's vocal sounds (0.49.0): mid-2000s pop-rock turning
//      darker, belted doubled choruses, hushed verses, bratty chants
//   L  a fifth album's vocal sounds (0.50.0): nu-metal with two voices, a
//      rapper (glitch-edited verses) and a singer (soaring hooks, screams)
//   M  a sixth album's vocal sounds (0.51.0): theatrical metalcore with a
//      clean singer and a screamer, both stacked thick with the DOUBLER
//   N  a seventh album's vocal sounds (0.52.0): industrial nu-metal, a
//      whisper-to-scream voice treated like a machine
//   O  an eighth album's vocal sounds (0.53.0): late-90s industrial metal
//      with a dance-machine pulse, chanted shouts, screams, a deep second voice
//   P  a ninth album's vocal sounds (0.54.0): early-90s grunge, a deep
//      baritone snarled, crooned and shouted through a megaphone
//
// Most start with a compressor and the de-esser, as a vocal chain would.
std::vector<Rig> makeFactoryRigs()
{
    using namespace ParamIDs;
    constexpr int A = 0, B = 1, C = 2, D = 3, E = 4, F = 5, G = 6, H = 7, I = 8, J = 9, K = 10, L = 11, M = 12, N = 13, O = 14, P = 15;

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
            { compOn, 1 }, { compType, abyssComp }, { abyssPeakReduction, 40 }, { abyssGain, 8 },   // Classic until 0.41.0
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

        // MARS on one band of warm tape: the glue and warmth of a voice recorded to tape.
        make ("Tape Warmth", A, 7, "Studio,Saturation,Mars", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -14 },
            { deessOn, 1 }, { deessAmount, 6 },
            { driveOn, 1 }, { "mb1Style", warmTapeStyle }, { "mb1Drive", 0.45f }, { "mb1Treble", 1.5f },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.4f }, { reverbMix, 0.15f } }),

        // A polished pop lead, tuned gently (TUNE VOICE: Natural, in the
        // HARMONY panel). Set KEY and SCALE for the song first, or SCALE to
        // Chromatic for any song. Tuning makes the plugin run 40 ms late
        // (your DAW lines it back up).
        make ("Tuned Lead", A, 8, "Pop,Studio,Tuning", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -18 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 7 },
            { harmonyCorrect, naturalTune },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -3 }, { "eq250", -2 }, { "eq2k", 1 }, { "eq4k", 3 }, { "eq8k", 2 }, { "eq16k", 2 },
            { delayOn, 1 }, { delaySync, eighth }, { delayFeedback, 0.2f }, { delayMix, 0.1f }, { delayDuck, 0.6f },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.45f }, { reverbMix, 0.16f } }),

        // A smooth, even lead on ABYSS, the optical leveller (0.41.0): it rides
        // the voice gently, a little more on the bright esses (EMPHASIS).
        make ("Silky Lead", A, 9, "Pop,Ballad,Studio,Abyss", {
            { compOn, 1 }, { compType, abyssComp }, { abyssPeakReduction, 55 }, { abyssGain, 16 }, { abyssEmphasis, 0.3f },
            { deessOn, 1 }, { deessAmount, 5 },
            { eqOn, 1 }, { "eq250", -1.5f }, { "eq4k", 1 }, { "eq16k", 2 },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.5f }, { reverbMix, 0.2f }, { reverbPreDelay, 40 } }),

        // ---- B: rock and grit ----------------------------------------------
        // A rock lead: a little Screamer edge, a tape slap and a plate.
        make ("Rock Lead", B, 1, "Rock,Drive,Delay", {
            { rigLevel, -6 },   // even with the other rigs
            { compOn, 1 }, { compType, revival }, { revivalCompression, -20 },
            { deessOn, 1 }, { deessAmount, 6 },
            // Edge on the mids only (the lows kept clean), as a TS-style boost does.
            { driveOn, 1 }, { marsBands, 1 }, { marsCross1, 300 }, { "mb1Style", cleanTubeStyle }, { "mb1Drive", 0.1f },
            { "mb2Style", saturatedTubeStyle }, { "mb2Drive", 0.45f }, { "mb2Mid", 2 }, { "mb2Treble", 1 },
            { eqOn, 1 }, { "eq1k", 2 },
            { delayOn, 1 }, { delayType, tapeDelay }, { delaySync, syncOff }, { delayTime, 120 }, { delayFeedback, 0.15f },
            { delayMix, 0.18f }, { delayAnalog, warm },
            { reverbOn, 1 }, { reverbType, plate }, { reverbMix, 0.15f } }),

        // Shouted and driven, mids pushed forward, a tight room.
        make ("Grit Shout", B, 2, "Rock,Drive", {
            { rigLevel, -3 },   // even with the other rigs
            { compOn, 1 }, { compType, photonComp }, { photonInput, 24 }, { photonOutput, -17 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, 0 },   // Daybreak until 0.42.0
            { driveOn, 1 }, { "mb1Style", britishAmpStyle }, { "mb1Drive", 0.6f }, { "mb1Presence", 2 }, { "mb1Mix", 0.85f },
            { eqOn, 1 }, { "eq125", -4 }, { "eq1k", 3 }, { "eq2k", 2 }, { "eq8k", -2 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.25f }, { reverbMix, 0.12f } }),

        // A bullhorn with a dirty slap: garage, punk and breakdowns.
        make ("Garage Megaphone", B, 3, "Rock,Lo-Fi,Drive", {
            { rigLevel, -3 },   // even with the other rigs
            { compOn, 1 }, { compType, photonComp }, { photonInput, 22 }, { photonOutput, -16 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, 0 },   // Daybreak until 0.42.0
            { driveOn, 1 }, { "mb1Style", crunchyAmpStyle }, { "mb1Drive", 0.5f },
            { lofiOn, 1 }, { lofiType, megaphone }, { lofiAmount, 6 }, { lofiNoise, 2 },
            { delayOn, 1 }, { delayType, tapeDelay }, { delaySync, syncOff }, { delayTime, 90 }, { delayFeedback, 0.2f },
            { delayMix, 0.2f }, { delayAnalog, grit } }),

        // Four loose takes stacked, a gang-vocal wall.
        make ("Gang Vocal", B, 4, "Rock,Doubler", {
            { rigLevel, 2 },   // even with the other rigs
            { compOn, 1 }, { compType, revival }, { revivalCompression, -18 },
            { driveOn, 1 }, { "mb1Style", warmTubeStyle }, { "mb1Drive", 0.35f },
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

        // MARS in three bands: clean lows (no mud), amp-driven mids for bite,
        // tape on the highs so the grit stays smooth.
        make ("Multiband Grit", B, 6, "Rock,Saturation,Mars", {
            { rigLevel, -6 },   // even with the other rigs
            { compOn, 1 }, { compType, revival }, { revivalCompression, -18 },
            { deessOn, 1 }, { deessAmount, 7 },
            { driveOn, 1 }, { marsBands, 2 }, { marsCross1, 250 }, { marsCross2, 3000 },
            { "mb1Style", cleanTubeStyle }, { "mb1Drive", 0.2f },
            { "mb2Style", britishAmpStyle }, { "mb2Drive", 0.55f }, { "mb2Dynamics", 0.3f },
            { "mb3Style", warmTapeStyle }, { "mb3Drive", 0.35f }, { "mb3Level", -2 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.3f }, { reverbMix, 0.12f } }),

        // Upfront rock on PHOTON, the FET compressor (0.41.0): 8:1, a quick
        // attack and release, so every word punches through the band.
        make ("Punch Lead", B, 7, "Rock,Photon,Studio", {
            { compOn, 1 }, { compType, photonComp }, { photonInput, 24 }, { photonOutput, -10 }, { photonAttack, 5 }, { photonRelease, 6 },
            { photonRatio, photonEight },
            { deessOn, 1 }, { deessAmount, 6 },
            { eqOn, 1 }, { "eq125", -2 }, { "eq2k", 1.5f }, { "eq4k", 2 },
            { delayOn, 1 }, { delaySync, eighth }, { delayFeedback, 0.2f }, { delayMix, 0.1f }, { delayDuck, 0.6f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.35f }, { reverbMix, 0.12f } }),

        // PHOTON with every RATIO button in (ALL): the loudest words pushed
        // back and roughed up, the classic aggressive shout.
        make ("All-In Shout", B, 8, "Rock,Rap,Photon,Aggressive", {
            { compOn, 1 }, { compType, photonComp }, { photonInput, 26 }, { photonOutput, -6 }, { photonAttack, 6 }, { photonRelease, 6 },
            { photonRatio, photonAll },
            { deessOn, 1 }, { deessAmount, 6 },
            { eqOn, 1 }, { "eq125", -3 }, { "eq1k", 1 }, { "eq4k", 2 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.3f }, { reverbMix, 0.1f } }),

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

        // A stack of your own voice: four loose takes spread wide and an
        // octave-down take underneath, in a hall (the DOUBLER; until 0.46.0
        // this was "Harmony Stack", harmonies a third and a fifth above).
        make ("Vocal Stack", E, 3, "Doubler,Pop", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -16 },
            { deessOn, 1 }, { deessAmount, 6 },
            { doublerOn, 1 }, { "dv3On", 1 }, { "dv4On", 1 }, { doublerOutput, -3 },
            { "dv1Pan", -1 }, { "dv2Pan", 1 }, { "dv1Drift", 0.6f }, { "dv2Drift", 0.6f },
            { "dv3Octave", 1 }, { "dv3Gain", -12 }, { "dv3Pan", 0 }, { "dv3Detune", 0 },
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
            // Cyclone slow, well in the sound, through the cabinet's own speakers (KEY).
            { modOn, 1 }, { modType, rotary }, { modCycFast, 0 }, { modCycSlowSpeed, 1.2f }, { modCycLow, 4 }, { modCycHigh, 4 },
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
            { rigLevel, -3 },   // AEON adds dry and wet at full level: louder than the old flanger
            { compOn, 1 }, { compType, revival }, { revivalCompression, -16 },
            { modOn, 1 }, { modType, flanger }, { modRate, 0.2f }, { modDepth, 0.7f }, { modManual, 5 }, { modRes, 6 },
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
            { rigLevel, -3 },   // AEON adds dry and wet at full level: louder than the old flanger
            { compOn, 1 }, { compType, revival }, { revivalCompression, -18 },
            { modOn, 1 }, { modType, flanger }, { modRate, 0.1f }, { modDepth, 0.9f }, { modManual, 7 }, { modRes, 9 },
            { delayOn, 1 }, { delayType, analogDelay }, { delaySync, syncOff }, { delayTime, 25 }, { delayFeedback, 0.6f },
            { delayMix, 0.3f },
            { lofiOn, 1 }, { lofiType, bitcrusher }, { lofiAmount, 4 }, { lofiNoise, 0 }, { lofiMix, 0.6f } }),

        // A voice through a broken transmitter: MARS crushes its highs to bits
        // (its lows left alone), with a slap behind it.
        make ("Broken Transmission", E, 15, "Lo-Fi,Saturation,Mars,Experimental", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -18 },
            { driveOn, 1 }, { marsBands, 1 }, { marsCross1, 900 },
            { "mb1On", 0 },
            { "mb2Style", destroyStyle }, { "mb2Drive", 0.55f }, { "mb2Mix", 0.7f }, { "mb2Feedback", 0.3f }, { "mb2FbFreq", 2200 },
            { delayOn, 1 }, { delayType, tapeDelay }, { delaySync, syncOff }, { delayTime, 140 }, { delayFeedback, 0.25f },
            { delayMix, 0.2f } }),

        // Sixteenth-note echoes for rap and trap ad-libs, kept tight.
        make ("Trap Ad-Lib", E, 14, "Rap,Delay", {
            { rigLevel, -3 },   // even with the other rigs
            { compOn, 1 }, { compType, revival }, { revivalCompression, -20 },
            { deessOn, 1 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq125", -2 }, { "eq4k", 2 }, { "eq8k", 2 },
            { delayOn, 1 }, { delaySync, sixteenth }, { delayFeedback, 0.35f }, { delayMix, 0.2f }, { delayPingPong, 1 },
            { delayHighCut, 5000 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.35f }, { reverbMix, 0.12f } }),

        // ---- MARS's modulation (0.15.0) -------------------------------------
        // Tape that breathes with you: an envelope follower on DRIVE, so the
        // louder you sing, the harder the tape is pushed.
        make ("Breathing Tape", E, 16, "Saturation,Mars,Modulation", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -12 },
            { deessOn, 1 }, { deessAmount, 5 },
            { driveOn, 1 }, { "mb1Style", warmTapeStyle }, { "mb1Drive", 0.1f },
            { "mmSlot1Source", envelopeFollower1 }, { "mmSlot1Target", band1Drive }, { "mmSlot1Amount", 0.6f },
            { "mmEf1Attack", 20 }, { "mmEf1Release", 250 },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.4f }, { reverbMix, 0.15f } }),

        // A tube that throbs in time: an XLFO, an eighth note long, pulls
        // the saturated band down and back up on every eighth.
        make ("Throb", E, 17, "Saturation,Mars,Modulation,Tempo", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -16 },
            { driveOn, 1 }, { "mb1Style", saturatedTubeStyle }, { "mb1Drive", 0.45f },
            { "mmLfo1Sync", eighthNote }, { "mmLfo1S1Value", 0 }, { "mmLfo1S2Value", -1 },
            { "mmSlot1Source", xlfo1 }, { "mmSlot1Target", band1Level }, { "mmSlot1Amount", 0.3f },
            { delayOn, 1 }, { delaySync, quarter }, { delayFeedback, 0.2f }, { delayMix, 0.12f } }),

        // A voice through a radio being tuned: everything above MARS's
        // crossover is cut, and a slow XLFO (two bars) sweeps the crossover.
        make ("Sweeping Radio", E, 18, "Lo-Fi,Mars,Modulation,Tempo", {
            { rigLevel, -4 },   // even with the other rigs
            { compOn, 1 }, { compType, revival }, { revivalCompression, -20 },
            { driveOn, 1 }, { marsBands, 1 }, { marsCross1, 1200 },
            { "mb1Style", britishAmpStyle }, { "mb1Drive", 0.35f }, { "mb2Level", -36 },
            { "mmLfo1Sync", twoBars },
            { "mmSlot1Source", xlfo1 }, { "mmSlot1Target", crossover1 }, { "mmSlot1Amount", 0.2f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.3f }, { reverbMix, 0.12f } }),

        // Grit on your foot: the expression pedal turns MARS's DRIVE up,
        // clean at the heel, crunchy at the toe (whatever the pedal's own setting).
        make ("Pedal Grit", E, 19, "Saturation,Mars,Modulation,Expression", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -16 },
            { deessOn, 1 }, { deessAmount, 5 },
            { driveOn, 1 }, { "mb1Style", crunchyAmpStyle }, { "mb1Drive", 0.05f },
            { "mmSlot1Source", expressionPedal }, { "mmSlot1Target", band1Drive }, { "mmSlot1Amount", 0.7f },
            { delayOn, 1 }, { delaySync, eighth }, { delayFeedback, 0.25f }, { delayMix, 0.12f } }),

        // A rhythmic gate: an eight-step XLFO, a bar long, with no glide,
        // ducks the voice on the off-steps. Sing long notes through it.
        make ("Stutter Steps", E, 20, "Mars,Modulation,Tempo,Experimental", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -14 },
            { driveOn, 1 }, { "mb1Style", warmTubeStyle }, { "mb1Drive", 0.25f },
            { "mmLfo1Steps", 7 }, { "mmLfo1Sync", oneBar },
            { "mmLfo1S1Value", 0 }, { "mmLfo1S1Glide", 0 }, { "mmLfo1S2Value", -1 }, { "mmLfo1S2Glide", 0 },
            { "mmLfo1S3Value", 0 }, { "mmLfo1S4Value", 0 }, { "mmLfo1S5Value", -1 }, { "mmLfo1S6Value", 0 },
            { "mmLfo1S7Value", -1 }, { "mmLfo1S8Value", 0 },
            { "mmSlot1Source", xlfo1 }, { "mmSlot1Target", band1Level }, { "mmSlot1Amount", 0.5f },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.35f }, { reverbMix, 0.15f } }),

        // The hard-tuned robot voice: every note snapped straight to pitch
        // (TUNE VOICE: Hard), the slides turned into steps. Set KEY and
        // SCALE for the song first (or SCALE to Chromatic); it runs 40 ms late.
        make ("Hard Tune", E, 21, "Pop,Rap,Tuning,Experimental", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -20 },
            { deessOn, 1 }, { deessAmount, 7 },
            { harmonyCorrect, hardTune },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq4k", 2 }, { "eq8k", 3 },
            { doublerOn, 1 }, { "dv1Gain", -8 }, { "dv2Gain", -8 },
            { delayOn, 1 }, { delaySync, quarter }, { delayFeedback, 0.25f }, { delayMix, 0.12f }, { delayDuck, 0.6f },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.4f }, { reverbMix, 0.14f } }),

        // A wall of voices out of one: four loose takes and an octave-down
        // take spread wide, a shimmer echo rising an octave above, a big
        // hall (the DOUBLER; until 0.46.0 this was "Auto Choir", a choir
        // generated by HARMONY).
        make ("Voice Wall", E, 22, "Doubler,Ambient", {
            { rigLevel, -4 },   // even with the other rigs
            { compOn, 1 }, { compType, revival }, { revivalCompression, -18 },
            { deessOn, 1 }, { deessAmount, 6 },
            { doublerOn, 1 }, { "dv3On", 1 }, { "dv4On", 1 }, { doublerOutput, -3 },
            { "dv1Pan", -1 }, { "dv2Pan", 1 }, { "dv1Drift", 0.8f }, { "dv2Drift", 0.8f }, { "dv4Drift", 0.8f },
            { "dv3Octave", 1 }, { "dv3Gain", -10 }, { "dv3Pan", 0 }, { "dv3Detune", 0 },
            { delayOn, 1 }, { delayType, shimmer }, { delaySync, quarter }, { delayFeedback, 0.35f }, { delayMix, 0.12f },
            { delayDuck, 0.6f },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.75f }, { reverbMix, 0.3f }, { reverbPreDelay, 40 } }),

        // The VOCODER (0.35.0): the synth shaped by your voice. Auto plays
        // what you sing, so these work without a keyboard.

        // The classic robot: crisp (HARD, high SCALE), a saw on your own
        // note, a little crushed noise so the consonants still cut.
        make ("Robot Voice", E, 23, "Vocoder,Robot,Electronic,Experimental", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -18 },
            { deessOn, 1 }, { deessAmount, 4 },
            { vocoderOn, 1 }, { vocoderMode, 2 }, { vocoderScale, 0.75f },
            { vocoderNoiseOn, 1 }, { vocoderNoiseGain, -20 }, { vocoderNoiseCrush, 0.9f },
            { delayOn, 1 }, { delaySync, eighth }, { delayFeedback, 0.25f }, { delayMix, 0.1f }, { delayDuck, 0.6f },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.4f }, { reverbMix, 0.15f } }),

        // A choir of synths: seven detuned saws on your note, a third and a
        // fifth above and an octave below, in the key (INTERVAL voices).
        make ("Vocoder Choir", E, 24, "Vocoder,Choir,Electronic,Ambient", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -18 },
            { deessOn, 1 }, { deessAmount, 5 },
            { vocoderOn, 1 }, { vocoderMode, 1 }, { vocoderScale, 0.45f },
            { "vocOsc1Wave", 4 }, { "vocOsc1Shape", 0.4f },
            { "vocInterval1", 3 }, { "vocInterval2", 5 }, { "vocInterval3", 15 }, { vocoderIntervalLevel, -5 },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.7f }, { reverbMix, 0.28f }, { reverbPreDelay, 30 } }),

        // A low, dark machine: the synth an octave under you, its bands
        // shifted down, soft square waves, the top rolled off.
        make ("Deep Robot", E, 25, "Vocoder,Robot,Dark,Experimental", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -18 },
            { deessOn, 1 }, { deessAmount, 4 },
            { vocoderOn, 1 }, { vocoderMode, 1 }, { vocoderShift, -4 }, { vocoderScale, 0.5f },
            { "vocOsc1Wave", 1 }, { "vocOsc1Shape", 0.3f }, { "vocOsc1Coarse", -12 },
            { "vocOsc2On", 1 }, { "vocOsc2Wave", 0 }, { "vocOsc2Coarse", -24 }, { "vocOsc2Gain", -8 },
            { vocoderHighCut, 7000 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.35f }, { reverbMix, 0.15f } }),

        // For a MIDI keyboard: play chords, sing the words (POLY). Your own
        // voice stays faintly underneath, so it's never silent without keys.
        make ("Keys Vocoder", E, 26, "Vocoder,MIDI,Electronic,Choir", {
            { rigLevel, 3 },   // without keys only the faint dry voice plays
            { compOn, 1 }, { compType, revival }, { revivalCompression, -18 },
            { deessOn, 1 }, { deessAmount, 5 },
            { vocoderOn, 1 }, { vocoderMode, 1 }, { vocoderScale, 0.55f }, { vocoderDry, -8 },
            { vocoderCarrier, 1 }, { vocoderVoicing, 1 },
            { "vocOsc1Wave", 4 }, { "vocOsc1Shape", 0.3f }, { "vocOsc2On", 1 }, { "vocOsc2Wave", 1 },
            { "vocOsc2Shape", 0.5f }, { "vocOsc2ShapeMod", 0.4f }, { vocoderLfoRate, 0.4f },
            { vocoderNoiseOn, 1 }, { vocoderNoiseGain, -22 }, { vocoderNoiseCrush, 0.9f },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.6f }, { reverbMix, 0.25f } }),

        // LO-FI's voices through a device (0.36.0).

        // A call on a bad line: the phone's codec, a few lost moments, a
        // short room around it (the other end).
        make ("Phone Call", E, 27, "Lo-Fi,Phone,Intro,Experimental", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -16 },
            { deessOn, 1 }, { deessAmount, 4 },
            { lofiOn, 1 }, { lofiType, cellPhone }, { lofiAmount, 4 }, { lofiNoise, 3 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.25f }, { reverbMix, 0.1f } }),

        // Over the radio: narrow, squashed, static while you talk and the
        // squelch's hiss when you stop.
        make ("Radio Check", E, 28, "Lo-Fi,Radio,Intro,Experimental", {
            { rigLevel, 7 },   // even with the other rigs
            { compOn, 1 }, { compType, revival }, { revivalCompression, -14 },
            { deessOn, 1 }, { deessAmount, 4 },
            { lofiOn, 1 }, { lofiType, walkieTalkie }, { lofiAmount, 6 }, { lofiNoise, 4 } }),

        // An old 78: the horn, the warble, the crackle, in a little room.
        make ("Old 78", E, 29, "Lo-Fi,Vintage,Intro,Jazz", {
            { rigLevel, 4 },   // even with the other rigs
            { compOn, 1 }, { compType, revival }, { revivalCompression, -14 },
            { deessOn, 1 }, { deessAmount, 4 },
            { lofiOn, 1 }, { lofiType, gramophone }, { lofiAmount, 5 }, { lofiNoise, 5 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.3f }, { reverbMix, 0.12f } }),

        // FORMANT (0.37.0): the voice's size, the pitch kept.

        // A cartoon-small voice: the resonances an octave up, a bright plate.
        make ("Chipmunk", E, 30, "Formant,Character,Experimental", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -16 },
            { deessOn, 1 }, { deessAmount, 5 },
            { formantOn, 1 }, { formantShift, 9 },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.35f }, { reverbMix, 0.14f } }),

        // A giant: the resonances down most of an octave, darker, a big room.
        make ("Giant", E, 31, "Formant,Character,Dark,Experimental", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -16 },
            { deessOn, 1 }, { deessAmount, 3 },
            { formantOn, 1 }, { formantShift, -8 },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.7f }, { reverbMix, 0.22f } }),

        // ---- F: signature styles ---------------------------------------------
        // Each is our reading of a style's vocal sound (from how such records
        // are commonly made: doubling, compression, delays, saturation), not
        // a copy of anyone's settings.

        // 2000s pop-punk lead: squeezed hard, bright, and double-tracked
        // tight, as those records almost always were.
        make ("Pop-Punk Lead", F, 1, "Pop-Punk,Rock,Doubler", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -22 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 7 },
            { driveOn, 1 }, { "mb1Style", warmTapeStyle }, { "mb1Drive", 0.2f },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -3 }, { "eq250", -2 }, { "eq2k", 2 }, { "eq4k", 3 }, { "eq8k", 2 },
            { doublerOn, 1 }, { "dv1Gain", -4 }, { "dv2Gain", -4 }, { "dv1Detune", 7 }, { "dv2Detune", -7 },
            { "dv1Drift", 0.4f }, { "dv2Drift", 0.4f },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.35f }, { reverbMix, 0.12f } }),

        // The pop-punk chorus: four takes wide, an octave-down take tucked in
        // for a gang-vocal weight, a quarter-note echo behind.
        make ("Pop-Punk Chorus", F, 2, "Pop-Punk,Rock,Doubler", {
            { rigLevel, -3 },   // even with the other rigs
            { compOn, 1 }, { compType, revival }, { revivalCompression, -22 },
            { deessOn, 1 }, { deessAmount, 7 },
            { driveOn, 1 }, { "mb1Style", saturatedTapeStyle }, { "mb1Drive", 0.25f },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -3 }, { "eq2k", 2 }, { "eq4k", 3 }, { "eq8k", 2 },
            { doublerOn, 1 }, { "dv3On", 1 }, { "dv4On", 1 }, { "dv1Drift", 0.6f }, { "dv2Drift", 0.6f },
            { "dv3Drift", 0.6f }, { "dv4Drift", 0.6f }, { doublerOutput, -3 },
            { "dv3Octave", 1 }, { "dv3Gain", -14 }, { "dv3Pan", 0 }, { "dv3Detune", 0 },
            { delayOn, 1 }, { delaySync, quarter }, { delayFeedback, 0.2f }, { delayMix, 0.1f }, { delayDuck, 0.6f },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.45f }, { reverbMix, 0.15f } }),

        // The pop-rock ballad: warm and close in the verse, a long plate and
        // a dotted-eighth echo to carry the held notes.
        make ("Pop-Rock Ballad", F, 3, "Pop-Punk,Ballad,Delay,Reverb", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -16 },
            { deessOn, 1 }, { deessAmount, 6 },
            { eqOn, 1 }, { "eq63", -4 }, { "eq125", 1 }, { "eq250", 1 }, { "eq4k", 1 }, { "eq16k", 2 },
            { delayOn, 1 }, { delaySync, dottedEighth }, { delayFeedback, 0.3f }, { delayMix, 0.14f }, { delayDuck, 0.6f },
            { delayHighCut, 5000 },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.6f }, { reverbMix, 0.22f }, { reverbPreDelay, 40 } }),

        // Modern metalcore cleans: airy and polished, floating on a wide
        // ping-pong echo and a big hall that ducks out of the way.
        make ("Ethereal Metalcore Clean", F, 4, "Metal,Metalcore,Ambient,Delay,Reverb", {
            { rigLevel, -2 },   // even with the other rigs
            { compOn, 1 }, { compType, revival }, { revivalCompression, -20 },
            { deessOn, 1 }, { deessFreq, 7500 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq250", -2 }, { "eq4k", 1 }, { "eq8k", 3 }, { "eq16k", 4 },
            { doublerOn, 1 }, { "dv1Gain", -8 }, { "dv2Gain", -8 },
            { delayOn, 1 }, { delaySync, dottedEighth }, { delayPingPong, 1 }, { delayFeedback, 0.4f }, { delayMix, 0.18f },
            { delayDuck, 0.7f }, { delayLowCut, 300 }, { delayHighCut, 6000 },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.75f }, { reverbMix, 0.3f }, { reverbPreDelay, 60 },
            { reverbDecay, 1.3f } }),

        // Metalcore screams: two bands of MARS (the lows kept tight, the rest
        // torn up), pushed harder the louder you scream (an envelope
        // follower on DRIVE), doubled and kept dry.
        make ("Metalcore Scream", F, 5, "Metal,Metalcore,Scream,Saturation,Mars,Modulation", {
            { rigLevel, -6 },   // even with the other rigs
            { compOn, 1 }, { compType, photonComp }, { photonInput, 26 }, { photonOutput, -13 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, photonEight },   // Daybreak until 0.42.0
            { driveOn, 1 }, { marsBands, 1 }, { marsCross1, 350 },
            { "mb1Style", warmTubeStyle }, { "mb1Drive", 0.15f },
            { "mb2Style", brokenTubeStyle }, { "mb2Drive", 0.45f }, { "mb2Presence", 2 },
            { "mmSlot1Source", envelopeFollower1 }, { "mmSlot1Target", band2Drive }, { "mmSlot1Amount", 0.35f },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -3 }, { "eq1k", 1 }, { "eq2k", 3 }, { "eq8k", -2 },
            { doublerOn, 1 }, { "dv1Gain", -5 }, { "dv2Gain", -5 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.25f }, { reverbMix, 0.1f } }),

        // Haunted cleans: a ghostly octave above in the echoes (SHIMMER), a
        // little texture glitching in the background, a cathedral behind.
        make ("Haunted Clean", F, 6, "Metal,Metalcore,Ambient,Doubler,Experimental", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -18 },
            { deessOn, 1 }, { deessAmount, 6 },
            { doublerOn, 1 }, { "dv1Gain", -10 }, { "dv2Gain", -10 },
            { glitchOn, 1 }, { glitchMode, texturizer }, { glitchMix, 0.15f },
            { delayOn, 1 }, { delayType, shimmer }, { delaySync, quarter }, { delayFeedback, 0.4f }, { delayMix, 0.14f }, { delayDuck, 0.6f },
            { reverbOn, 1 }, { reverbType, cathedral }, { reverbSize, 0.7f }, { reverbMix, 0.3f }, { reverbPreDelay, 50 } }),

        // Prog-metal tenor: a high, clear lead stacked with four takes and an
        // octave-down take for body, with a dual echo.
        make ("Prog Tenor Stack", F, 7, "Metal,Prog,Doubler,Delay", {
            { rigLevel, -3 },   // even with the other rigs
            { compOn, 1 }, { compType, revival }, { revivalCompression, -20 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -3 }, { "eq250", -1 }, { "eq2k", 1 }, { "eq4k", 2 }, { "eq8k", 2 }, { "eq16k", 2 },
            { doublerOn, 1 }, { "dv1Gain", -6 }, { "dv2Gain", -6 }, { "dv3On", 1 }, { "dv4On", 1 }, { doublerOutput, -3 },
            { "dv3Octave", 1 }, { "dv3Gain", -14 }, { "dv3Pan", 0 }, { "dv3Detune", 0 },
            { delayOn, 1 }, { delaySync, quarter }, { delayDual, 1 }, { delaySyncRight, dottedEighth },
            { delayFeedback, 0.3f }, { delayMix, 0.14f }, { delayDuck, 0.6f }, { delayHighCut, 6000 },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.6f }, { reverbMix, 0.2f }, { reverbPreDelay, 40 } }),

        // Prog-metal screams: mid-forward and articulate rather than buried,
        // an octave-down take underneath for weight, a short slap.
        make ("Prog Scream", F, 8, "Metal,Prog,Scream,Saturation,Mars", {
            { rigLevel, -6 },   // even with the other rigs
            { compOn, 1 }, { compType, photonComp }, { photonInput, 26 }, { photonOutput, -13 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, photonEight },   // Daybreak until 0.42.0
            { driveOn, 1 }, { "mb1Style", crunchyAmpStyle }, { "mb1Drive", 0.55f }, { "mb1Mid", 2 }, { "mb1Presence", 2 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq1k", 2 }, { "eq2k", 2 }, { "eq8k", -2 },
            { doublerOn, 1 }, { "dv3On", 1 }, { "dv3Octave", 1 }, { "dv3Gain", -12 }, { "dv3Pan", 0 }, { "dv3Detune", 0 },
            { delayOn, 1 }, { delayType, analogDelay }, { delaySync, syncOff }, { delayTime, 110 }, { delayFeedback, 0.15f },
            { delayMix, 0.12f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.3f }, { reverbMix, 0.1f } }),

        // The soaring prog chorus: four takes, wide shimmer echoes glinting an
        // octave on top, and a long hall.
        make ("Soaring Prog Chorus", F, 9, "Metal,Prog,Doubler,Reverb", {
            { rigLevel, -4 },   // even with the other rigs
            { compOn, 1 }, { compType, revival }, { revivalCompression, -22 },
            { deessOn, 1 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq4k", 2 }, { "eq8k", 3 }, { "eq16k", 3 },
            { doublerOn, 1 }, { "dv3On", 1 }, { "dv4On", 1 }, { doublerOutput, -4 },
            { delayOn, 1 }, { delayType, shimmer }, { delaySync, eighth }, { delayPingPong, 1 }, { delayFeedback, 0.35f }, { delayMix, 0.14f },
            { delayDuck, 0.7f },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.8f }, { reverbMix, 0.28f }, { reverbDecay, 1.4f } }),

        // 90s grunge: a doubled, mid-heavy, slightly dirty voice with a
        // touch of chorus, in a dark plate.
        make ("Grunge Double", F, 10, "Grunge,Rock,Doubler,Saturation", {
            { compOn, 1 }, { compType, photonComp }, { photonInput, 22 }, { photonOutput, -11 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, 0 },   // Daybreak until 0.42.0
            { driveOn, 1 }, { "mb1Style", warmTubeStyle }, { "mb1Drive", 0.4f }, { "mb1Treble", -2 },
            { eqOn, 1 }, { "eq63", -4 }, { "eq500", 1 }, { "eq1k", 2 }, { "eq8k", -2 }, { "eq16k", -3 },
            { doublerOn, 1 }, { "dv1Gain", -3 }, { "dv2Gain", -3 }, { "dv1Drift", 0.7f }, { "dv2Drift", 0.7f },
            { modOn, 1 }, { modType, chorus }, { modRate, 0.4f }, { modDepth, 0.3f }, { modMix, 0.2f },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.45f }, { reverbMix, 0.15f }, { reverbRolloff, 6000 } }),

        // 2000s emo and post-hardcore: clean but pushed, a slap and a short
        // room, ready to break into a shout.
        make ("Emo Anthem", F, 11, "Emo,Rock,Saturation,Delay", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -20 },
            { deessOn, 1 }, { deessAmount, 6 },
            { driveOn, 1 }, { "mb1Style", saturatedTubeStyle }, { "mb1Drive", 0.3f },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq1k", 1 }, { "eq2k", 2 }, { "eq4k", 2 },
            { doublerOn, 1 }, { "dv1Gain", -6 }, { "dv2Gain", -6 },
            { delayOn, 1 }, { delayType, tapeDelay }, { delaySync, syncOff }, { delayTime, 130 }, { delayFeedback, 0.2f },
            { delayMix, 0.15f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.35f }, { reverbMix, 0.12f } }),

        // Nu-metal: a rap-rock voice that's hard, dry and right up front,
        // doubled for the hooks, with a driven edge.
        make ("Nu-Metal Shout", F, 12, "Nu-Metal,Rap,Rock,Saturation,Doubler", {
            { rigLevel, -3 },   // even with the other rigs
            { compOn, 1 }, { compType, photonComp }, { photonInput, 26 }, { photonOutput, -13 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, photonEight },   // Daybreak until 0.42.0
            { driveOn, 1 }, { "mb1Style", americanAmpStyle }, { "mb1Drive", 0.45f }, { "mb1Mix", 0.8f },
            { eqOn, 1 }, { "eq63", -4 }, { "eq250", -2 }, { "eq1k", 1 }, { "eq2k", 2 }, { "eq4k", 3 }, { "eq8k", 2 },
            { doublerOn, 1 }, { "dv1Gain", -4 }, { "dv2Gain", -4 }, { "dv1Detune", 4 }, { "dv2Detune", -4 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.2f }, { reverbMix, 0.08f } }),

        // Deathcore lows: a growl with a full octave-down take at its own
        // level beneath it, torn up by MARS, very dry.
        make ("Deathcore Growl", F, 13, "Metal,Deathcore,Scream,Pitch,Saturation", {
            { rigLevel, -8 },   // even with the other rigs
            { compOn, 1 }, { compType, photonComp }, { photonInput, 28 }, { photonOutput, -17 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, photonEight },   // Daybreak until 0.42.0
            { driveOn, 1 }, { "mb1Style", screamingAmpStyle }, { "mb1Drive", 0.5f }, { "mb1Bass", 2 }, { "mb1Treble", -3 },
            { eqOn, 1 }, { "eq63", -2 }, { "eq125", 2 }, { "eq250", 1 }, { "eq2k", 1 }, { "eq8k", -4 }, { "eq16k", -6 },
            { doublerOn, 1 }, { "dv1On", 0 }, { "dv2On", 0 }, { "dv3On", 1 }, { "dv3Octave", 1 }, { "dv3Gain", -3 },
            { "dv3Pan", 0 }, { "dv3Detune", 0 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.2f }, { reverbMix, 0.06f } }),

        // Whisper pop: breathy and intimate, right on the mic, with a soft
        // octave-down shadow and a small room.
        make ("Whisper Pop", F, 14, "Pop,Intimate,Doubler", {
            { rigLevel, -5 },   // even with the other rigs
            { compOn, 1 }, { compType, revival }, { revivalCompression, -24 },
            { deessOn, 1 }, { deessFreq, 6500 }, { deessAmount, 9 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", 2 }, { "eq250", 1 }, { "eq2k", -1 }, { "eq8k", 2 }, { "eq16k", 3 },
            { doublerOn, 1 }, { "dv1On", 0 }, { "dv2On", 0 }, { "dv3On", 1 }, { "dv3Octave", 1 }, { "dv3Gain", -14 },
            { "dv3Pan", 0 }, { "dv3Detune", 0 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.3f }, { reverbMix, 0.1f } }),

        // Bedroom indie: worn tape, a slow chorus and a small room, as if
        // recorded at home on old gear.
        make ("Bedroom Indie", F, 15, "Indie,Lo-Fi,Modulation", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -16 },
            { driveOn, 1 }, { "mb1Style", warmTapeStyle }, { "mb1Drive", 0.3f },
            { modOn, 1 }, { modType, chorus }, { modRate, 0.3f }, { modDepth, 0.4f }, { modMix, 0.3f },
            { lofiOn, 1 }, { lofiType, cassette }, { lofiAmount, 3 }, { lofiNoise, 1 }, { lofiMix, 0.5f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.4f }, { reverbMix, 0.18f } }),

        // 80s synth-pop: chorused, a gated burst of reverb, a big
        // dotted-eighth echo.
        make ("Synth-Pop 80s", F, 16, "80s,Pop,Modulation,Reverb", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -18 },
            { deessOn, 1 }, { deessAmount, 6 },
            { modOn, 1 }, { modType, dimension }, { modRate, 0.5f }, { modDepth, 0.5f }, { modMix, 0.35f },
            { delayOn, 1 }, { delaySync, dottedEighth }, { delayFeedback, 0.35f }, { delayMix, 0.16f }, { delayDuck, 0.5f },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.6f }, { reverbMix, 0.3f }, { reverbPreDelay, 0 },
            { reverbDecay, 0.25f }, { reverbSpread, 200 }, { reverbShape, 0.85f } }),

        // ---- G: moving harmonies ---------------------------------------------
        // HARMONY's modulators: right-click a HARMONY knob to see what's on
        // it, or open MODS. The sounds generated by GENERATE NOTES follow
        // your voice; MIDI keys add to them.

        // A choir where every voice has its own slow vibrato (M1 TRIGGER Poly:
        // each note's cycle from its own start), so they never wobble in step.
        withMods (make ("Living Choir", G, 1, "Harmony,Choir,Modulators,Ambient", {
            { rigLevel, -4 },   // even with the other rigs
            { compOn, 1 }, { compType, revival }, { revivalCompression, -18 },
            { deessOn, 1 }, { deessAmount, 6 },
            { harmonyOn, 1 }, { "hv1Active", 0 }, { "hv2Active", 0 },
            { harmonyGenerate, 1 }, { harmonyChord, choirMap }, { harmonyNoteLevel, -8 }, { harmonyNoteWidth, 0.9f },
            { harmonyNoteAttack, 150 }, { harmonyNoteRelease, 500 },
            { "hm1Rate", 4.8f }, { "hm1Trigger", polyTrigger },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.7f }, { reverbMix, 0.28f }, { reverbPreDelay, 40 } }),
            { { harmonyGlobalPitch, HMod::m1, 0.18f / 24.0f } }),   // ±18 cents, note by note

        // Each new harmony note swells in behind you (A1 on the note voices'
        // LEVEL: a slow attack, held, a long fade), while you stay up front.
        withMods (make ("Swell Stack", G, 2, "Harmony,Modulators,Ambient,Ballad", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -16 },
            { deessOn, 1 }, { deessAmount, 6 },
            { harmonyOn, 1 }, { "hv1Active", 0 }, { "hv2Active", 0 },
            { harmonyGenerate, 1 }, { harmonyChord, threePartStack }, { harmonyNoteLevel, -26 }, { harmonyNoteWidth, 0.8f },
            { harmonyNoteRelease, 800 },
            { "harmonyA1Attack", 700 }, { "harmonyA1Decay", 600 }, { "harmonyA1Sustain", 0.85f }, { "harmonyA1Release", 900 },
            { "harmonyA1AttackCurve", 0.6f },
            { delayOn, 1 }, { delaySync, quarter }, { delayFeedback, 0.3f }, { delayMix, 0.12f }, { delayDuck, 0.6f },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.6f }, { reverbMix, 0.25f } }),
            { { harmonyNoteLevel, HMod::a1, 0.3f } }),   // up to about 20 dB louder at the top of the swell

        // A wide gospel chord the Spreader opens out: the low notes a touch
        // flat and darker, the high ones a touch sharp and brighter.
        withMods (make ("Spread Gospel", G, 3, "Harmony,Choir,Modulators,Soul", {
            { rigLevel, -5 },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -18 },
            { deessOn, 1 }, { deessAmount, 6 },
            { harmonyOn, 1 }, { "hv1Active", 0 }, { "hv2Active", 0 },
            { harmonyGenerate, 1 }, { harmonyChord, gospelStack }, { harmonyNoteLevel, -9 }, { harmonyNoteWidth, 1 },
            { harmonyNoteAttack, 80 }, { harmonyNoteRelease, 400 },
            { harmonySpMode, outward },
            { reverbOn, 1 }, { reverbType, chamber }, { reverbSize, 0.6f }, { reverbMix, 0.24f } }),
            { { harmonyGlobalPitch, HMod::sp, 0.12f / 24.0f },     // ±12 cents, outward
              { harmonyGlobalFormant, HMod::sp, 0.06f } }),        // ±1.4 semitones of formant

        // Two harmonies circling you: a third and a fifth panned by one slow
        // LFO, in opposite directions.
        withMods (make ("Circling Thirds", G, 4, "Harmony,Modulators,Stereo", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -16 },
            { deessOn, 1 }, { deessAmount, 6 },
            { harmonyOn, 1 }, { "hv1Level", -7 }, { "hv2Level", -9 }, { "hv1Pan", 0 }, { "hv2Pan", 0 },
            { "hm1Rate", 0.2f },
            { delayOn, 1 }, { delaySync, dottedEighth }, { delayFeedback, 0.25f }, { delayMix, 0.1f }, { delayDuck, 0.6f },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.55f }, { reverbMix, 0.22f } }),
            { { "hv1Pan", HMod::m1, 0.45f }, { "hv2Pan", HMod::m1, -0.45f } }),   // each 0.9 either way

        // A harmony that steps through a pattern in time: a 4-step SEQ on
        // voice 1's PITCH (octave, fifth, octave, an octave and a third),
        // locked to the song's eighth notes.
        withMods (make ("Arp Harmony", G, 5, "Harmony,Modulators,Tempo,Electronic", {
            { rigLevel, -1.5f },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -18 },
            { deessOn, 1 }, { deessAmount, 6 },
            { harmonyOn, 1 }, { "hv1Pitch", 0 }, { "hv1Level", -6 }, { "hv1Pan", 0.3f }, { "hv2Active", 0 },
            { "hm1Type", seq }, { "hm1Steps", 4 }, { "hm1Sync", modEighth }, { "hm1Trigger", syncTrigger },
            { "hm1Step1", 12 }, { "hm1Step2", 7 }, { "hm1Step3", 12 }, { "hm1Step4", 16 },
            { delayOn, 1 }, { delaySync, dottedEighth }, { delayFeedback, 0.35f }, { delayMix, 0.15f }, { delayDuck, 0.5f },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.5f }, { reverbMix, 0.2f } }),
            { { "hv1Pitch", HMod::m1, 1.0f } }),   // the steps exactly, in semitones (then into the key)

        // Harmonies that answer how hard you sing: push and they step back
        // (AM ducks them), sing softly and they come forward.
        withMods (make ("Breathing Harmony", G, 6, "Harmony,Modulators,Dynamics,Ballad", {
            { rigLevel, 3 },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -12 },
            { deessOn, 1 }, { deessAmount, 6 },
            { harmonyOn, 1 }, { "hv1Level", -4 }, { "hv2Level", -6 },
            { harmonyAmAttack, 30 }, { harmonyAmRelease, 400 },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.55f }, { reverbMix, 0.22f } }),
            { { harmonyLevel, HMod::am, -0.3f } }),   // up to 20 dB down at the loudest

        // A rhythmic gate on the harmonies, drawn in M1 (a bar of eighths and
        // sixteenths), locked to the song: the trance-gate trick, on a choir.
        withShape (withMods (make ("Gated Choir", G, 7, "Harmony,Choir,Modulators,Tempo,Electronic", {
            { rigLevel, -3 },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -18 },
            { deessOn, 1 }, { deessAmount, 6 },
            { harmonyOn, 1 }, { "hv1Active", 0 }, { "hv2Active", 0 },
            { harmonyGenerate, 1 }, { harmonyChord, threePartStack }, { harmonyNoteLevel, -6 }, { harmonyNoteWidth, 0.9f },
            { "hm1Shape", drawnShape }, { "hm1Sync", modOneBar }, { "hm1Trigger", syncTrigger }, { "hm1Smooth", 4 },
            { delayOn, 1 }, { delaySync, quarter }, { delayFeedback, 0.3f }, { delayMix, 0.12f }, { delayDuck, 0.6f },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.5f }, { reverbMix, 0.2f } }),
            { { harmonyLevel, HMod::m1, 0.6f } }),   // a closed step is 40 dB down
            0, { 0, 0, 0, -1,  0, 0, 0, -1,  0, -1, 0, -1,  0, 0, -1, -1,  0, 0, 0, -1,  0, 0, 0, -1,  0, -1, 0, -1,  0, -1, 0, 0 }),

        // Higher notes, bigger harmony: the pitch you sing (PT) brings the
        // harmony up as you climb, and puts it away when you go low.
        withMods (make ("Rising Harmony", G, 8, "Harmony,Modulators,Rock,Anthem", {
            { compOn, 1 }, { compType, revival }, { revivalCompression, -16 },
            { deessOn, 1 }, { deessAmount, 6 },
            { harmonyOn, 1 }, { harmonyLevel, -8 }, { "hv1Level", -2 }, { "hv2Level", -4 },
            { harmonyPtSmooth, 200 },
            { doublerOn, 1 }, { "dv1Gain", -9 }, { "dv2Gain", -9 },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.5f }, { reverbMix, 0.2f } }),
            { { harmonyLevel, HMod::pt, 0.5f } }),   // about 4 dB down on an A3, 10 dB up an octave higher

        // For a MIDI keyboard: play the chord, sing the words. How hard you
        // play sets each note's level, the mod wheel widens the chord, the
        // pitch wheel bends it two semitones. (Without MIDI: just your voice.)
        withMods (make ("Keyboard Choir", G, 9, "Harmony,Choir,Modulators,MIDI", {
            { rigLevel, -2 },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -18 },
            { deessOn, 1 }, { deessAmount, 6 },
            { harmonyOn, 1 }, { "hv1Active", 0 }, { "hv2Active", 0 },
            { harmonyNoteLevel, -8 }, { harmonyNoteWidth, 0.3f }, { harmonyNoteAttack, 40 }, { harmonyNoteRelease, 350 },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.6f }, { reverbMix, 0.24f } }),
            { { harmonyNoteLevel, HMod::velocity, 0.15f },     // soft keys about 10 dB quieter than hard ones
              { harmonyNoteWidth, HMod::modWheel, 0.7f },
              { harmonyGlobalPitch, HMod::pitchWheel, 2.0f / 24.0f } }),

        // ---- H: an album's vocal sounds ---------------------------------------
        // One record's range, from breathy layered cleans to fry screams and
        // low growls. Each is our reading of a sound on it (how such vocals
        // are commonly produced), not anyone's actual settings. The layers
        // come from the DOUBLER, not HARMONY (the owner's choice: the
        // doubler sounds better), and none needs the song's KEY.

        // A breathy, airy clean that opens a song: lifted highs, two soft
        // takes far out to the sides, a ping-pong echo and a big hall that
        // both duck under the words.
        make ("Breath-Lit Intro", H, 1, "Metal,Clean,Ambient,Delay,Reverb", {
            { compOn, 1 }, { compType, abyssComp }, { abyssPeakReduction, 50 }, { abyssGain, 14 }, { abyssEmphasis, 0.4f },
            { deessOn, 1 }, { deessFreq, 7500 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -3 }, { "eq250", -2 }, { "eq4k", 1 }, { "eq8k", 3 }, { "eq16k", 5 },
            { doublerOn, 1 }, { "dv1Gain", -10 }, { "dv2Gain", -10 }, { "dv1Pan", -1 }, { "dv2Pan", 1 },
            { delayOn, 1 }, { delaySync, dottedEighth }, { delayPingPong, 1 }, { delayFeedback, 0.4f }, { delayMix, 0.18f },
            { delayDuck, 0.7f }, { delayLowCut, 400 }, { delayHighCut, 7000 },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.75f }, { reverbMix, 0.3f }, { reverbPreDelay, 50 },
            { reverbDuck, 0.4f }, { reverbDecay, 1.3f }, { reverbBassRt, 0.6f } }),

        // A high, serrated fry scream: the lows kept tight and the rest torn
        // up (more the harder you push), mids forward so the words cut, two
        // tight takes and almost no room.
        make ("Serrated Fry Scream", H, 2, "Metal,Scream,Saturation,Mars", {
            { rigLevel, -6 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 26 }, { photonOutput, -13 }, { photonAttack, 5 }, { photonRelease, 5 }, { photonRatio, photonEight },
            { driveOn, 1 }, { marsBands, 1 }, { marsCross1, 400 },
            { "mb1Style", warmTubeStyle }, { "mb1Drive", 0.15f },
            { "mb2Style", brokenTubeStyle }, { "mb2Drive", 0.5f }, { "mb2Presence", 3 },
            { "mmSlot1Source", envelopeFollower1 }, { "mmSlot1Target", band2Drive }, { "mmSlot1Amount", 0.3f },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -4 }, { "eq1k", 2 }, { "eq2k", 3 }, { "eq4k", 1 }, { "eq8k", -3 },
            { doublerOn, 1 }, { "dv1Gain", -5 }, { "dv2Gain", -5 }, { "dv1Detune", 4 }, { "dv2Detune", -4 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.2f }, { reverbMix, 0.08f } }),

        // The big clean hook: four loose takes spread wide and an octave-down
        // take underneath for weight, a bright plate and a quiet quarter-note
        // echo.
        make ("Wounded Hook", H, 3, "Metal,Clean,Doubler", {
            { rigLevel, -3 },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -22 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -3 }, { "eq250", -1 }, { "eq4k", 2 }, { "eq8k", 3 }, { "eq16k", 3 },
            { doublerOn, 1 }, { "dv3On", 1 }, { "dv4On", 1 }, { doublerOutput, -4 },
            { "dv1Pan", -1 }, { "dv2Pan", 1 }, { "dv1Drift", 0.6f }, { "dv2Drift", 0.6f },
            { "dv3Octave", 1 }, { "dv3Gain", -12 }, { "dv3Pan", 0 }, { "dv3Detune", 0 },
            { "dv4Pan", -0.4f }, { "dv4Drift", 0.6f },
            { delayOn, 1 }, { delaySync, quarter }, { delayFeedback, 0.25f }, { delayMix, 0.1f }, { delayDuck, 0.7f },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.5f }, { reverbMix, 0.2f }, { reverbPreDelay, 30 } }),

        // A venomous verse scream: hard, mid-heavy and dry, squeezed with all
        // the buttons in so every syllable hits at the same level.
        make ("Venom Verse", H, 4, "Metal,Scream,Saturation,Photon", {
            { rigLevel, -6 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 24 }, { photonOutput, -12 }, { photonAttack, 5 }, { photonRelease, 6 }, { photonRatio, photonAll },
            { driveOn, 1 }, { "mb1Style", brokenTubeStyle }, { "mb1Drive", 0.6f }, { "mb1Mid", 3 }, { "mb1Presence", 2 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq500", 1 }, { "eq1k", 2 }, { "eq2k", 2 }, { "eq8k", -1 },
            { doublerOn, 1 }, { "dv1Gain", -6 }, { "dv2Gain", -6 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.15f }, { reverbMix, 0.06f } }),

        // Two screamers trading lines: your scream, a second take hard left,
        // and an octave-down throat on the right, so it sounds like two
        // different voices rather than one doubled.
        make ("Twin Throat Duet", H, 5, "Metal,Scream,Doubler,Pitch", {
            { rigLevel, -7 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 26 }, { photonOutput, -13 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, photonEight },
            { driveOn, 1 }, { "mb1Style", screamingAmpStyle }, { "mb1Drive", 0.45f }, { "mb1Treble", -2 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq250", 1 }, { "eq1k", 1 }, { "eq2k", 2 }, { "eq8k", -3 },
            { doublerOn, 1 }, { doublerDirectPan, 0.2f },
            { "dv1Gain", -3 }, { "dv1Pan", -1 }, { "dv1Detune", -12 }, { "dv1Delay", 28 },
            { "dv2On", 0 },
            { "dv3On", 1 }, { "dv3Octave", 1 }, { "dv3Gain", -8 }, { "dv3Pan", 0.8f }, { "dv3Detune", 0 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.25f }, { reverbMix, 0.08f } }),

        // A soaring clean belt: four takes wide, a shimmer echo glinting an
        // octave above on every repeat, and a long hall for the held notes.
        make ("Soaring Belt", H, 6, "Metal,Clean,Doubler,Delay,Reverb", {
            { rigLevel, -6 },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -22 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq2k", 1 }, { "eq4k", 2 }, { "eq8k", 3 }, { "eq16k", 3 },
            { doublerOn, 1 }, { "dv3On", 1 }, { "dv4On", 1 }, { "dv1Gain", -7 }, { "dv2Gain", -7 }, { doublerOutput, -3 },
            { delayOn, 1 }, { delayType, shimmer }, { delaySync, eighth }, { delayPingPong, 1 }, { delayFeedback, 0.35f },
            { delayMix, 0.13f }, { delayDuck, 0.7f },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.8f }, { reverbMix, 0.28f }, { reverbDecay, 1.4f },
            { reverbDuck, 0.3f } }),

        // Dreamy pop: smooth and close, a shimmering chorus on it, two soft
        // takes far out to the sides, a bright plate and a dotted-eighth echo.
        make ("Dream Pop Glow", H, 7, "Pop,Clean,Modulation,Doubler", {
            { rigLevel, 2 },
            { compOn, 1 }, { compType, abyssComp }, { abyssPeakReduction, 55 }, { abyssGain, 15 }, { abyssEmphasis, 0.3f },
            { deessOn, 1 }, { deessAmount, 6 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq250", -1 }, { "eq4k", 1 }, { "eq8k", 2 }, { "eq16k", 4 },
            { doublerOn, 1 }, { "dv1Gain", -9 }, { "dv2Gain", -9 }, { "dv1Pan", -1 }, { "dv2Pan", 1 },
            { "dv1Drift", 0.7f }, { "dv2Drift", 0.7f },
            { modOn, 1 }, { modType, dimension }, { modRate, 0.4f }, { modDepth, 0.4f }, { modMix, 0.3f },
            { delayOn, 1 }, { delaySync, dottedEighth }, { delayFeedback, 0.3f }, { delayMix, 0.14f }, { delayDuck, 0.6f },
            { delayHighCut, 6000 },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.55f }, { reverbMix, 0.22f }, { reverbPreDelay, 35 } }),

        // A tight, rhythmic snarl: fast and hard on the attack, saturated,
        // a short analog slap and almost no space, for riffs the voice
        // locks to.
        make ("Riff Snarl", H, 8, "Metal,Scream,Saturation,Delay", {
            { rigLevel, -5 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 24 }, { photonOutput, -12 }, { photonAttack, 7 }, { photonRelease, 6 }, { photonRatio, 3 },
            { driveOn, 1 }, { "mb1Style", saturatedTubeStyle }, { "mb1Drive", 0.55f }, { "mb1Presence", 3 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -3 }, { "eq1k", 2 }, { "eq2k", 2 }, { "eq4k", 2 }, { "eq8k", -2 },
            { doublerOn, 1 }, { "dv1Gain", -7 }, { "dv2Gain", -7 },
            { delayOn, 1 }, { delayType, analogDelay }, { delaySync, syncOff }, { delayTime, 90 }, { delayFeedback, 0.1f },
            { delayMix, 0.12f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.1f }, { reverbMix, 0.05f } }),

        // An ethereal haze: every word rising into an octave-up shimmer and a
        // slowly drifting cathedral, ducked so the line itself stays clear.
        make ("Ethereal Haze", H, 9, "Metal,Clean,Ambient,Delay,Reverb", {
            { rigLevel, -2 },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -18 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq8k", 2 }, { "eq16k", 4 },
            { doublerOn, 1 }, { "dv1Gain", -10 }, { "dv2Gain", -10 },
            { delayOn, 1 }, { delayType, shimmer }, { delaySync, quarter }, { delayFeedback, 0.45f }, { delayMix, 0.16f },
            { delayPingPong, 1 }, { delayDuck, 0.7f },
            { reverbOn, 1 }, { reverbType, cathedral }, { reverbSize, 0.7f }, { reverbMix, 0.32f }, { reverbPreDelay, 60 },
            { reverbDuck, 0.5f }, { reverbSpin, 1.8f }, { reverbWander, 1.8f } }),

        // A strange, synthetic voice: a vocoder synth singing your note (it
        // follows your pitch by itself) blended with your own voice, and a
        // quarter-note echo.
        make ("Glass Machine Voice", H, 10, "Electronic,Vocoder,Experimental", {
            { rigLevel, -2 },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -20 },
            { deessOn, 1 }, { deessAmount, 6 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq4k", 2 }, { "eq8k", 2 },
            { vocoderOn, 1 }, { vocoderMode, 0 }, { vocoderScale, 0.6f }, { vocoderLevel, -6 }, { vocoderDry, -2 },
            { delayOn, 1 }, { delaySync, quarter }, { delayFeedback, 0.3f }, { delayMix, 0.12f }, { delayDuck, 0.6f },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.45f }, { reverbMix, 0.16f } }),

        // A low cavern growl: a full octave-down take under the voice, the
        // lows pushed, the top rolled off, very dry and very heavy.
        make ("Cavern Growl", H, 11, "Metal,Scream,Growl,Pitch,Saturation", {
            { rigLevel, -9 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 28 }, { photonOutput, -16 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, photonAll },
            { driveOn, 1 }, { "mb1Style", screamingAmpStyle }, { "mb1Drive", 0.55f }, { "mb1Bass", 3 }, { "mb1Treble", -4 },
            { eqOn, 1 }, { "eq63", -2 }, { "eq125", 3 }, { "eq250", 2 }, { "eq2k", 1 }, { "eq8k", -5 }, { "eq16k", -8 },
            { doublerOn, 1 }, { "dv1On", 0 }, { "dv2On", 0 }, { "dv3On", 1 }, { "dv3Octave", 1 }, { "dv3Gain", -2 },
            { "dv3Pan", 0 }, { "dv3Detune", 0 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.2f }, { reverbMix, 0.05f } }),

        // A floating, oceanic clean: smooth levelling, a long tape echo
        // that drifts, and a big hall with the lows kept short so it never
        // turns to mud.
        make ("Ocean Drift", H, 12, "Metal,Clean,Ambient,Delay,Reverb", {
            { compOn, 1 }, { compType, abyssComp }, { abyssPeakReduction, 50 }, { abyssGain, 13 },
            { deessOn, 1 }, { deessAmount, 6 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq250", -1 }, { "eq8k", 2 }, { "eq16k", 3 },
            { doublerOn, 1 }, { "dv1Gain", -9 }, { "dv2Gain", -9 }, { "dv1Drift", 0.7f }, { "dv2Drift", 0.7f },
            { delayOn, 1 }, { delayType, tapeDelay }, { delaySync, quarter }, { delayFeedback, 0.45f }, { delayMix, 0.18f },
            { delayDuck, 0.6f }, { delayModDepth, 0.3f }, { delayHighCut, 5000 },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.85f }, { reverbMix, 0.3f }, { reverbPreDelay, 50 },
            { reverbDecay, 1.5f }, { reverbBassRt, 0.6f }, { reverbCrossover, 300 }, { reverbDuck, 0.4f } }),

        // A scream thrown into space: torn up and doubled like the others,
        // but sent into a long hall and echo that bloom in the gaps.
        make ("Tidal Scream", H, 13, "Metal,Scream,Saturation,Reverb,Delay", {
            { rigLevel, -3 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 26 }, { photonOutput, -13 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, photonEight },
            { driveOn, 1 }, { "mb1Style", brokenTubeStyle }, { "mb1Drive", 0.45f }, { "mb1Presence", 2 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -3 }, { "eq1k", 1 }, { "eq2k", 2 }, { "eq8k", -2 },
            { doublerOn, 1 }, { "dv1Gain", -5 }, { "dv2Gain", -5 },
            { delayOn, 1 }, { delaySync, quarter }, { delayPingPong, 1 }, { delayFeedback, 0.35f }, { delayMix, 0.14f },
            { delayDuck, 0.8f }, { delayHighCut, 5000 },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.75f }, { reverbMix, 0.25f }, { reverbDuck, 0.7f },
            { reverbBassRt, 0.5f } }),

        // A fragile, close ballad voice: gently levelled, warm, every breath
        // kept, in a small chamber with a soft tape echo.
        make ("Fragile Lullaby", H, 14, "Ballad,Clean,Intimate,Abyss", {
            { compOn, 1 }, { compType, abyssComp }, { abyssPeakReduction, 40 }, { abyssGain, 10 },
            { deessOn, 1 }, { deessFreq, 6500 }, { deessAmount, 6 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", 1 }, { "eq250", 1 }, { "eq2k", -1 }, { "eq8k", 1 }, { "eq16k", 2 },
            { delayOn, 1 }, { delayType, tapeDelay }, { delaySync, quarter }, { delayFeedback, 0.2f }, { delayMix, 0.1f },
            { delayDuck, 0.6f }, { delayHighCut, 4000 },
            { reverbOn, 1 }, { reverbType, chamber }, { reverbSize, 0.45f }, { reverbMix, 0.2f }, { reverbPreDelay, 20 } }),

        // A closing wall of voices: the lead in front, four loose takes and
        // an octave-down take spread wide behind it, a shimmer echo above and
        // a big hall.
        make ("Finale Wall", H, 15, "Metal,Clean,Doubler,Reverb", {
            { rigLevel, -5 },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -20 },
            { deessOn, 1 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq4k", 1 }, { "eq8k", 2 }, { "eq16k", 3 },
            { doublerOn, 1 }, { "dv3On", 1 }, { "dv4On", 1 }, { doublerOutput, -3 },
            { "dv1Pan", -1 }, { "dv2Pan", 1 }, { "dv1Drift", 0.8f }, { "dv2Drift", 0.8f }, { "dv4Drift", 0.8f },
            { "dv3Octave", 1 }, { "dv3Gain", -10 }, { "dv3Pan", 0 }, { "dv3Detune", 0 },
            { delayOn, 1 }, { delayType, shimmer }, { delaySync, quarter }, { delayFeedback, 0.3f }, { delayMix, 0.1f },
            { delayDuck, 0.7f },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.8f }, { reverbMix, 0.3f }, { reverbDecay, 1.3f },
            { reverbDuck, 0.3f } }),

        // ---- I: a second album's vocal sounds ---------------------------------
        // Modern progressive metal: a high, soaring tenor stacked into a wall
        // of takes, and screams double- and quad-tracked tight, as that style
        // is commonly produced. Every layer is the DOUBLER (no HARMONY: the
        // owner's choice), with the DELAY's Shimmer for any octave above.
        // Each is our reading of a sound, not anyone's settings.

        // A barrage of screams: four takes stacked tight around the centre,
        // torn up on two bands, everything squeezed with all the buttons in,
        // almost no room.
        make ("Barrage Scream", I, 1, "Metal,Prog,Scream,Doubler,Saturation", {
            { rigLevel, -4 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 24 }, { photonOutput, -12 }, { photonAttack, 5 }, { photonRelease, 6 }, { photonRatio, photonAll },
            { driveOn, 1 }, { marsBands, 1 }, { marsCross1, 400 },
            { "mb1Style", warmTubeStyle }, { "mb1Drive", 0.15f },
            { "mb2Style", brokenTubeStyle }, { "mb2Drive", 0.75f }, { "mb2Presence", 2 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -3 }, { "eq1k", 2 }, { "eq2k", 3 }, { "eq8k", 0 },
            { doublerOn, 1 }, { "dv3On", 1 }, { "dv4On", 1 }, { doublerOutput, -3 },
            { "dv1Pan", -0.5f }, { "dv2Pan", 0.5f }, { "dv3Pan", -0.25f }, { "dv4Pan", 0.25f },
            { "dv1Drift", 0.3f }, { "dv2Drift", 0.3f }, { "dv3Drift", 0.3f }, { "dv4Drift", 0.3f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.15f }, { reverbMix, 0.06f } }),

        // A rapid bark: a rhythmic, half-rapped shout, hard and dry,
        // two tight takes and a short slap, so fast lines stay clear.
        make ("Rapid Bark", I, 2, "Metal,Prog,Scream,Rap,Doubler", {
            { rigLevel, -5 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 24 }, { photonOutput, -12 }, { photonAttack, 7 }, { photonRelease, 7 }, { photonRatio, photonEight },
            { driveOn, 1 }, { "mb1Style", saturatedTubeStyle }, { "mb1Drive", 0.55f }, { "mb1Presence", 3 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -3 }, { "eq1k", 2 }, { "eq2k", 3 }, { "eq4k", 2 }, { "eq8k", -1 },
            { doublerOn, 1 }, { "dv1Gain", -6 }, { "dv2Gain", -6 }, { "dv1Delay", 14 }, { "dv2Delay", 19 },
            { delayOn, 1 }, { delayType, analogDelay }, { delaySync, syncOff }, { delayTime, 80 }, { delayFeedback, 0.08f },
            { delayMix, 0.1f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.1f }, { reverbMix, 0.04f } }),

        // A high tenor anthem: the big clean chorus, four takes spread wide
        // with a quiet octave-down take for body, a dotted-eighth echo and a
        // bright plate.
        make ("High Tenor Anthem", I, 3, "Metal,Prog,Clean,Doubler", {
            { rigLevel, -4 },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -22 },
            { deessOn, 1 }, { deessFreq, 7500 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -3 }, { "eq250", -2 }, { "eq2k", 1 }, { "eq4k", 2 }, { "eq8k", 3 }, { "eq16k", 3 },
            { doublerOn, 1 }, { "dv3On", 1 }, { "dv4On", 1 }, { doublerOutput, -3 },
            { "dv1Pan", -1 }, { "dv2Pan", 1 }, { "dv1Drift", 0.5f }, { "dv2Drift", 0.5f }, { "dv4Drift", 0.5f },
            { "dv3Octave", 1 }, { "dv3Gain", -14 }, { "dv3Pan", 0 }, { "dv3Detune", 0 },
            { delayOn, 1 }, { delaySync, dottedEighth }, { delayPingPong, 1 }, { delayFeedback, 0.3f }, { delayMix, 0.12f },
            { delayDuck, 0.7f }, { delayHighCut, 6000 },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.6f }, { reverbMix, 0.22f }, { reverbPreDelay, 35 } }),

        // A layered mid scream: the scream itself, two tight takes, and an
        // octave-down take underneath so the chorus screams sound huge.
        make ("Layered Mid Scream", I, 4, "Metal,Prog,Scream,Doubler,Pitch", {
            { rigLevel, -6 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 26 }, { photonOutput, -13 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, photonEight },
            { driveOn, 1 }, { "mb1Style", brokenTubeStyle }, { "mb1Drive", 0.7f }, { "mb1Mid", 2 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq1k", 2 }, { "eq2k", 2 }, { "eq8k", 0 },
            { doublerOn, 1 }, { "dv1Gain", -5 }, { "dv2Gain", -5 },
            { "dv3On", 1 }, { "dv3Octave", 1 }, { "dv3Gain", -10 }, { "dv3Pan", 0 }, { "dv3Detune", 0 },
            { delayOn, 1 }, { delaySync, quarter }, { delayFeedback, 0.2f }, { delayMix, 0.08f }, { delayDuck, 0.8f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.25f }, { reverbMix, 0.08f } }),

        // A silken falsetto: airy and light, a slow shimmering chorus, two
        // soft takes far out to the sides and a long hall.
        make ("Silken Falsetto", I, 5, "Prog,Clean,Ambient,Modulation,Doubler", {
            { rigLevel, 2 },
            { compOn, 1 }, { compType, abyssComp }, { abyssPeakReduction, 50 }, { abyssGain, 14 }, { abyssEmphasis, 0.4f },
            { deessOn, 1 }, { deessFreq, 7500 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -3 }, { "eq250", -2 }, { "eq8k", 3 }, { "eq16k", 5 },
            { doublerOn, 1 }, { "dv1Gain", -10 }, { "dv2Gain", -10 }, { "dv1Pan", -1 }, { "dv2Pan", 1 },
            { modOn, 1 }, { modType, dimension }, { modRate, 0.3f }, { modDepth, 0.4f }, { modMix, 0.25f },
            { delayOn, 1 }, { delaySync, quarter }, { delayFeedback, 0.35f }, { delayMix, 0.14f }, { delayDuck, 0.7f },
            { delayHighCut, 6000 },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.8f }, { reverbMix, 0.3f }, { reverbPreDelay, 50 },
            { reverbDuck, 0.4f }, { reverbDecay, 1.3f } }),

        // A gritty clean: sung, not screamed, but pushed into a tube until it
        // grits, doubled tight, for heavy verses.
        make ("Gritty Sung Edge", I, 6, "Metal,Prog,Saturation,Doubler", {
            { rigLevel, -3 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 20 }, { photonOutput, -10 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, 0 },
            { deessOn, 1 }, { deessAmount, 6 },
            { driveOn, 1 }, { "mb1Style", saturatedTubeStyle }, { "mb1Drive", 0.35f },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq1k", 2 }, { "eq2k", 2 }, { "eq4k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -5 }, { "dv2Gain", -5 },
            { delayOn, 1 }, { delayType, tapeDelay }, { delaySync, eighth }, { delayFeedback, 0.2f }, { delayMix, 0.1f },
            { delayDuck, 0.7f },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.35f }, { reverbMix, 0.12f } }),

        // A venomous shriek: a high, thin scream, the top pushed and torn up
        // harder the louder you go, doubled tight.
        make ("Venomous Shriek", I, 7, "Metal,Prog,Scream,Saturation,Mars", {
            { rigLevel, -6 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 26 }, { photonOutput, -13 }, { photonAttack, 5 }, { photonRelease, 6 }, { photonRatio, photonEight },
            { driveOn, 1 }, { marsBands, 1 }, { marsCross1, 900 },
            { "mb1Style", warmTubeStyle }, { "mb1Drive", 0.2f },
            { "mb2Style", brokenTubeStyle }, { "mb2Drive", 0.6f }, { "mb2Presence", 4 },
            { "mmSlot1Source", envelopeFollower1 }, { "mmSlot1Target", band2Drive }, { "mmSlot1Amount", 0.3f },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -6 }, { "eq250", -3 }, { "eq2k", 3 }, { "eq4k", 3 }, { "eq8k", -1 },
            { doublerOn, 1 }, { "dv1Gain", -5 }, { "dv2Gain", -5 }, { "dv1Detune", 5 }, { "dv2Detune", -5 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.2f }, { reverbMix, 0.07f } }),

        // An epic tenor swell: a long clean line rising into a Shimmer echo an
        // octave above and a slowly drifting cathedral.
        make ("Epic Tenor Swell", I, 8, "Prog,Clean,Ambient,Delay,Reverb", {
            { rigLevel, -2 },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -20 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq4k", 1 }, { "eq8k", 2 }, { "eq16k", 4 },
            { doublerOn, 1 }, { "dv1Gain", -8 }, { "dv2Gain", -8 }, { "dv1Drift", 0.7f }, { "dv2Drift", 0.7f },
            { delayOn, 1 }, { delayType, shimmer }, { delaySync, quarter }, { delayPingPong, 1 }, { delayFeedback, 0.45f },
            { delayMix, 0.15f }, { delayDuck, 0.7f },
            { reverbOn, 1 }, { reverbType, cathedral }, { reverbSize, 0.7f }, { reverbMix, 0.3f }, { reverbPreDelay, 60 },
            { reverbDuck, 0.5f }, { reverbSpin, 1.6f }, { reverbWander, 1.6f } }),

        // An epic low roar: a deep, guttural roar with a full octave-down
        // take under it, the lows pushed and the top rolled off, very dry.
        make ("Epic Low Roar", I, 9, "Metal,Prog,Scream,Growl,Pitch,Saturation", {
            { rigLevel, -9 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 28 }, { photonOutput, -16 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, photonAll },
            { driveOn, 1 }, { "mb1Style", screamingAmpStyle }, { "mb1Drive", 0.5f }, { "mb1Bass", 3 }, { "mb1Treble", -4 },
            { eqOn, 1 }, { "eq63", -2 }, { "eq125", 3 }, { "eq250", 2 }, { "eq2k", 1 }, { "eq8k", -5 }, { "eq16k", -8 },
            { doublerOn, 1 }, { "dv1Gain", -8 }, { "dv2Gain", -8 },
            { "dv3On", 1 }, { "dv3Octave", 1 }, { "dv3Gain", -2 }, { "dv3Pan", 0 }, { "dv3Detune", 0 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.2f }, { reverbMix, 0.05f } }),

        // A polished pop hook: radio-ready, bright and close, two tight takes,
        // a quarter-note echo and a short plate.
        make ("Pop Hook Polish", I, 10, "Pop,Prog,Clean,Doubler", {
            { rigLevel, -4 },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -22 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq250", -1 }, { "eq4k", 2 }, { "eq8k", 3 }, { "eq16k", 4 },
            { doublerOn, 1 }, { "dv1Gain", -6 }, { "dv2Gain", -6 }, { "dv1Detune", 5 }, { "dv2Detune", -5 },
            { delayOn, 1 }, { delaySync, quarter }, { delayFeedback, 0.25f }, { delayMix, 0.1f }, { delayDuck, 0.7f },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.4f }, { reverbMix, 0.16f }, { reverbPreDelay, 25 } }),

        // A dry groove shout: a mid-range shout locked to the riff, doubled
        // tight, no space at all.
        make ("Dry Groove Shout", I, 11, "Metal,Prog,Scream,Doubler,Photon", {
            { rigLevel, -5 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 24 }, { photonOutput, -12 }, { photonAttack, 6 }, { photonRelease, 6 }, { photonRatio, 2 },
            { driveOn, 1 }, { "mb1Style", americanAmpStyle }, { "mb1Drive", 0.6f }, { "mb1Presence", 2 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq250", -2 }, { "eq1k", 1 }, { "eq2k", 2 }, { "eq4k", 2 },
            { doublerOn, 1 }, { "dv1Gain", -4 }, { "dv2Gain", -4 }, { "dv1Detune", 4 }, { "dv2Detune", -4 } }),

        // A brutal wide scream: four takes, two hard left and right, the
        // others tucked in, torn up and squeezed: the heaviest wall.
        make ("Brutal Wide Scream", I, 12, "Metal,Prog,Scream,Doubler,Saturation", {
            { rigLevel, -7 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 26 }, { photonOutput, -13 }, { photonAttack, 5 }, { photonRelease, 5 }, { photonRatio, photonAll },
            { driveOn, 1 }, { "mb1Style", screamingAmpStyle }, { "mb1Drive", 0.55f }, { "mb1Presence", 2 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq500", 1 }, { "eq2k", 3 }, { "eq8k", -2 },
            { doublerOn, 1 }, { "dv3On", 1 }, { "dv4On", 1 }, { doublerOutput, -3 },
            { "dv1Pan", -1 }, { "dv2Pan", 1 }, { "dv3Pan", -0.3f }, { "dv4Pan", 0.3f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.2f }, { reverbMix, 0.06f } }),

        // A grinding verse scream: lower and rougher, grinding through the
        // mids, an octave-down shadow and a short slap.
        make ("Grinding Verse Scream", I, 13, "Metal,Prog,Scream,Saturation,Delay", {
            { rigLevel, -6 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 26 }, { photonOutput, -13 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, photonEight },
            { driveOn, 1 }, { "mb1Style", brokenTubeStyle }, { "mb1Drive", 0.65f }, { "mb1Mid", 3 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq250", 1 }, { "eq500", 2 }, { "eq1k", 2 }, { "eq8k", 0 },
            { doublerOn, 1 }, { "dv1Gain", -6 }, { "dv2Gain", -6 },
            { "dv3On", 1 }, { "dv3Octave", 1 }, { "dv3Gain", -14 }, { "dv3Pan", 0 }, { "dv3Detune", 0 },
            { delayOn, 1 }, { delayType, analogDelay }, { delaySync, syncOff }, { delayTime, 110 }, { delayFeedback, 0.12f },
            { delayMix, 0.1f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.2f }, { reverbMix, 0.06f } }),

        // A close lullaby tenor: soft, near the mic, gently levelled, one
        // faint take behind, in a small chamber.
        make ("Close Lullaby Tenor", I, 14, "Ballad,Clean,Intimate,Abyss", {
            { compOn, 1 }, { compType, abyssComp }, { abyssPeakReduction, 40 }, { abyssGain, 10 },
            { deessOn, 1 }, { deessFreq, 6500 }, { deessAmount, 6 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", 1 }, { "eq250", 1 }, { "eq2k", -1 }, { "eq8k", 1 }, { "eq16k", 2 },
            { doublerOn, 1 }, { "dv1Gain", -14 }, { "dv2On", 0 }, { "dv1Pan", 0.3f },
            { delayOn, 1 }, { delayType, tapeDelay }, { delaySync, quarter }, { delayFeedback, 0.2f }, { delayMix, 0.09f },
            { delayDuck, 0.6f }, { delayHighCut, 4000 },
            { reverbOn, 1 }, { reverbType, chamber }, { reverbSize, 0.45f }, { reverbMix, 0.2f }, { reverbPreDelay, 20 } }),

        // The final ascent: the closing wall, four loose takes and an
        // octave-down take spread wide, a Shimmer echo above, a huge hall.
        make ("Final Ascent Wall", I, 15, "Prog,Clean,Doubler,Reverb", {
            { rigLevel, -5 },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -20 },
            { deessOn, 1 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq4k", 1 }, { "eq8k", 2 }, { "eq16k", 3 },
            { doublerOn, 1 }, { "dv3On", 1 }, { "dv4On", 1 }, { doublerOutput, -3 },
            { "dv1Pan", -1 }, { "dv2Pan", 1 }, { "dv1Drift", 0.8f }, { "dv2Drift", 0.8f }, { "dv4Drift", 0.8f },
            { "dv3Octave", 1 }, { "dv3Gain", -10 }, { "dv3Pan", 0 }, { "dv3Detune", 0 },
            { delayOn, 1 }, { delayType, shimmer }, { delaySync, quarter }, { delayFeedback, 0.35f }, { delayMix, 0.12f },
            { delayDuck, 0.7f },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.9f }, { reverbMix, 0.32f }, { reverbDecay, 1.5f },
            { reverbDuck, 0.3f } }),

        // ---- J: a third album's vocal sounds ----------------------------------
        // Early-2000s space rock: a voice that's half sung, half shouted,
        // often filtered or treated like a signal from far away, under huge
        // shouted choruses, with synths and long spaces around it. Layers
        // are the DOUBLER (no HARMONY: the owner's choice); the treated
        // voices come from LO-FI, MODULATION, GLITCH and the VOCODER. Each is
        // our reading of a sound, not anyone's settings.

        // A cold signal verse: the voice half through a phone line, half
        // itself, doubled tight, a short echo: a verse heard from far away.
        make ("Cold Signal Verse", J, 1, "Rock,Space,Lo-Fi,Doubler", {
            { rigLevel, -4 },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -20 },
            { deessOn, 1 }, { deessAmount, 6 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -4 }, { "eq1k", 2 }, { "eq2k", 2 }, { "eq8k", -2 },
            { doublerOn, 1 }, { "dv1Gain", -8 }, { "dv2Gain", -8 },
            { delayOn, 1 }, { delayType, analogDelay }, { delaySync, eighth }, { delayFeedback, 0.25f }, { delayMix, 0.12f },
            { delayDuck, 0.6f }, { delayHighCut, 4000 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.3f }, { reverbMix, 0.1f },
            { lofiOn, 1 }, { lofiType, telephone }, { lofiAmount, 5 }, { lofiNoise, 1 }, { lofiMix, 0.5f } }),

        // A rally-cry chorus: shouted from the chest, pushed into a tube until
        // it tears, four takes across, a bright plate behind.
        make ("Rally Cry Chorus", J, 2, "Rock,Space,Scream,Doubler,Saturation", {
            { rigLevel, -5 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 24 }, { photonOutput, -12 }, { photonAttack, 5 }, { photonRelease, 6 }, { photonRatio, photonEight },
            { driveOn, 1 }, { "mb1Style", brokenTubeStyle }, { "mb1Drive", 0.6f }, { "mb1Presence", 3 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -3 }, { "eq1k", 2 }, { "eq2k", 3 }, { "eq4k", 1 },
            { doublerOn, 1 }, { "dv3On", 1 }, { "dv4On", 1 }, { doublerOutput, -3 },
            { "dv1Pan", -0.8f }, { "dv2Pan", 0.8f }, { "dv3Pan", -0.3f }, { "dv4Pan", 0.3f },
            { delayOn, 1 }, { delaySync, quarter }, { delayFeedback, 0.2f }, { delayMix, 0.08f }, { delayDuck, 0.8f },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.45f }, { reverbMix, 0.14f } }),

        // An orbit lead: a sung rock lead with a little grit, doubled wide, a
        // dotted-eighth echo bouncing side to side and a plate.
        make ("Orbit Lead", J, 3, "Rock,Space,Doubler,Delay", {
            { rigLevel, -1 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 18 }, { photonOutput, -9 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, 0 },
            { deessOn, 1 }, { deessAmount, 6 },
            { driveOn, 1 }, { "mb1Style", warmTubeStyle }, { "mb1Drive", 0.3f },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq1k", 1 }, { "eq2k", 2 }, { "eq4k", 1 }, { "eq8k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -6 }, { "dv2Gain", -6 }, { "dv1Pan", -0.9f }, { "dv2Pan", 0.9f },
            { delayOn, 1 }, { delaySync, dottedEighth }, { delayPingPong, 1 }, { delayFeedback, 0.3f }, { delayMix, 0.13f },
            { delayDuck, 0.7f }, { delayHighCut, 6000 },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.5f }, { reverbMix, 0.16f }, { reverbPreDelay, 30 } }),

        // A breath-close whisper: almost spoken, right on the mic, gently
        // levelled, one faint take behind and a small chamber.
        make ("Breath Close Whisper", J, 4, "Space,Clean,Intimate,Abyss", {
            { rigLevel, 1 },
            { compOn, 1 }, { compType, abyssComp }, { abyssPeakReduction, 50 }, { abyssGain, 12 },
            { deessOn, 1 }, { deessFreq, 6500 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -1 }, { "eq250", 1 }, { "eq2k", -1 }, { "eq8k", 1 }, { "eq16k", 2 },
            { doublerOn, 1 }, { "dv1Gain", -14 }, { "dv2On", 0 }, { "dv1Pan", -0.4f },
            { delayOn, 1 }, { delayType, tapeDelay }, { delaySync, quarter }, { delayFeedback, 0.25f }, { delayMix, 0.1f },
            { delayDuck, 0.6f }, { delayHighCut, 4000 },
            { reverbOn, 1 }, { reverbType, chamber }, { reverbSize, 0.4f }, { reverbMix, 0.18f }, { reverbPreDelay, 20 } }),

        // A static-storm scream: torn up on two bands, a little stutter and
        // grain behind it from GLITCH, doubled tight: a scream breaking up.
        make ("Static Storm Scream", J, 5, "Rock,Space,Scream,Saturation,Experimental", {
            { rigLevel, -3 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 26 }, { photonOutput, -13 }, { photonAttack, 5 }, { photonRelease, 6 }, { photonRatio, photonAll },
            { driveOn, 1 }, { marsBands, 1 }, { marsCross1, 600 },
            { "mb1Style", warmTubeStyle }, { "mb1Drive", 0.2f },
            { "mb2Style", brokenTubeStyle }, { "mb2Drive", 0.7f }, { "mb2Presence", 3 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -3 }, { "eq1k", 2 }, { "eq2k", 3 }, { "eq8k", 0 },
            { glitchOn, 1 }, { glitchMode, texturizer }, { glitchMix, 0.2f },
            { doublerOn, 1 }, { "dv1Gain", -5 }, { "dv2Gain", -5 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.25f }, { reverbMix, 0.08f } }),

        // A hollow chant: low and moody, a slow phaser hollowing it out, a
        // slapback and a room, a little dark on top.
        make ("Hollow Chant", J, 6, "Rock,Space,Modulation,Delay", {
            { rigLevel, -2 },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -18 },
            { deessOn, 1 }, { deessAmount, 6 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq250", 1 }, { "eq500", 2 }, { "eq1k", 1 }, { "eq8k", -3 }, { "eq16k", -4 },
            { doublerOn, 1 }, { "dv1Gain", -10 }, { "dv2Gain", -10 },
            { modOn, 1 }, { modType, phaser }, { modRate, 0.25f }, { modDepth, 0.6f }, { modMix, 0.45f },
            { delayOn, 1 }, { delayType, analogDelay }, { delaySync, syncOff }, { delayTime, 120 }, { delayFeedback, 0.15f },
            { delayMix, 0.12f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.45f }, { reverbMix, 0.14f } }),

        // A satellite ballad: a soft sung line, two takes far out to the
        // sides, a quarter-note echo and a long hall.
        make ("Satellite Ballad", J, 7, "Space,Clean,Ballad,Doubler,Reverb", {
            { rigLevel, -4 },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -20 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq4k", 1 }, { "eq8k", 2 }, { "eq16k", 3 },
            { doublerOn, 1 }, { "dv1Gain", -9 }, { "dv2Gain", -9 }, { "dv1Pan", -1 }, { "dv2Pan", 1 },
            { "dv1Drift", 0.6f }, { "dv2Drift", 0.6f },
            { delayOn, 1 }, { delaySync, quarter }, { delayFeedback, 0.35f }, { delayMix, 0.13f }, { delayDuck, 0.7f },
            { delayHighCut, 5000 },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.8f }, { reverbMix, 0.28f }, { reverbPreDelay, 45 },
            { reverbDuck, 0.4f }, { reverbDecay, 1.3f } }),

        // A starfield vocoder: a synth singing your note (it follows your
        // pitch by itself), blended under your voice, a ping-pong echo and a
        // cathedral: a voice from the dark between the synths.
        make ("Starfield Vocoder", J, 8, "Space,Electronic,Vocoder,Ambient", {
            { rigLevel, -2 },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -20 },
            { deessOn, 1 }, { deessAmount, 6 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq4k", 1 }, { "eq8k", 2 },
            { vocoderOn, 1 }, { vocoderMode, 0 }, { vocoderScale, 0.6f }, { vocoderLevel, -6 }, { vocoderDry, -4 },
            { "vocOsc1Wave", 4 }, { "vocOsc1Shape", 0.4f },
            { delayOn, 1 }, { delaySync, quarter }, { delayPingPong, 1 }, { delayFeedback, 0.35f }, { delayMix, 0.14f },
            { delayDuck, 0.6f },
            { reverbOn, 1 }, { reverbType, cathedral }, { reverbSize, 0.65f }, { reverbMix, 0.26f }, { reverbPreDelay, 40 } }),

        // A marching shout: a hard, rhythmic shout locked to the drums,
        // doubled tight and kept dry.
        make ("Marching Shout", J, 9, "Rock,Space,Scream,Doubler,Photon", {
            { rigLevel, -5 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 24 }, { photonOutput, -12 }, { photonAttack, 6 }, { photonRelease, 6 }, { photonRatio, photonEight },
            { driveOn, 1 }, { "mb1Style", americanAmpStyle }, { "mb1Drive", 0.6f }, { "mb1Presence", 3 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq250", -2 }, { "eq1k", 1 }, { "eq2k", 2 }, { "eq4k", 2 },
            { doublerOn, 1 }, { "dv1Gain", -4 }, { "dv2Gain", -4 }, { "dv1Detune", 4 }, { "dv2Detune", -4 },
            { delayOn, 1 }, { delayType, analogDelay }, { delaySync, syncOff }, { delayTime, 90 }, { delayFeedback, 0.08f },
            { delayMix, 0.08f } }),

        // A twilight tenor: a gentle clean voice with a slow, wide chorus,
        // a soft echo and a warm hall.
        make ("Twilight Tenor", J, 10, "Space,Clean,Modulation,Reverb", {
            { rigLevel, 1 },
            { compOn, 1 }, { compType, abyssComp }, { abyssPeakReduction, 45 }, { abyssGain, 12 }, { abyssEmphasis, 0.3f },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -2 }, { "eq8k", 2 }, { "eq16k", 3 },
            { doublerOn, 1 }, { "dv1Gain", -12 }, { "dv2Gain", -12 },
            { modOn, 1 }, { modType, dimension }, { modRate, 0.3f }, { modDepth, 0.4f }, { modMix, 0.25f },
            { delayOn, 1 }, { delayType, tapeDelay }, { delaySync, quarter }, { delayFeedback, 0.3f }, { delayMix, 0.12f },
            { delayDuck, 0.6f }, { delayHighCut, 5000 },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.7f }, { reverbMix, 0.25f }, { reverbPreDelay, 40 } }),

        // A deep-space drift: the most spacious of the bank, a Shimmer echo
        // an octave above and a slowly turning cathedral.
        make ("Deep Space Drift", J, 11, "Space,Clean,Ambient,Delay,Reverb", {
            { rigLevel, -2 },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -20 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq4k", 1 }, { "eq8k", 2 }, { "eq16k", 3 },
            { doublerOn, 1 }, { "dv1Gain", -9 }, { "dv2Gain", -9 }, { "dv1Drift", 0.7f }, { "dv2Drift", 0.7f },
            { delayOn, 1 }, { delayType, shimmer }, { delaySync, quarter }, { delayPingPong, 1 }, { delayFeedback, 0.45f },
            { delayMix, 0.15f }, { delayDuck, 0.7f },
            { reverbOn, 1 }, { reverbType, cathedral }, { reverbSize, 0.75f }, { reverbMix, 0.32f }, { reverbPreDelay, 60 },
            { reverbDuck, 0.5f }, { reverbSpin, 1.8f }, { reverbWander, 1.8f } }),

        // A machine pulse: an industrial voice, gritty, a slow jet flanger
        // and a crushed copy of itself blended in.
        make ("Machine Pulse", J, 12, "Rock,Space,Industrial,Saturation,Modulation,Lo-Fi", {
            { rigLevel, -4 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 20 }, { photonOutput, -10 }, { photonAttack, 5 }, { photonRelease, 6 }, { photonRatio, photonEight },
            { driveOn, 1 }, { "mb1Style", saturatedTubeStyle }, { "mb1Drive", 0.4f },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -3 }, { "eq1k", 2 }, { "eq2k", 2 },
            { doublerOn, 1 }, { "dv1Gain", -7 }, { "dv2Gain", -7 },
            { modOn, 1 }, { modType, flanger }, { modRate, 0.15f }, { modDepth, 0.7f }, { modManual, 5 }, { modRes, 6 }, { modMix, 0.4f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.3f }, { reverbMix, 0.08f },
            { lofiOn, 1 }, { lofiType, bitcrusher }, { lofiAmount, 4 }, { lofiNoise, 0 }, { lofiMix, 0.35f } }),

        // A buried tape voice: far off and worn, as if found on an old tape
        // after the record seems to have ended, in a dim room.
        make ("Buried Tape Voice", J, 13, "Space,Lo-Fi,Experimental", {
            { rigLevel, -2 },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -18 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -3 }, { "eq4k", -2 }, { "eq8k", -4 },
            { doublerOn, 1 }, { "dv1Gain", -12 }, { "dv2Gain", -12 },
            { delayOn, 1 }, { delayType, tapeDelay }, { delaySync, quarter }, { delayFeedback, 0.3f }, { delayMix, 0.12f },
            { delayHighCut, 3500 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.55f }, { reverbMix, 0.2f },
            { lofiOn, 1 }, { lofiType, cassette }, { lofiAmount, 5 }, { lofiNoise, 4 }, { lofiMix, 0.7f } }),

        // A tremor growl: the heaviest moments, a full octave-down take under
        // the voice, the lows pushed, the top rolled off, dry.
        make ("Tremor Growl", J, 14, "Rock,Space,Scream,Growl,Pitch,Saturation", {
            { rigLevel, -9 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 28 }, { photonOutput, -16 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, photonAll },
            { driveOn, 1 }, { "mb1Style", screamingAmpStyle }, { "mb1Drive", 0.5f }, { "mb1Bass", 3 }, { "mb1Treble", -4 },
            { eqOn, 1 }, { "eq63", -2 }, { "eq125", 3 }, { "eq250", 2 }, { "eq2k", 1 }, { "eq8k", -5 }, { "eq16k", -8 },
            { doublerOn, 1 }, { "dv1Gain", -8 }, { "dv2Gain", -8 },
            { "dv3On", 1 }, { "dv3Octave", 1 }, { "dv3Gain", -2 }, { "dv3Pan", 0 }, { "dv3Detune", 0 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.25f }, { reverbMix, 0.06f } }),

        // A horizon wall: the closing build, four loose takes and an
        // octave-down take spread wide, a Shimmer echo above and a huge hall.
        make ("Horizon Wall", J, 15, "Rock,Space,Clean,Doubler,Reverb", {
            { rigLevel, -5 },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -20 },
            { deessOn, 1 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq4k", 1 }, { "eq8k", 2 }, { "eq16k", 3 },
            { doublerOn, 1 }, { "dv3On", 1 }, { "dv4On", 1 }, { doublerOutput, -3 },
            { "dv1Pan", -1 }, { "dv2Pan", 1 }, { "dv1Drift", 0.8f }, { "dv2Drift", 0.8f }, { "dv4Drift", 0.8f },
            { "dv3Octave", 1 }, { "dv3Gain", -10 }, { "dv3Pan", 0 }, { "dv3Detune", 0 },
            { delayOn, 1 }, { delayType, shimmer }, { delaySync, quarter }, { delayFeedback, 0.35f }, { delayMix, 0.12f },
            { delayDuck, 0.7f },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.9f }, { reverbMix, 0.32f }, { reverbDecay, 1.5f },
            { reverbDuck, 0.3f } }),

        // ---- K: a fourth album's vocal sounds ---------------------------------
        // Mid-2000s pop-rock turning darker, for a bright, slightly nasal
        // mezzo voice with its own rasp. Kept restrained on purpose (the owner
        // records a singer with that tone and asked that the effects never
        // swamp it): the voice dry and upfront, the choruses double-tracked
        // strong and tight (the owner: "the strong double effect she uses a
        // lot especially for choruses": takes about 2 dB under the lead, just off
        // centre, a few cents apart, so they fuse into one thick voice; the
        // owner's example: the chorus of the song behind K08), grit a touch or
        // none (the singer's own rasp carries it), short rooms and plates,
        // echoes tucked under and ducked. Layers are the DOUBLER (no HARMONY).
        // Each is our reading of a sound, not anyone's settings.

        // A restless verse: the voice alone and close, levelled firmly, a
        // faint tight take behind it and a small room.
        make ("Restless Verse", K, 1, "Rock,Pop,Clean,Doubler", {
            { rigLevel, 3 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 14 }, { photonOutput, -7 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, 0 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 6 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq250", -1 }, { "eq4k", 1 }, { "eq8k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -14 }, { "dv2On", 0 }, { "dv1Pan", 0.2f }, { "dv1Drift", 0.2f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.25f }, { reverbMix, 0.07f } }),

        // A storm belt chorus: the big chorus belted with the strong,
        // tight double: two takes 2 dB under the lead, just off centre,
        // so the three sound as one thick voice, and a quieter pair out wide.
        // A breath of tape warmth, a short plate, an echo kept under.
        make ("Storm Belt Chorus", K, 2, "Rock,Pop,Doubler", {
            { rigLevel, -2 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 18 }, { photonOutput, -9 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, 0 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 7 },
            { driveOn, 1 }, { "mb1Style", warmTapeStyle }, { "mb1Drive", 0.12f },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq250", -1 }, { "eq2k", 1 }, { "eq4k", 1 }, { "eq8k", 1 },
            { doublerOn, 1 }, { "dv3On", 1 }, { "dv4On", 1 },
            { "dv1Gain", -2 }, { "dv2Gain", -2 }, { "dv1Pan", -0.45f }, { "dv2Pan", 0.45f },
            { "dv1Detune", 7 }, { "dv2Detune", -7 }, { "dv1Delay", 14 }, { "dv2Delay", 22 },
            { "dv1Drift", 0.15f }, { "dv2Drift", 0.15f },
            { "dv3Gain", -9 }, { "dv4Gain", -9 }, { "dv3Pan", -1 }, { "dv4Pan", 1 },
            { "dv3Drift", 0.3f }, { "dv4Drift", 0.3f },
            { delayOn, 1 }, { delaySync, eighth }, { delayFeedback, 0.15f }, { delayMix, 0.05f }, { delayDuck, 0.8f },
            { delayHighCut, 5000 },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.4f }, { reverbMix, 0.1f } }),

        // A twin-heart chorus: a sweet, clean pop chorus with the strong
        // double (two takes nearly as loud as the lead), a short plate, a
        // faint quarter-note echo.
        make ("Twin Heart Chorus", K, 3, "Pop,Clean,Doubler", {
            { rigLevel, -1.5f },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -16 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq250", -1 }, { "eq4k", 1 }, { "eq8k", 2 },
            { doublerOn, 1 }, { "dv1Gain", -2 }, { "dv2Gain", -2 }, { "dv1Pan", -0.45f }, { "dv2Pan", 0.45f },
            { "dv1Detune", 7 }, { "dv2Detune", -7 }, { "dv1Delay", 14 }, { "dv2Delay", 22 },
            { "dv1Drift", 0.15f }, { "dv2Drift", 0.15f },
            { delayOn, 1 }, { delaySync, quarter }, { delayFeedback, 0.2f }, { delayMix, 0.05f }, { delayDuck, 0.8f },
            { delayHighCut, 5000 },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.4f }, { reverbMix, 0.11f }, { reverbPreDelay, 20 } }),

        // A brushed-off hook: a punchy, forward pop-rock lead with attitude,
        // doubled tight in the middle, a quick slapback.
        make ("Brushed Off Hook", K, 4, "Rock,Pop,Clean,Doubler,Photon", {
            { compOn, 1 }, { compType, photonComp }, { photonInput, 16 }, { photonOutput, -8 }, { photonAttack, 5 }, { photonRelease, 6 }, { photonRatio, 0 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq250", -1 }, { "eq2k", 1 }, { "eq4k", 1 }, { "eq8k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -4 }, { "dv2Gain", -4 }, { "dv1Pan", -0.3f }, { "dv2Pan", 0.3f },
            { "dv1Detune", 4 }, { "dv2Detune", -4 }, { "dv1Drift", 0.2f }, { "dv2Drift", 0.2f },
            { delayOn, 1 }, { delayType, analogDelay }, { delaySync, syncOff }, { delayTime, 90 }, { delayFeedback, 0.08f },
            { delayMix, 0.06f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.25f }, { reverbMix, 0.06f } }),

        // A playground chant: a bratty, shouted gang chant, four takes spread
        // across, a little crunch, almost dry.
        make ("Playground Chant", K, 5, "Rock,Punk,Pop,Doubler", {
            { rigLevel, 1.5f },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 18 }, { photonOutput, -9 }, { photonAttack, 5 }, { photonRelease, 6 }, { photonRatio, photonEight },
            { deessOn, 1 }, { deessAmount, 6 },
            { driveOn, 1 }, { "mb1Style", crunchyAmpStyle }, { "mb1Drive", 0.2f },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq250", -1 }, { "eq1k", 1 }, { "eq2k", 1 },
            { doublerOn, 1 }, { "dv3On", 1 }, { "dv4On", 1 }, { doublerOutput, -3 },
            { "dv1Gain", -6 }, { "dv2Gain", -6 }, { "dv3Gain", -8 }, { "dv4Gain", -8 },
            { "dv1Pan", -1 }, { "dv2Pan", 1 }, { "dv3Pan", -0.5f }, { "dv4Pan", 0.5f },
            { "dv1Drift", 0.4f }, { "dv2Drift", 0.4f }, { "dv3Drift", 0.4f }, { "dv4Drift", 0.4f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.3f }, { reverbMix, 0.06f } }),

        // A hushed diary verse: soft and breathy on the mic, levelled gently
        // by the opto, a faint take behind, a small chamber.
        make ("Hushed Diary Verse", K, 6, "Pop,Clean,Intimate,Abyss", {
            { rigLevel, 2 },
            { compOn, 1 }, { compType, abyssComp }, { abyssPeakReduction, 35 }, { abyssGain, 9 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq4k", 1 }, { "eq8k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -16 }, { "dv2On", 0 }, { "dv1Pan", -0.2f }, { "dv1Drift", 0.2f },
            { delayOn, 1 }, { delayType, tapeDelay }, { delaySync, quarter }, { delayFeedback, 0.15f }, { delayMix, 0.05f },
            { delayDuck, 0.7f }, { delayHighCut, 4500 },
            { reverbOn, 1 }, { reverbType, chamber }, { reverbSize, 0.35f }, { reverbMix, 0.11f }, { reverbPreDelay, 15 } }),

        // A bittersweet anthem: a catchy chorus belted over guitars with the
        // strong double just off centre and a quieter pair out wide, a
        // dotted-eighth echo under it and a short plate.
        make ("Bittersweet Anthem", K, 7, "Rock,Pop,Clean,Doubler", {
            { rigLevel, -1.5f },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 18 }, { photonOutput, -9 }, { photonAttack, 4 }, { photonRelease, 6 }, { photonRatio, 0 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq250", -1 }, { "eq2k", 1 }, { "eq4k", 1 }, { "eq8k", 1 },
            { doublerOn, 1 }, { "dv3On", 1 }, { "dv4On", 1 },
            { "dv1Gain", -2 }, { "dv2Gain", -2 }, { "dv1Pan", -0.45f }, { "dv2Pan", 0.45f },
            { "dv1Detune", 7 }, { "dv2Detune", -7 }, { "dv1Delay", 14 }, { "dv2Delay", 22 },
            { "dv1Drift", 0.15f }, { "dv2Drift", 0.15f },
            { "dv3Gain", -9 }, { "dv4Gain", -9 }, { "dv3Pan", -1 }, { "dv4Pan", 1 },
            { "dv3Drift", 0.3f }, { "dv4Drift", 0.3f },
            { delayOn, 1 }, { delaySync, dottedEighth }, { delayFeedback, 0.2f }, { delayMix, 0.05f }, { delayDuck, 0.8f },
            { delayHighCut, 5000 },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.4f }, { reverbMix, 0.1f } }),

        // A lonely street echo: the moodier chorus with the strong double at
        // its clearest (the owner's example of the effect): two takes nearly
        // as loud as the lead, just off centre, tight and a few cents apart,
        // so the three fuse into one thick voice, a soft tape echo trailing
        // it and a plate, both kept under the voice.
        make ("Lonely Street Echo", K, 8, "Rock,Pop,Clean,Doubler", {
            { rigLevel, -0.5f },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 16 }, { photonOutput, -8 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, 0 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq250", -1 }, { "eq2k", 1 }, { "eq4k", 1 }, { "eq8k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -2 }, { "dv2Gain", -2 }, { "dv1Pan", -0.45f }, { "dv2Pan", 0.45f },
            { "dv1Detune", 7 }, { "dv2Detune", -7 }, { "dv1Delay", 14 }, { "dv2Delay", 22 },
            { "dv1Drift", 0.15f }, { "dv2Drift", 0.15f },
            { delayOn, 1 }, { delayType, tapeDelay }, { delaySync, quarter }, { delayFeedback, 0.2f }, { delayMix, 0.06f },
            { delayDuck, 0.8f }, { delayHighCut, 4500 },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.45f }, { reverbMix, 0.11f } }),

        // A dark-room wail: the most dramatic one, belted hard with a little
        // tube warmth, three takes, a darker hall kept back.
        make ("Dark Room Wail", K, 9, "Rock,Doubler,Reverb", {
            { rigLevel, -2 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 20 }, { photonOutput, -10 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, photonEight },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 7 },
            { driveOn, 1 }, { "mb1Style", warmTubeStyle }, { "mb1Drive", 0.15f },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq250", -1 }, { "eq1k", 1 }, { "eq2k", 1 },
            { doublerOn, 1 }, { "dv3On", 1 }, { "dv1Gain", -4 }, { "dv2Gain", -4 }, { "dv3Gain", -9 },
            { "dv1Pan", -0.6f }, { "dv2Pan", 0.6f }, { "dv3Pan", 0 },
            { "dv1Detune", 6 }, { "dv2Detune", -6 }, { "dv1Drift", 0.15f }, { "dv2Drift", 0.15f },
            { delayOn, 1 }, { delaySync, quarter }, { delayFeedback, 0.2f }, { delayMix, 0.05f }, { delayDuck, 0.8f },
            { delayHighCut, 4500 },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.6f }, { reverbMix, 0.13f }, { reverbTone, 4 } }),

        // A shrugging groove: an upbeat, easy-going lead, doubled tight, a
        // faint slap and a small room, so the rhythm of the words leads.
        make ("Shrugging Groove", K, 10, "Pop,Rock,Clean,Doubler", {
            { rigLevel, -2 },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -18 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq250", -1 }, { "eq4k", 1 }, { "eq8k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -5 }, { "dv2Gain", -5 }, { "dv1Pan", -0.4f }, { "dv2Pan", 0.4f },
            { "dv1Detune", 4 }, { "dv2Detune", -4 }, { "dv1Drift", 0.2f }, { "dv2Drift", 0.2f },
            { delayOn, 1 }, { delayType, analogDelay }, { delaySync, syncOff }, { delayTime, 100 }, { delayFeedback, 0.08f },
            { delayMix, 0.05f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.25f }, { reverbMix, 0.06f } }),

        // A shattered-glass ballad: a fragile, clean voice, two soft takes
        // wide, a soft echo and a hall, nothing swirling.
        make ("Shattered Glass Ballad", K, 11, "Pop,Ballad,Clean,Doubler,Reverb", {
            { rigLevel, 1 },
            { compOn, 1 }, { compType, abyssComp }, { abyssPeakReduction, 40 }, { abyssGain, 10 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq4k", 1 }, { "eq8k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -11 }, { "dv2Gain", -11 }, { "dv1Pan", -1 }, { "dv2Pan", 1 },
            { "dv1Drift", 0.4f }, { "dv2Drift", 0.4f },
            { delayOn, 1 }, { delaySync, quarter }, { delayFeedback, 0.25f }, { delayMix, 0.06f }, { delayDuck, 0.7f },
            { delayHighCut, 4500 },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.6f }, { reverbMix, 0.16f }, { reverbPreDelay, 30 } }),

        // A snarling punk shout: fast and angry, a little amp crunch, doubled
        // tight, a slap and nothing else.
        make ("Snarling Punk Shout", K, 12, "Rock,Punk,Doubler,Saturation", {
            { rigLevel, -3 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 20 }, { photonOutput, -10 }, { photonAttack, 5 }, { photonRelease, 6 }, { photonRatio, photonEight },
            { deessOn, 1 }, { deessAmount, 6 },
            { driveOn, 1 }, { "mb1Style", americanAmpStyle }, { "mb1Drive", 0.3f },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq250", -1 }, { "eq1k", 1 }, { "eq2k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -4 }, { "dv2Gain", -4 }, { "dv1Pan", -0.5f }, { "dv2Pan", 0.5f },
            { "dv1Drift", 0.2f }, { "dv2Drift", 0.2f },
            { delayOn, 1 }, { delayType, analogDelay }, { delaySync, syncOff }, { delayTime, 85 }, { delayFeedback, 0.06f },
            { delayMix, 0.05f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.2f }, { reverbMix, 0.05f } }),

        // A piano lament: a tender ballad voice, close and warm, one faint
        // take, a slow soft echo and a hall.
        make ("Piano Lament", K, 13, "Ballad,Clean,Intimate,Reverb", {
            { rigLevel, 1 },
            { compOn, 1 }, { compType, abyssComp }, { abyssPeakReduction, 35 }, { abyssGain, 9 },
            { deessOn, 1 }, { deessFreq, 6500 }, { deessAmount, 6 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -1 }, { "eq4k", 1 }, { "eq8k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -16 }, { "dv2On", 0 }, { "dv1Pan", 0.2f }, { "dv1Drift", 0.2f },
            { delayOn, 1 }, { delayType, tapeDelay }, { delaySync, quarter }, { delayFeedback, 0.2f }, { delayMix, 0.06f },
            { delayDuck, 0.7f }, { delayHighCut, 4000 },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.65f }, { reverbMix, 0.17f }, { reverbPreDelay, 35 },
            { reverbDecay, 1.2f } }),

        // A garage punk blast: a raw, cheeky shout, a little crunch, doubled,
        // a small room and nothing else.
        make ("Garage Punk Blast", K, 14, "Rock,Punk,Doubler,Saturation", {
            { rigLevel, -2 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 20 }, { photonOutput, -10 }, { photonAttack, 6 }, { photonRelease, 6 }, { photonRatio, photonEight },
            { deessOn, 1 }, { deessAmount, 6 },
            { driveOn, 1 }, { "mb1Style", crunchyAmpStyle }, { "mb1Drive", 0.25f },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq250", -1 }, { "eq1k", 1 }, { "eq2k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -4 }, { "dv2Gain", -4 }, { "dv1Pan", -0.7f }, { "dv2Pan", 0.7f },
            { "dv1Drift", 0.3f }, { "dv2Drift", 0.3f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.3f }, { reverbMix, 0.06f } }),

        // A rising hall lift: a chorus that opens up, the strong double just
        // off centre and a quieter pair wide, a faint echo and a hall, still
        // clean and clear.
        make ("Rising Hall Lift", K, 15, "Pop,Rock,Clean,Doubler,Reverb", {
            { rigLevel, -4 },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -18 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -2 }, { "eq250", -1 }, { "eq4k", 1 }, { "eq8k", 1 },
            { doublerOn, 1 }, { "dv3On", 1 }, { "dv4On", 1 },
            { "dv1Gain", -2 }, { "dv2Gain", -2 }, { "dv1Pan", -0.45f }, { "dv2Pan", 0.45f },
            { "dv1Detune", 7 }, { "dv2Detune", -7 }, { "dv1Delay", 14 }, { "dv2Delay", 22 },
            { "dv1Drift", 0.15f }, { "dv2Drift", 0.15f },
            { "dv3Gain", -9 }, { "dv4Gain", -9 }, { "dv3Pan", -1 }, { "dv4Pan", 1 },
            { "dv3Drift", 0.3f }, { "dv4Drift", 0.3f },
            { delayOn, 1 }, { delaySync, quarter }, { delayFeedback, 0.2f }, { delayMix, 0.05f }, { delayDuck, 0.8f },
            { delayHighCut, 5000 },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.6f }, { reverbMix, 0.15f }, { reverbPreDelay, 30 } }),
        // ---- L: a fifth album's vocal sounds ----------------------------------
        // Turn-of-the-millennium nu-metal with two vocalists: a rapper (tight,
        // dry, doubled, his verses often chopped, stuttered or filtered in the
        // edit) and a singer who goes from a clean, soaring hook to a torn
        // scream. Tags say which: "Rap" for the rapper's rigs, "Sung" or
        // "Scream" for the singer's. Layers are the DOUBLER (no HARMONY); the
        // edits are GLITCH, which sits before the DOUBLER so the takes stutter
        // with the lead, as a printed edit would. Our reading, not anyone's
        // settings.

        // A stutter-edit verse: a dry, doubled rap with the words chopped in
        // time (GLITCH Sequence: now and then a 1/16 slice repeats instead of
        // moving on), part of it squeezed through a phone band.
        make ("Stutter Edit Verse", L, 1, "Nu-Metal,Rap,Glitch,Lo-Fi,Doubler", {
            { rigLevel, -1 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 20 }, { photonOutput, -10 }, { photonAttack, 5 }, { photonRelease, 6 }, { photonRatio, photonEight },
            { deessOn, 1 }, { deessAmount, 6 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -3 }, { "eq1k", 2 }, { "eq2k", 2 }, { "eq8k", -1 },
            { glitchOn, 1 }, { glitchMode, sequenceGlitch }, { glitchDivision, sixteenthGlitch }, { glitchDensity, 4 },
            { glitchChaos, 2 }, { glitchMix, 0.7f },
            { doublerOn, 1 }, { "dv1Gain", -5 }, { "dv2Gain", -5 }, { "dv1Pan", -0.4f }, { "dv2Pan", 0.4f },
            { "dv1Drift", 0.15f }, { "dv2Drift", 0.15f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.2f }, { reverbMix, 0.05f },
            { lofiOn, 1 }, { lofiType, telephone }, { lofiAmount, 5 }, { lofiNoise, 0 }, { lofiMix, 0.45f } }),

        // A broken-signal rap: the same verse as a failing transmission
        // (GLITCH Interfere: bits dropped out, flipped backwards or caught in
        // a buzzing loop), through a cell-phone band, doubled tight.
        make ("Broken Signal Rap", L, 2, "Nu-Metal,Rap,Glitch,Lo-Fi,Doubler", {
            { rigLevel, 1.5f },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 20 }, { photonOutput, -10 }, { photonAttack, 5 }, { photonRelease, 6 }, { photonRatio, photonEight },
            { deessOn, 1 }, { deessAmount, 6 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -3 }, { "eq1k", 2 }, { "eq2k", 2 },
            { glitchOn, 1 }, { glitchMode, interfereGlitch }, { glitchDivision, sixteenthGlitch }, { glitchDensity, 3 },
            { glitchChaos, 4 }, { glitchMix, 0.6f },
            { doublerOn, 1 }, { "dv1Gain", -6 }, { "dv2Gain", -6 }, { "dv1Pan", -0.4f }, { "dv2Pan", 0.4f },
            { "dv1Drift", 0.15f }, { "dv2Drift", 0.15f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.2f }, { reverbMix, 0.05f },
            { lofiOn, 1 }, { lofiType, cellPhone }, { lofiAmount, 4 }, { lofiNoise, 2 }, { lofiMix, 0.5f } }),

        // A chopped-grid rap: fast 1/32 beat-repeats cut into a dry, doubled
        // rap, more often than the stutter edit, full range (no phone band).
        make ("Chopped Grid Rap", L, 3, "Nu-Metal,Rap,Glitch,Doubler", {
            { rigLevel, -1 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 20 }, { photonOutput, -10 }, { photonAttack, 5 }, { photonRelease, 6 }, { photonRatio, photonEight },
            { deessOn, 1 }, { deessAmount, 6 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -2 }, { "eq1k", 1 }, { "eq2k", 2 }, { "eq4k", 1 },
            { glitchOn, 1 }, { glitchMode, sequenceGlitch }, { glitchDivision, thirtySecondGlitch }, { glitchDensity, 3.5f },
            { glitchChaos, 1 }, { glitchMix, 0.75f },
            { doublerOn, 1 }, { "dv1Gain", -5 }, { "dv2Gain", -5 }, { "dv1Pan", -0.35f }, { "dv2Pan", 0.35f },
            { "dv1Drift", 0.15f }, { "dv2Drift", 0.15f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.15f }, { reverbMix, 0.04f } }),

        // A nod-along rap: dry and punchy, doubled tight, a quick slapback,
        // every word clear.
        make ("Nod Along Rap", L, 4, "Nu-Metal,Rap,Doubler,Photon", {
            { rigLevel, -2 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 20 }, { photonOutput, -10 }, { photonAttack, 5 }, { photonRelease, 6 }, { photonRatio, photonEight },
            { deessOn, 1 }, { deessAmount, 6 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -2 }, { "eq1k", 1 }, { "eq2k", 2 }, { "eq4k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -4 }, { "dv2Gain", -4 }, { "dv1Pan", -0.4f }, { "dv2Pan", 0.4f },
            { "dv1Detune", 5 }, { "dv2Detune", -5 }, { "dv1Drift", 0.15f }, { "dv2Drift", 0.15f },
            { delayOn, 1 }, { delayType, analogDelay }, { delaySync, syncOff }, { delayTime, 90 }, { delayFeedback, 0.08f },
            { delayMix, 0.06f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.2f }, { reverbMix, 0.05f } }),

        // A measured rap verse: calm and close, gently levelled, doubled
        // tight, a short plate: a rap that sits under a piano.
        make ("Measured Rap Verse", L, 5, "Nu-Metal,Rap,Clean,Doubler", {
            { rigLevel, -2 },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -18 },
            { deessOn, 1 }, { deessAmount, 6 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -2 }, { "eq250", -1 }, { "eq2k", 1 }, { "eq4k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -6 }, { "dv2Gain", -6 }, { "dv1Pan", -0.3f }, { "dv2Pan", 0.3f },
            { "dv1Drift", 0.15f }, { "dv2Drift", 0.15f },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.35f }, { reverbMix, 0.09f } }),

        // A fuse-blown scream: the singer's all-out shout, torn up on a tube,
        // doubled tight, a small room.
        make ("Fuse Blown Scream", L, 6, "Nu-Metal,Scream,Doubler,Saturation", {
            { rigLevel, -5.5f },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 24 }, { photonOutput, -12 }, { photonAttack, 5 }, { photonRelease, 6 }, { photonRatio, photonAll },
            { driveOn, 1 }, { marsBands, 1 }, { marsCross1, 500 },
            { "mb1Style", warmTubeStyle }, { "mb1Drive", 0.2f },
            { "mb2Style", brokenTubeStyle }, { "mb2Drive", 0.7f }, { "mb2Presence", 3 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -3 }, { "eq1k", 2 }, { "eq2k", 3 }, { "eq8k", 0 },
            { doublerOn, 1 }, { "dv1Gain", -4 }, { "dv2Gain", -4 }, { "dv1Pan", -0.5f }, { "dv2Pan", 0.5f },
            { "dv1Drift", 0.2f }, { "dv2Drift", 0.2f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.25f }, { reverbMix, 0.07f } }),

        // A strained-grit chorus: sung hard enough to crack, a tube adding
        // grit, the strong double just off centre, a short plate.
        make ("Strained Grit Chorus", L, 7, "Nu-Metal,Sung,Doubler,Saturation", {
            { rigLevel, -3.5f },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 20 }, { photonOutput, -10 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, photonEight },
            { deessOn, 1 }, { deessAmount, 6 },
            { driveOn, 1 }, { "mb1Style", saturatedTubeStyle }, { "mb1Drive", 0.35f },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -2 }, { "eq1k", 1 }, { "eq2k", 2 }, { "eq4k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -3 }, { "dv2Gain", -3 }, { "dv1Pan", -0.45f }, { "dv2Pan", 0.45f },
            { "dv1Detune", 6 }, { "dv2Detune", -6 }, { "dv1Drift", 0.15f }, { "dv2Drift", 0.15f },
            { delayOn, 1 }, { delaySync, eighth }, { delayFeedback, 0.15f }, { delayMix, 0.05f }, { delayDuck, 0.8f },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.4f }, { reverbMix, 0.11f } }),

        // A soaring hook chorus: the clean, lifted chorus, the strong double,
        // a dotted-eighth echo kept under and a plate.
        make ("Soaring Hook Chorus", L, 8, "Nu-Metal,Sung,Clean,Doubler", {
            { rigLevel, -4 },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -20 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -2 }, { "eq250", -1 }, { "eq4k", 1 }, { "eq8k", 2 },
            { doublerOn, 1 }, { "dv1Gain", -3 }, { "dv2Gain", -3 }, { "dv1Pan", -0.5f }, { "dv2Pan", 0.5f },
            { "dv1Detune", 6 }, { "dv2Detune", -6 }, { "dv1Drift", 0.15f }, { "dv2Drift", 0.15f },
            { delayOn, 1 }, { delaySync, dottedEighth }, { delayFeedback, 0.2f }, { delayMix, 0.06f }, { delayDuck, 0.8f },
            { delayHighCut, 5000 },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.45f }, { reverbMix, 0.13f } }),

        // A shadow-grit chorus: a dark, sung-shouted chorus with more grit,
        // doubled and a little wider, a room.
        make ("Shadow Grit Chorus", L, 9, "Nu-Metal,Sung,Doubler,Saturation", {
            { rigLevel, -5 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 22 }, { photonOutput, -11 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, photonEight },
            { deessOn, 1 }, { deessAmount, 6 },
            { driveOn, 1 }, { "mb1Style", crunchyAmpStyle }, { "mb1Drive", 0.4f },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -2 }, { "eq500", 1 }, { "eq1k", 2 }, { "eq2k", 2 },
            { doublerOn, 1 }, { "dv3On", 1 }, { "dv4On", 1 },
            { "dv1Gain", -3 }, { "dv2Gain", -3 }, { "dv1Pan", -0.5f }, { "dv2Pan", 0.5f },
            { "dv1Drift", 0.15f }, { "dv2Drift", 0.15f },
            { "dv3Gain", -9 }, { "dv4Gain", -9 }, { "dv3Pan", -1 }, { "dv4Pan", 1 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.3f }, { reverbMix, 0.07f } }),

        // A vinyl-filter verse: the sung verse half through an old radio
        // band, a slow flanger moving through it, doubled tight.
        make ("Vinyl Filter Verse", L, 10, "Nu-Metal,Sung,Lo-Fi,Modulation", {
            { rigLevel, -4 },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -18 },
            { deessOn, 1 }, { deessAmount, 6 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -3 }, { "eq1k", 2 }, { "eq2k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -8 }, { "dv2Gain", -8 },
            { modOn, 1 }, { modType, flanger }, { modRate, 0.15f }, { modDepth, 0.5f }, { modManual, 5 }, { modRes, 4 }, { modMix, 0.3f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.3f }, { reverbMix, 0.07f },
            { lofiOn, 1 }, { lofiType, amRadio }, { lofiAmount, 4 }, { lofiNoise, 2 }, { lofiMix, 0.5f } }),

        // An escape anthem: sung at full stretch with grit, the strong double
        // and a pair out wide, an eighth-note echo kept under.
        make ("Escape Anthem", L, 11, "Nu-Metal,Sung,Doubler,Saturation", {
            { rigLevel, -4 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 22 }, { photonOutput, -11 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, photonEight },
            { deessOn, 1 }, { deessAmount, 6 },
            { driveOn, 1 }, { "mb1Style", saturatedTubeStyle }, { "mb1Drive", 0.4f },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -2 }, { "eq1k", 1 }, { "eq2k", 2 }, { "eq4k", 1 },
            { doublerOn, 1 }, { "dv3On", 1 }, { "dv4On", 1 },
            { "dv1Gain", -3 }, { "dv2Gain", -3 }, { "dv1Pan", -0.45f }, { "dv2Pan", 0.45f },
            { "dv1Detune", 6 }, { "dv2Detune", -6 }, { "dv1Drift", 0.15f }, { "dv2Drift", 0.15f },
            { "dv3Gain", -9 }, { "dv4Gain", -9 }, { "dv3Pan", -1 }, { "dv4Pan", 1 },
            { delayOn, 1 }, { delaySync, eighth }, { delayFeedback, 0.15f }, { delayMix, 0.05f }, { delayDuck, 0.8f },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.4f }, { reverbMix, 0.1f } }),

        // A wounded ballad lead: softer and sung, close, a faint double, a
        // quarter-note echo and a hall.
        make ("Wounded Ballad Lead", L, 12, "Nu-Metal,Sung,Clean,Ballad,Reverb", {
            { compOn, 1 }, { compType, abyssComp }, { abyssPeakReduction, 40 }, { abyssGain, 10 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -2 }, { "eq4k", 1 }, { "eq8k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -10 }, { "dv2Gain", -10 }, { "dv1Pan", -0.6f }, { "dv2Pan", 0.6f },
            { delayOn, 1 }, { delaySync, quarter }, { delayFeedback, 0.25f }, { delayMix, 0.07f }, { delayDuck, 0.7f },
            { delayHighCut, 4500 },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.6f }, { reverbMix, 0.17f }, { reverbPreDelay, 30 } }),

        // A long bridge scream: one long, held scream, torn up hard, doubled,
        // a quarter-note echo trailing it.
        make ("Long Bridge Scream", L, 13, "Nu-Metal,Scream,Saturation,Delay", {
            { rigLevel, -5.5f },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 26 }, { photonOutput, -13 }, { photonAttack, 5 }, { photonRelease, 6 }, { photonRatio, photonAll },
            { driveOn, 1 }, { marsBands, 1 }, { marsCross1, 600 },
            { "mb1Style", warmTubeStyle }, { "mb1Drive", 0.2f },
            { "mb2Style", brokenTubeStyle }, { "mb2Drive", 0.8f }, { "mb2Presence", 3 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -3 }, { "eq1k", 2 }, { "eq2k", 3 },
            { doublerOn, 1 }, { "dv1Gain", -5 }, { "dv2Gain", -5 }, { "dv1Pan", -0.5f }, { "dv2Pan", 0.5f },
            { delayOn, 1 }, { delaySync, quarter }, { delayFeedback, 0.25f }, { delayMix, 0.08f }, { delayDuck, 0.6f },
            { delayHighCut, 4500 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.3f }, { reverbMix, 0.08f } }),

        // A twin-fury scream: the scream stacked four takes across, torn up,
        // almost dry: the heaviest wall in the bank.
        make ("Twin Fury Scream", L, 14, "Nu-Metal,Scream,Doubler,Saturation", {
            { rigLevel, -3 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 26 }, { photonOutput, -13 }, { photonAttack, 5 }, { photonRelease, 5 }, { photonRatio, photonAll },
            { driveOn, 1 }, { "mb1Style", brokenTubeStyle }, { "mb1Drive", 0.7f }, { "mb1Presence", 2 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -3 }, { "eq1k", 2 }, { "eq2k", 3 }, { "eq8k", -1 },
            { doublerOn, 1 }, { "dv3On", 1 }, { "dv4On", 1 }, { doublerOutput, -3 },
            { "dv1Pan", -1 }, { "dv2Pan", 1 }, { "dv3Pan", -0.35f }, { "dv4Pan", 0.35f },
            { "dv1Drift", 0.3f }, { "dv2Drift", 0.3f }, { "dv3Drift", 0.3f }, { "dv4Drift", 0.3f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.2f }, { reverbMix, 0.06f } }),

        // A whispered undercurrent: for a whispered layer under a verse:
        // close and bright, two takes far out to the sides, half through a
        // phone band, a small room.
        make ("Whispered Undercurrent", L, 15, "Nu-Metal,Whisper,Lo-Fi,Doubler", {
            { compOn, 1 }, { compType, abyssComp }, { abyssPeakReduction, 55 }, { abyssGain, 14 },
            { deessOn, 1 }, { deessFreq, 6500 }, { deessAmount, 8 },
            { eqOn, 1 }, { "eq63", -10 }, { "eq125", -5 }, { "eq250", -2 }, { "eq2k", 2 }, { "eq4k", 2 },
            { doublerOn, 1 }, { "dv1Gain", -4 }, { "dv2Gain", -4 }, { "dv1Pan", -1 }, { "dv2Pan", 1 },
            { "dv1Drift", 0.5f }, { "dv2Drift", 0.5f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.3f }, { reverbMix, 0.08f },
            { lofiOn, 1 }, { lofiType, telephone }, { lofiAmount, 4 }, { lofiNoise, 0 }, { lofiMix, 0.4f } }),
        // ---- M: a sixth album's vocal sounds ----------------------------------
        // Mid-2010s theatrical metalcore with two vocalists: a clean singer
        // (polished, belted, theatrical; tag "Sung") and a screamer (tag
        // "Scream", "Growl" for the lowest). Both are stacked thick with the
        // DOUBLER, the cleans with the strong double, the screams as walls of
        // takes. No HARMONY (the owner's rule), so the record's very tightly
        // tuned cleans are left to the singer. Our reading, not anyone's
        // settings.

        // A rallying address: a spoken call to arms through a megaphone, half
        // the voice clean underneath, in a big hall.
        make ("Rallying Address", M, 1, "Metalcore,Spoken,Lo-Fi,Reverb", {
            { rigLevel, -2 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 18 }, { photonOutput, -9 }, { photonAttack, 5 }, { photonRelease, 6 }, { photonRatio, photonEight },
            { deessOn, 1 }, { deessAmount, 6 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -3 }, { "eq1k", 2 }, { "eq2k", 2 },
            { delayOn, 1 }, { delaySync, quarter }, { delayFeedback, 0.25f }, { delayMix, 0.08f }, { delayDuck, 0.6f },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.75f }, { reverbMix, 0.2f },
            { lofiOn, 1 }, { lofiType, megaphone }, { lofiAmount, 5 }, { lofiNoise, 1 }, { lofiMix, 0.6f } }),

        // An oath scream: a sharp, mid-high scream, torn up on two bands,
        // two takes tight either side, a small room.
        make ("Oath Scream", M, 2, "Metalcore,Scream,Doubler,Saturation", {
            { rigLevel, -5 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 24 }, { photonOutput, -12 }, { photonAttack, 5 }, { photonRelease, 6 }, { photonRatio, photonAll },
            { driveOn, 1 }, { marsBands, 1 }, { marsCross1, 500 },
            { "mb1Style", warmTubeStyle }, { "mb1Drive", 0.2f },
            { "mb2Style", brokenTubeStyle }, { "mb2Drive", 0.7f }, { "mb2Presence", 3 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -3 }, { "eq1k", 2 }, { "eq2k", 3 }, { "eq8k", 0 },
            { doublerOn, 1 }, { "dv1Gain", -3 }, { "dv2Gain", -3 }, { "dv1Pan", -0.6f }, { "dv2Pan", 0.6f },
            { "dv1Drift", 0.2f }, { "dv2Drift", 0.2f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.25f }, { reverbMix, 0.07f } }),

        // An ageless anthem chorus: the big, polished clean chorus, belted,
        // the strong double just off centre and a pair out wide, a dotted
        // eighth echo kept under and a bright plate.
        make ("Ageless Anthem Chorus", M, 3, "Metalcore,Sung,Clean,Doubler", {
            { rigLevel, -7 },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -22 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -3 }, { "eq250", -1 }, { "eq2k", 1 }, { "eq4k", 1 }, { "eq8k", 2 },
            { doublerOn, 1 }, { "dv3On", 1 }, { "dv4On", 1 },
            { "dv1Gain", -2 }, { "dv2Gain", -2 }, { "dv1Pan", -0.45f }, { "dv2Pan", 0.45f },
            { "dv1Detune", 7 }, { "dv2Detune", -7 }, { "dv1Delay", 14 }, { "dv2Delay", 22 },
            { "dv1Drift", 0.15f }, { "dv2Drift", 0.15f },
            { "dv3Gain", -9 }, { "dv4Gain", -9 }, { "dv3Pan", -1 }, { "dv4Pan", 1 },
            { delayOn, 1 }, { delaySync, dottedEighth }, { delayFeedback, 0.2f }, { delayMix, 0.07f }, { delayDuck, 0.8f },
            { delayHighCut, 5000 },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.45f }, { reverbMix, 0.13f } }),

        // A circuit scream wall: screams stacked four takes across with an
        // electronic edge: a crushed copy blended in and a little grain from
        // GLITCH behind it.
        make ("Circuit Scream Wall", M, 4, "Metalcore,Scream,Doubler,Saturation,Lo-Fi,Experimental", {
            { rigLevel, -1.5f },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 26 }, { photonOutput, -13 }, { photonAttack, 5 }, { photonRelease, 5 }, { photonRatio, photonAll },
            { driveOn, 1 }, { "mb1Style", brokenTubeStyle }, { "mb1Drive", 0.7f }, { "mb1Presence", 2 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -3 }, { "eq1k", 2 }, { "eq2k", 3 }, { "eq8k", -1 },
            { glitchOn, 1 }, { glitchMode, texturizer }, { glitchMix, 0.15f },
            { doublerOn, 1 }, { "dv3On", 1 }, { "dv4On", 1 }, { doublerOutput, -3 },
            { "dv1Pan", -1 }, { "dv2Pan", 1 }, { "dv3Pan", -0.35f }, { "dv4Pan", 0.35f },
            { "dv1Drift", 0.3f }, { "dv2Drift", 0.3f }, { "dv3Drift", 0.3f }, { "dv4Drift", 0.3f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.2f }, { reverbMix, 0.06f },
            { lofiOn, 1 }, { lofiType, bitcrusher }, { lofiAmount, 4 }, { lofiNoise, 0 }, { lofiMix, 0.25f } }),

        // A skeleton-pact chorus: an arena clean chorus, a little rasp, the
        // strong double, a quarter-note echo under and a plate.
        make ("Skeleton Pact Chorus", M, 5, "Metalcore,Sung,Doubler,Saturation", {
            { rigLevel, -2.5f },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 20 }, { photonOutput, -10 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, 0 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 7 },
            { driveOn, 1 }, { "mb1Style", warmTubeStyle }, { "mb1Drive", 0.25f },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -2 }, { "eq1k", 1 }, { "eq2k", 2 }, { "eq4k", 1 }, { "eq8k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -2 }, { "dv2Gain", -2 }, { "dv1Pan", -0.45f }, { "dv2Pan", 0.45f },
            { "dv1Detune", 7 }, { "dv2Detune", -7 }, { "dv1Delay", 14 }, { "dv2Delay", 22 },
            { "dv1Drift", 0.15f }, { "dv2Drift", 0.15f },
            { delayOn, 1 }, { delaySync, quarter }, { delayFeedback, 0.2f }, { delayMix, 0.06f }, { delayDuck, 0.8f },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.45f }, { reverbMix, 0.12f } }),

        // A beast growl: the lowest roar, a full octave-down take under it,
        // the lows pushed and the top rolled off, very dry.
        make ("Beast Growl", M, 6, "Metalcore,Scream,Growl,Pitch,Saturation", {
            { rigLevel, -10 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 28 }, { photonOutput, -16 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, photonAll },
            { driveOn, 1 }, { "mb1Style", screamingAmpStyle }, { "mb1Drive", 0.5f }, { "mb1Bass", 3 }, { "mb1Treble", -4 },
            { eqOn, 1 }, { "eq63", -2 }, { "eq125", 3 }, { "eq250", 2 }, { "eq2k", 1 }, { "eq8k", -5 }, { "eq16k", -8 },
            { doublerOn, 1 }, { "dv1Gain", -8 }, { "dv2Gain", -8 },
            { "dv3On", 1 }, { "dv3Octave", 1 }, { "dv3Gain", -2 }, { "dv3Pan", 0 }, { "dv3Detune", 0 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.2f }, { reverbMix, 0.05f } }),

        // A doubtful verse: the clean voice close and quiet, gently levelled,
        // one faint take, a small chamber and a soft echo.
        make ("Doubtful Verse", M, 7, "Metalcore,Sung,Clean,Intimate,Abyss", {
            { rigLevel, 1.5f },
            { compOn, 1 }, { compType, abyssComp }, { abyssPeakReduction, 40 }, { abyssGain, 10 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -2 }, { "eq4k", 1 }, { "eq8k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -14 }, { "dv2On", 0 }, { "dv1Pan", 0.3f },
            { delayOn, 1 }, { delayType, tapeDelay }, { delaySync, quarter }, { delayFeedback, 0.2f }, { delayMix, 0.07f },
            { delayDuck, 0.6f }, { delayHighCut, 4500 },
            { reverbOn, 1 }, { reverbType, chamber }, { reverbSize, 0.4f }, { reverbMix, 0.15f } }),

        // An unhinged snarl: a theatrical, half-sung, half-sneered voice with
        // amp crunch, doubled tight, a slapback.
        make ("Unhinged Snarl", M, 8, "Metalcore,Sung,Saturation,Doubler", {
            { rigLevel, -5 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 22 }, { photonOutput, -11 }, { photonAttack, 5 }, { photonRelease, 6 }, { photonRatio, photonEight },
            { deessOn, 1 }, { deessAmount, 6 },
            { driveOn, 1 }, { "mb1Style", crunchyAmpStyle }, { "mb1Drive", 0.4f }, { "mb1Mid", 2 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -2 }, { "eq500", 1 }, { "eq1k", 2 }, { "eq2k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -4 }, { "dv2Gain", -4 }, { "dv1Pan", -0.4f }, { "dv2Pan", 0.4f },
            { "dv1Detune", 6 }, { "dv2Detune", -6 }, { "dv1Drift", 0.2f }, { "dv2Drift", 0.2f },
            { delayOn, 1 }, { delayType, analogDelay }, { delaySync, syncOff }, { delayTime, 100 }, { delayFeedback, 0.1f },
            { delayMix, 0.07f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.25f }, { reverbMix, 0.07f } }),

        // A rewired pulse voice: a cleaner, electronic take on a heavy song:
        // the sung voice doubled, a slow jet flanger and a soft crushed copy
        // blended in, a plate.
        make ("Rewired Pulse Voice", M, 9, "Metalcore,Sung,Electronic,Modulation,Lo-Fi", {
            { rigLevel, -5.5f },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -20 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -2 }, { "eq1k", 1 }, { "eq4k", 1 }, { "eq8k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -4 }, { "dv2Gain", -4 }, { "dv1Pan", -0.5f }, { "dv2Pan", 0.5f },
            { "dv1Drift", 0.2f }, { "dv2Drift", 0.2f },
            { modOn, 1 }, { modType, flanger }, { modRate, 0.12f }, { modDepth, 0.5f }, { modManual, 5 }, { modRes, 4 }, { modMix, 0.3f },
            { delayOn, 1 }, { delaySync, eighth }, { delayFeedback, 0.2f }, { delayMix, 0.07f }, { delayDuck, 0.7f },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.45f }, { reverbMix, 0.13f },
            { lofiOn, 1 }, { lofiType, bitcrusher }, { lofiAmount, 3 }, { lofiNoise, 0 }, { lofiMix, 0.2f } }),

        // A runaway gang shout: the whole band shouting, four takes spread
        // across, amp crunch, almost dry.
        make ("Runaway Gang Shout", M, 10, "Metalcore,Gang,Doubler,Saturation", {
            { rigLevel, -2.5f },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 22 }, { photonOutput, -11 }, { photonAttack, 5 }, { photonRelease, 6 }, { photonRatio, photonEight },
            { driveOn, 1 }, { "mb1Style", crunchyAmpStyle }, { "mb1Drive", 0.4f },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -3 }, { "eq500", 1 }, { "eq1k", 2 }, { "eq2k", 2 },
            { doublerOn, 1 }, { "dv3On", 1 }, { "dv4On", 1 }, { doublerOutput, -3 },
            { "dv1Pan", -1 }, { "dv2Pan", 1 }, { "dv3Pan", -0.5f }, { "dv4Pan", 0.5f },
            { "dv1Drift", 0.6f }, { "dv2Drift", 0.6f }, { "dv3Drift", 0.6f }, { "dv4Drift", 0.6f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.35f }, { reverbMix, 0.08f } }),

        // A lineage scream: a scream that rides the riff, doubled tight with
        // a quarter-note echo trailing it.
        make ("Lineage Scream", M, 11, "Metalcore,Scream,Saturation,Delay", {
            { rigLevel, -6 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 26 }, { photonOutput, -13 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, photonEight },
            { driveOn, 1 }, { "mb1Style", brokenTubeStyle }, { "mb1Drive", 0.7f }, { "mb1Mid", 2 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -2 }, { "eq500", 1 }, { "eq1k", 2 }, { "eq2k", 2 },
            { doublerOn, 1 }, { "dv1Gain", -4 }, { "dv2Gain", -4 }, { "dv1Pan", -0.5f }, { "dv2Pan", 0.5f },
            { delayOn, 1 }, { delaySync, quarter }, { delayFeedback, 0.2f }, { delayMix, 0.08f }, { delayDuck, 0.7f },
            { delayHighCut, 4500 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.25f }, { reverbMix, 0.07f } }),

        // An ashes-ascent chorus: the clean voice lifted high, the strong
        // double and a wide pair, a Shimmer echo an octave above, a hall.
        make ("Ashes Ascent Chorus", M, 12, "Metalcore,Sung,Clean,Doubler,Reverb", {
            { rigLevel, -6.5f },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -20 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -2 }, { "eq4k", 1 }, { "eq8k", 2 }, { "eq16k", 2 },
            { doublerOn, 1 }, { "dv3On", 1 }, { "dv4On", 1 },
            { "dv1Gain", -2 }, { "dv2Gain", -2 }, { "dv1Pan", -0.45f }, { "dv2Pan", 0.45f },
            { "dv1Detune", 7 }, { "dv2Detune", -7 }, { "dv1Delay", 14 }, { "dv2Delay", 22 },
            { "dv1Drift", 0.15f }, { "dv2Drift", 0.15f },
            { "dv3Gain", -9 }, { "dv4Gain", -9 }, { "dv3Pan", -1 }, { "dv4Pan", 1 },
            { delayOn, 1 }, { delayType, shimmer }, { delaySync, quarter }, { delayFeedback, 0.3f }, { delayMix, 0.09f },
            { delayDuck, 0.7f },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.7f }, { reverbMix, 0.2f }, { reverbPreDelay, 40 } }),

        // A defiant closer belt: the last chorus belted hard with grit, the
        // strong double, an eighth-note echo under and a plate.
        make ("Defiant Closer Belt", M, 13, "Metalcore,Sung,Doubler,Saturation", {
            { rigLevel, -5 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 22 }, { photonOutput, -11 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, photonEight },
            { deessOn, 1 }, { deessAmount, 6 },
            { driveOn, 1 }, { "mb1Style", saturatedTubeStyle }, { "mb1Drive", 0.4f },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -2 }, { "eq1k", 1 }, { "eq2k", 2 }, { "eq4k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -2 }, { "dv2Gain", -2 }, { "dv1Pan", -0.45f }, { "dv2Pan", 0.45f },
            { "dv1Detune", 7 }, { "dv2Detune", -7 }, { "dv1Delay", 14 }, { "dv2Delay", 22 },
            { "dv1Drift", 0.15f }, { "dv2Drift", 0.15f },
            { delayOn, 1 }, { delaySync, eighth }, { delayFeedback, 0.15f }, { delayMix, 0.05f }, { delayDuck, 0.8f },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.4f }, { reverbMix, 0.1f } }),

        // A captive battle scream: a raw, driving scream on two bands, doubled
        // tight, a small room.
        make ("Captive Battle Scream", M, 14, "Metalcore,Scream,Doubler,Saturation", {
            { rigLevel, -5 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 24 }, { photonOutput, -12 }, { photonAttack, 5 }, { photonRelease, 6 }, { photonRatio, photonAll },
            { driveOn, 1 }, { marsBands, 1 }, { marsCross1, 700 },
            { "mb1Style", warmTubeStyle }, { "mb1Drive", 0.25f },
            { "mb2Style", brokenTubeStyle }, { "mb2Drive", 0.75f }, { "mb2Presence", 2 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -3 }, { "eq1k", 2 }, { "eq2k", 3 },
            { doublerOn, 1 }, { "dv1Gain", -3 }, { "dv2Gain", -3 }, { "dv1Pan", -0.6f }, { "dv2Pan", 0.6f },
            { "dv1Drift", 0.2f }, { "dv2Drift", 0.2f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.25f }, { reverbMix, 0.07f } }),

        // An unplugged close lead: the clean voice natural and close for an
        // unplugged version, a faint take, a small chamber, nothing else.
        make ("Unplugged Close Lead", M, 15, "Metalcore,Sung,Clean,Unplugged,Intimate", {
            { rigLevel, 1.5f },
            { compOn, 1 }, { compType, abyssComp }, { abyssPeakReduction, 35 }, { abyssGain, 9 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 6 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -1 }, { "eq4k", 1 }, { "eq8k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -15 }, { "dv2On", 0 }, { "dv1Pan", -0.3f }, { "dv1Drift", 0.2f },
            { reverbOn, 1 }, { reverbType, chamber }, { reverbSize, 0.4f }, { reverbMix, 0.14f }, { reverbPreDelay, 15 } }),
        // ---- N: a seventh album's vocal sounds --------------------------------
        // Turn-of-the-millennium industrial nu-metal: one voice that goes from
        // a tense, close whisper to a torn scream within a line, often treated
        // like a machine (phone bands, megaphones, crushed copies, stutters
        // and scratches) over programmed noise. Layers are the DOUBLER (no
        // HARMONY). Our reading, not anyone's settings.

        // A coiled verse: tense and close, almost whispered, half through a
        // phone band, a faint take behind, a small room.
        make ("Coiled Verse", N, 1, "Nu-Metal,Industrial,Lo-Fi,Intimate", {
            { rigLevel, 1.5f },
            { compOn, 1 }, { compType, abyssComp }, { abyssPeakReduction, 50 }, { abyssGain, 12 },
            { deessOn, 1 }, { deessFreq, 6500 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -3 }, { "eq1k", 1 }, { "eq2k", 2 }, { "eq8k", -1 },
            { doublerOn, 1 }, { "dv1Gain", -12 }, { "dv2On", 0 }, { "dv1Pan", 0.3f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.25f }, { reverbMix, 0.07f },
            { lofiOn, 1 }, { lofiType, telephone }, { lofiAmount, 5 }, { lofiNoise, 1 }, { lofiMix, 0.45f } }),

        // An industrial scream surge: the chorus scream, torn up on two bands,
        // doubled tight, a crushed copy of itself blended in.
        make ("Industrial Scream Surge", N, 2, "Nu-Metal,Industrial,Scream,Doubler,Saturation,Lo-Fi", {
            { rigLevel, -5.5f },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 24 }, { photonOutput, -12 }, { photonAttack, 5 }, { photonRelease, 6 }, { photonRatio, photonAll },
            { driveOn, 1 }, { marsBands, 1 }, { marsCross1, 500 },
            { "mb1Style", warmTubeStyle }, { "mb1Drive", 0.2f },
            { "mb2Style", brokenTubeStyle }, { "mb2Drive", 0.7f }, { "mb2Presence", 3 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -3 }, { "eq1k", 2 }, { "eq2k", 3 },
            { doublerOn, 1 }, { "dv1Gain", -4 }, { "dv2Gain", -4 }, { "dv1Pan", -0.5f }, { "dv2Pan", 0.5f },
            { "dv1Drift", 0.2f }, { "dv2Drift", 0.2f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.25f }, { reverbMix, 0.07f },
            { lofiOn, 1 }, { lofiType, bitcrusher }, { lofiAmount, 4 }, { lofiNoise, 0 }, { lofiMix, 0.25f } }),

        // A collapsing-wall chorus: sung hard into a shout with grit, the
        // strong double and a wide pair, a short plate.
        make ("Collapsing Wall Chorus", N, 3, "Nu-Metal,Industrial,Doubler,Saturation", {
            { rigLevel, -5 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 22 }, { photonOutput, -11 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, photonEight },
            { deessOn, 1 }, { deessAmount, 6 },
            { driveOn, 1 }, { "mb1Style", saturatedTubeStyle }, { "mb1Drive", 0.4f },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -2 }, { "eq1k", 1 }, { "eq2k", 2 }, { "eq4k", 1 },
            { doublerOn, 1 }, { "dv3On", 1 }, { "dv4On", 1 },
            { "dv1Gain", -2 }, { "dv2Gain", -2 }, { "dv1Pan", -0.45f }, { "dv2Pan", 0.45f },
            { "dv1Detune", 7 }, { "dv2Detune", -7 }, { "dv1Delay", 14 }, { "dv2Delay", 22 },
            { "dv1Drift", 0.15f }, { "dv2Drift", 0.15f },
            { "dv3Gain", -9 }, { "dv4Gain", -9 }, { "dv3Pan", -1 }, { "dv4Pan", 1 },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.4f }, { reverbMix, 0.1f } }),

        // A scratched-signal verse: the voice through an old radio band and
        // broken up like a record under a DJ's hand (GLITCH Interfere: bits
        // dropped, flipped backwards, caught in a loop).
        make ("Scratched Signal Verse", N, 4, "Nu-Metal,Industrial,Glitch,Lo-Fi", {
            { rigLevel, -1 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 20 }, { photonOutput, -10 }, { photonAttack, 5 }, { photonRelease, 6 }, { photonRatio, photonEight },
            { deessOn, 1 }, { deessAmount, 6 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -3 }, { "eq1k", 2 }, { "eq2k", 1 },
            { glitchOn, 1 }, { glitchMode, interfereGlitch }, { glitchDivision, sixteenthGlitch }, { glitchDensity, 3 },
            { glitchChaos, 5 }, { glitchMix, 0.55f },
            { doublerOn, 1 }, { "dv1Gain", -8 }, { "dv2Gain", -8 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.25f }, { reverbMix, 0.06f },
            { lofiOn, 1 }, { lofiType, amRadio }, { lofiAmount, 4 }, { lofiNoise, 3 }, { lofiMix, 0.5f } }),

        // A hungry snarl: a gritty, half-sung sneer with amp crunch, doubled
        // tight, a slapback.
        make ("Hungry Snarl", N, 5, "Nu-Metal,Industrial,Doubler,Saturation", {
            { rigLevel, -5.5f },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 22 }, { photonOutput, -11 }, { photonAttack, 5 }, { photonRelease, 6 }, { photonRatio, photonEight },
            { deessOn, 1 }, { deessAmount, 6 },
            { driveOn, 1 }, { "mb1Style", crunchyAmpStyle }, { "mb1Drive", 0.4f }, { "mb1Mid", 2 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -2 }, { "eq500", 1 }, { "eq1k", 2 }, { "eq2k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -4 }, { "dv2Gain", -4 }, { "dv1Pan", -0.4f }, { "dv2Pan", 0.4f },
            { "dv1Detune", 6 }, { "dv2Detune", -6 }, { "dv1Drift", 0.2f }, { "dv2Drift", 0.2f },
            { delayOn, 1 }, { delayType, analogDelay }, { delaySync, syncOff }, { delayTime, 95 }, { delayFeedback, 0.1f },
            { delayMix, 0.07f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.25f }, { reverbMix, 0.06f } }),

        // A crooked-grin melody: a melodic sung line with a slow jet flanger
        // through it, doubled, an eighth-note echo and a plate.
        make ("Crooked Grin Melody", N, 6, "Nu-Metal,Industrial,Clean,Modulation", {
            { rigLevel, -5.5f },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -20 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -2 }, { "eq1k", 1 }, { "eq4k", 1 }, { "eq8k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -5 }, { "dv2Gain", -5 }, { "dv1Pan", -0.5f }, { "dv2Pan", 0.5f },
            { "dv1Drift", 0.2f }, { "dv2Drift", 0.2f },
            { modOn, 1 }, { modType, flanger }, { modRate, 0.12f }, { modDepth, 0.5f }, { modManual, 5 }, { modRes, 4 }, { modMix, 0.3f },
            { delayOn, 1 }, { delaySync, eighth }, { delayFeedback, 0.2f }, { delayMix, 0.07f }, { delayDuck, 0.7f },
            { delayHighCut, 5000 },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.45f }, { reverbMix, 0.12f } }),

        // A metronome stutter shout: a rhythmic shout with now and then a
        // 1/16 slice repeated in time (GLITCH Sequence), doubled, dry.
        make ("Metronome Stutter Shout", N, 7, "Nu-Metal,Industrial,Glitch,Doubler,Saturation", {
            { rigLevel, -4 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 22 }, { photonOutput, -11 }, { photonAttack, 5 }, { photonRelease, 6 }, { photonRatio, photonEight },
            { driveOn, 1 }, { "mb1Style", americanAmpStyle }, { "mb1Drive", 0.4f },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -2 }, { "eq1k", 2 }, { "eq2k", 2 },
            { glitchOn, 1 }, { glitchMode, sequenceGlitch }, { glitchDivision, sixteenthGlitch }, { glitchDensity, 3 },
            { glitchChaos, 1 }, { glitchMix, 0.7f },
            { doublerOn, 1 }, { "dv1Gain", -4 }, { "dv2Gain", -4 }, { "dv1Pan", -0.4f }, { "dv2Pan", 0.4f },
            { "dv1Drift", 0.15f }, { "dv2Drift", 0.15f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.2f }, { reverbMix, 0.05f } }),

        // A snide megaphone rant: a sneering, shouted rant through a
        // megaphone, half the voice clean underneath, a little crunch.
        make ("Snide Megaphone Rant", N, 8, "Nu-Metal,Industrial,Lo-Fi,Saturation", {
            { rigLevel, -2.5f },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 22 }, { photonOutput, -11 }, { photonAttack, 5 }, { photonRelease, 6 }, { photonRatio, photonEight },
            { driveOn, 1 }, { "mb1Style", crunchyAmpStyle }, { "mb1Drive", 0.3f },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -3 }, { "eq1k", 2 }, { "eq2k", 2 },
            { doublerOn, 1 }, { "dv1Gain", -6 }, { "dv2Gain", -6 }, { "dv1Pan", -0.4f }, { "dv2Pan", 0.4f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.25f }, { reverbMix, 0.06f },
            { lofiOn, 1 }, { lofiType, megaphone }, { lofiAmount, 6 }, { lofiNoise, 1 }, { lofiMix, 0.55f } }),

        // A fracture scream: the hardest scream, torn up on two bands,
        // doubled wide, a short slap.
        make ("Fracture Scream", N, 9, "Nu-Metal,Industrial,Scream,Doubler,Saturation", {
            { rigLevel, -5.5f },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 26 }, { photonOutput, -13 }, { photonAttack, 5 }, { photonRelease, 5 }, { photonRatio, photonAll },
            { driveOn, 1 }, { marsBands, 1 }, { marsCross1, 600 },
            { "mb1Style", warmTubeStyle }, { "mb1Drive", 0.25f },
            { "mb2Style", brokenTubeStyle }, { "mb2Drive", 0.8f }, { "mb2Presence", 3 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -3 }, { "eq1k", 2 }, { "eq2k", 3 }, { "eq8k", -1 },
            { doublerOn, 1 }, { "dv1Gain", -3 }, { "dv2Gain", -3 }, { "dv1Pan", -0.8f }, { "dv2Pan", 0.8f },
            { "dv1Drift", 0.25f }, { "dv2Drift", 0.25f },
            { delayOn, 1 }, { delayType, analogDelay }, { delaySync, syncOff }, { delayTime, 110 }, { delayFeedback, 0.1f },
            { delayMix, 0.06f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.2f }, { reverbMix, 0.05f } }),

        // A bitter-gratitude lead: a sung lead, a little grit, doubled, a
        // tape echo trailing it and a plate.
        make ("Bitter Gratitude Lead", N, 10, "Nu-Metal,Industrial,Doubler,Delay", {
            { rigLevel, -1 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 18 }, { photonOutput, -9 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, 0 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 6 },
            { driveOn, 1 }, { "mb1Style", warmTubeStyle }, { "mb1Drive", 0.25f },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -2 }, { "eq1k", 1 }, { "eq2k", 1 }, { "eq4k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -5 }, { "dv2Gain", -5 }, { "dv1Pan", -0.5f }, { "dv2Pan", 0.5f },
            { "dv1Drift", 0.2f }, { "dv2Drift", 0.2f },
            { delayOn, 1 }, { delayType, tapeDelay }, { delaySync, quarter }, { delayFeedback, 0.3f }, { delayMix, 0.09f },
            { delayDuck, 0.7f }, { delayHighCut, 4500 },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.45f }, { reverbMix, 0.12f } }),

        // A departure-drift verse: a moody clean voice, a slow wide chorus,
        // a soft echo and a hall.
        make ("Departure Drift Verse", N, 11, "Nu-Metal,Industrial,Clean,Modulation,Reverb", {
            { rigLevel, 2.5f },
            { compOn, 1 }, { compType, abyssComp }, { abyssPeakReduction, 40 }, { abyssGain, 10 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -2 }, { "eq4k", 1 }, { "eq8k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -11 }, { "dv2Gain", -11 },
            { modOn, 1 }, { modType, dimension }, { modRate, 0.3f }, { modDepth, 0.4f }, { modMix, 0.25f },
            { delayOn, 1 }, { delaySync, quarter }, { delayFeedback, 0.25f }, { delayMix, 0.08f }, { delayDuck, 0.6f },
            { delayHighCut, 4500 },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.65f }, { reverbMix, 0.18f }, { reverbPreDelay, 30 } }),

        // A long-fade epic: the long closing song, the voice doubled wide, a
        // ping-pong echo and a slowly turning cathedral.
        make ("Long Fade Epic", N, 12, "Nu-Metal,Industrial,Clean,Doubler,Reverb", {
            { rigLevel, -4.5f },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -20 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -2 }, { "eq4k", 1 }, { "eq8k", 2 },
            { doublerOn, 1 }, { "dv1Gain", -5 }, { "dv2Gain", -5 }, { "dv1Pan", -0.9f }, { "dv2Pan", 0.9f },
            { "dv1Drift", 0.4f }, { "dv2Drift", 0.4f },
            { delayOn, 1 }, { delaySync, dottedEighth }, { delayPingPong, 1 }, { delayFeedback, 0.35f }, { delayMix, 0.1f },
            { delayDuck, 0.7f }, { delayHighCut, 5000 },
            { reverbOn, 1 }, { reverbType, cathedral }, { reverbSize, 0.7f }, { reverbMix, 0.24f }, { reverbPreDelay, 50 },
            { reverbSpin, 1.5f }, { reverbWander, 1.5f } }),

        // A hidden dawn whisper: soft and far off, as if left on a tape after
        // the record seems to end, a dim room.
        make ("Hidden Dawn Whisper", N, 13, "Nu-Metal,Industrial,Lo-Fi,Intimate", {
            { rigLevel, 2 },
            { compOn, 1 }, { compType, abyssComp }, { abyssPeakReduction, 45 }, { abyssGain, 11 },
            { deessOn, 1 }, { deessFreq, 6500 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -3 }, { "eq4k", -1 }, { "eq8k", -3 },
            { doublerOn, 1 }, { "dv1Gain", -12 }, { "dv2Gain", -12 },
            { delayOn, 1 }, { delayType, tapeDelay }, { delaySync, quarter }, { delayFeedback, 0.25f }, { delayMix, 0.08f },
            { delayHighCut, 3500 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.5f }, { reverbMix, 0.16f },
            { lofiOn, 1 }, { lofiType, cassette }, { lofiAmount, 4 }, { lofiNoise, 3 }, { lofiMix, 0.6f } }),

        // A machine-room growl: the heaviest moments, a full octave-down take
        // under the voice, the lows pushed, the top rolled off, dry.
        make ("Machine Room Growl", N, 14, "Nu-Metal,Industrial,Scream,Growl,Pitch,Saturation", {
            { rigLevel, -10 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 28 }, { photonOutput, -16 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, photonAll },
            { driveOn, 1 }, { "mb1Style", screamingAmpStyle }, { "mb1Drive", 0.5f }, { "mb1Bass", 3 }, { "mb1Treble", -4 },
            { eqOn, 1 }, { "eq63", -2 }, { "eq125", 3 }, { "eq250", 2 }, { "eq2k", 1 }, { "eq8k", -5 }, { "eq16k", -8 },
            { doublerOn, 1 }, { "dv1Gain", -8 }, { "dv2Gain", -8 },
            { "dv3On", 1 }, { "dv3Octave", 1 }, { "dv3Gain", -2 }, { "dv3Pan", 0 }, { "dv3Detune", 0 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.2f }, { reverbMix, 0.05f } }),

        // A static-programmed voice: the voice as part of the programming,
        // half through a cell-phone band, a cloud of grains (GLITCH
        // Texturizer) behind it, doubled wide.
        make ("Static Programmed Voice", N, 15, "Nu-Metal,Industrial,Electronic,Glitch,Lo-Fi", {
            { rigLevel, 3 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 20 }, { photonOutput, -10 }, { photonAttack, 5 }, { photonRelease, 6 }, { photonRatio, photonEight },
            { deessOn, 1 }, { deessAmount, 6 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -3 }, { "eq1k", 2 }, { "eq2k", 1 },
            { glitchOn, 1 }, { glitchMode, texturizer }, { glitchMix, 0.25f },
            { doublerOn, 1 }, { "dv1Gain", -6 }, { "dv2Gain", -6 }, { "dv1Pan", -0.9f }, { "dv2Pan", 0.9f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.3f }, { reverbMix, 0.08f },
            { lofiOn, 1 }, { lofiType, cellPhone }, { lofiAmount, 4 }, { lofiNoise, 2 }, { lofiMix, 0.5f } }),
        // ---- O: an eighth album's vocal sounds --------------------------------
        // Late-90s industrial metal with a dance-machine pulse: a shouted,
        // chanted voice locked to programmed beats, tight and dry, torn up for
        // the screams, with dreamy, filtered melodies in a few choruses and a
        // second, deep voice roaring behind. Layers are the DOUBLER (no
        // HARMONY). Our reading, not anyone's settings.

        // A machine-disco chant: a rhythmic, shouted chant locked to the beat,
        // doubled dead centre, amp crunch, a touch of megaphone, dry.
        make ("Machine Disco Chant", O, 1, "Industrial,Nu-Metal,Doubler,Saturation,Lo-Fi", {
            { rigLevel, -2.5f },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 22 }, { photonOutput, -11 }, { photonAttack, 6 }, { photonRelease, 6 }, { photonRatio, photonEight },
            { driveOn, 1 }, { "mb1Style", crunchyAmpStyle }, { "mb1Drive", 0.35f },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -3 }, { "eq1k", 2 }, { "eq2k", 2 },
            { doublerOn, 1 }, { "dv1Gain", -3 }, { "dv2Gain", -3 }, { "dv1Pan", -0.2f }, { "dv2Pan", 0.2f },
            { "dv1Detune", 5 }, { "dv2Detune", -5 }, { "dv1Drift", 0.1f }, { "dv2Drift", 0.1f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.2f }, { reverbMix, 0.05f },
            { lofiOn, 1 }, { lofiType, megaphone }, { lofiAmount, 5 }, { lofiNoise, 0 }, { lofiMix, 0.3f } }),

        // A shove scream chorus: the chorus scream torn up on two bands,
        // doubled, with a quiet octave-down take as a second, deeper voice.
        make ("Shove Scream Chorus", O, 2, "Industrial,Nu-Metal,Scream,Doubler,Saturation", {
            { rigLevel, -6.5f },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 24 }, { photonOutput, -12 }, { photonAttack, 5 }, { photonRelease, 6 }, { photonRatio, photonAll },
            { driveOn, 1 }, { marsBands, 1 }, { marsCross1, 500 },
            { "mb1Style", warmTubeStyle }, { "mb1Drive", 0.2f },
            { "mb2Style", brokenTubeStyle }, { "mb2Drive", 0.75f }, { "mb2Presence", 3 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -2 }, { "eq1k", 2 }, { "eq2k", 3 },
            { doublerOn, 1 }, { "dv1Gain", -3 }, { "dv2Gain", -3 }, { "dv1Pan", -0.5f }, { "dv2Pan", 0.5f },
            { "dv1Drift", 0.2f }, { "dv2Drift", 0.2f },
            { "dv3On", 1 }, { "dv3Octave", 1 }, { "dv3Gain", -12 }, { "dv3Pan", 0 }, { "dv3Detune", 0 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.2f }, { reverbMix, 0.05f } }),

        // A dreamy filtered hook: a sung, floating chorus half through an old
        // radio band, a slow jet flanger, doubled wide, an echo.
        make ("Dreamy Filtered Hook", O, 3, "Industrial,Nu-Metal,Lo-Fi,Modulation,Doubler", {
            { rigLevel, -6 },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -20 },
            { deessOn, 1 }, { deessAmount, 6 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -3 }, { "eq1k", 2 }, { "eq2k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -4 }, { "dv2Gain", -4 }, { "dv1Pan", -0.7f }, { "dv2Pan", 0.7f },
            { "dv1Drift", 0.3f }, { "dv2Drift", 0.3f },
            { modOn, 1 }, { modType, flanger }, { modRate, 0.12f }, { modDepth, 0.5f }, { modManual, 5 }, { modRes, 4 }, { modMix, 0.3f },
            { delayOn, 1 }, { delaySync, eighth }, { delayFeedback, 0.25f }, { delayMix, 0.08f }, { delayDuck, 0.6f },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.45f }, { reverbMix, 0.12f },
            { lofiOn, 1 }, { lofiType, amRadio }, { lofiAmount, 4 }, { lofiNoise, 1 }, { lofiMix, 0.4f } }),

        // An ache-melody chorus: a sung, melodic chorus between the shouts,
        // the strong double just off centre, a plate.
        make ("Ache Melody Chorus", O, 4, "Industrial,Nu-Metal,Clean,Doubler", {
            { rigLevel, -5.5f },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -20 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -2 }, { "eq1k", 1 }, { "eq4k", 1 }, { "eq8k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -2 }, { "dv2Gain", -2 }, { "dv1Pan", -0.45f }, { "dv2Pan", 0.45f },
            { "dv1Detune", 7 }, { "dv2Detune", -7 }, { "dv1Delay", 14 }, { "dv2Delay", 22 },
            { "dv1Drift", 0.15f }, { "dv2Drift", 0.15f },
            { delayOn, 1 }, { delaySync, quarter }, { delayFeedback, 0.2f }, { delayMix, 0.06f }, { delayDuck, 0.8f },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.4f }, { reverbMix, 0.11f } }),

        // A grinding riff shout: a crunchy, mid-heavy shout doubled tight, a
        // quick slapback.
        make ("Grinding Riff Shout", O, 5, "Industrial,Nu-Metal,Doubler,Saturation", {
            { rigLevel, -6.5f },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 22 }, { photonOutput, -11 }, { photonAttack, 5 }, { photonRelease, 6 }, { photonRatio, photonEight },
            { driveOn, 1 }, { "mb1Style", americanAmpStyle }, { "mb1Drive", 0.45f }, { "mb1Mid", 2 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -2 }, { "eq500", 1 }, { "eq1k", 2 }, { "eq2k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -4 }, { "dv2Gain", -4 }, { "dv1Pan", -0.3f }, { "dv2Pan", 0.3f },
            { "dv1Detune", 5 }, { "dv2Detune", -5 }, { "dv1Drift", 0.15f }, { "dv2Drift", 0.15f },
            { delayOn, 1 }, { delayType, analogDelay }, { delaySync, syncOff }, { delayTime, 90 }, { delayFeedback, 0.08f },
            { delayMix, 0.06f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.2f }, { reverbMix, 0.05f } }),

        // A mantra shout: a chanted, repeated shout, four takes across,
        // crunch, almost dry: a whole crowd chanting the line.
        make ("Mantra Shout", O, 6, "Industrial,Nu-Metal,Gang,Doubler,Saturation", {
            { rigLevel, -2.5f },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 22 }, { photonOutput, -11 }, { photonAttack, 5 }, { photonRelease, 6 }, { photonRatio, photonEight },
            { driveOn, 1 }, { "mb1Style", crunchyAmpStyle }, { "mb1Drive", 0.4f },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -3 }, { "eq500", 1 }, { "eq1k", 2 }, { "eq2k", 2 },
            { doublerOn, 1 }, { "dv3On", 1 }, { "dv4On", 1 }, { doublerOutput, -3 },
            { "dv1Pan", -1 }, { "dv2Pan", 1 }, { "dv3Pan", -0.4f }, { "dv4Pan", 0.4f },
            { "dv1Drift", 0.4f }, { "dv2Drift", 0.4f }, { "dv3Drift", 0.4f }, { "dv4Drift", 0.4f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.3f }, { reverbMix, 0.06f } }),

        // A graveyard growl chant: a low, menacing chant with a full
        // octave-down take under it, the lows pushed, dry.
        make ("Graveyard Growl Chant", O, 7, "Industrial,Nu-Metal,Scream,Growl,Pitch,Saturation", {
            { rigLevel, -10 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 28 }, { photonOutput, -16 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, photonAll },
            { driveOn, 1 }, { "mb1Style", screamingAmpStyle }, { "mb1Drive", 0.5f }, { "mb1Bass", 3 }, { "mb1Treble", -4 },
            { eqOn, 1 }, { "eq63", -2 }, { "eq125", 3 }, { "eq250", 2 }, { "eq2k", 1 }, { "eq8k", -5 }, { "eq16k", -8 },
            { doublerOn, 1 }, { "dv1Gain", -8 }, { "dv2Gain", -8 },
            { "dv3On", 1 }, { "dv3Octave", 1 }, { "dv3Gain", -2 }, { "dv3Pan", 0 }, { "dv3Detune", 0 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.25f }, { reverbMix, 0.06f } }),

        // A hypnotic spoken loop: a spoken, droning voice through a phone
        // band, now and then a slice repeated in time (GLITCH Sequence), a
        // ping-pong echo.
        make ("Hypnotic Spoken Loop", O, 8, "Industrial,Nu-Metal,Spoken,Glitch,Lo-Fi", {
            { compOn, 1 }, { compType, photonComp }, { photonInput, 20 }, { photonOutput, -10 }, { photonAttack, 5 }, { photonRelease, 6 }, { photonRatio, photonEight },
            { deessOn, 1 }, { deessAmount, 6 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -3 }, { "eq1k", 2 }, { "eq2k", 1 },
            { glitchOn, 1 }, { glitchMode, sequenceGlitch }, { glitchDivision, sixteenthGlitch }, { glitchDensity, 3 },
            { glitchChaos, 1 }, { glitchMix, 0.65f },
            { doublerOn, 1 }, { "dv1Gain", -8 }, { "dv2Gain", -8 },
            { delayOn, 1 }, { delaySync, eighth }, { delayPingPong, 1 }, { delayFeedback, 0.3f }, { delayMix, 0.1f },
            { delayDuck, 0.5f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.3f }, { reverbMix, 0.07f },
            { lofiOn, 1 }, { lofiType, telephone }, { lofiAmount, 5 }, { lofiNoise, 0 }, { lofiMix, 0.5f } }),

        // A sticky fury scream: a fast, furious scream torn up on two bands,
        // doubled wide, a small room.
        make ("Sticky Fury Scream", O, 9, "Industrial,Nu-Metal,Scream,Doubler,Saturation", {
            { rigLevel, -6 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 26 }, { photonOutput, -13 }, { photonAttack, 5 }, { photonRelease, 6 }, { photonRatio, photonAll },
            { driveOn, 1 }, { marsBands, 1 }, { marsCross1, 600 },
            { "mb1Style", warmTubeStyle }, { "mb1Drive", 0.25f },
            { "mb2Style", brokenTubeStyle }, { "mb2Drive", 0.8f }, { "mb2Presence", 2 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -3 }, { "eq1k", 2 }, { "eq2k", 3 }, { "eq8k", -1 },
            { doublerOn, 1 }, { "dv1Gain", -3 }, { "dv2Gain", -3 }, { "dv1Pan", -0.7f }, { "dv2Pan", 0.7f },
            { "dv1Drift", 0.2f }, { "dv2Drift", 0.2f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.2f }, { reverbMix, 0.05f } }),

        // A craving snarl: a jittery, sneering half-shout with amp crunch, a
        // little GLITCH interference breaking it up, doubled tight.
        make ("Craving Snarl", O, 10, "Industrial,Nu-Metal,Glitch,Saturation,Doubler", {
            { rigLevel, -3 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 22 }, { photonOutput, -11 }, { photonAttack, 5 }, { photonRelease, 6 }, { photonRatio, photonEight },
            { driveOn, 1 }, { "mb1Style", crunchyAmpStyle }, { "mb1Drive", 0.4f },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -2 }, { "eq1k", 2 }, { "eq2k", 1 },
            { glitchOn, 1 }, { glitchMode, interfereGlitch }, { glitchDivision, sixteenthGlitch }, { glitchDensity, 2.5f },
            { glitchChaos, 3 }, { glitchMix, 0.5f },
            { doublerOn, 1 }, { "dv1Gain", -4 }, { "dv2Gain", -4 }, { "dv1Pan", -0.35f }, { "dv2Pan", 0.35f },
            { "dv1Drift", 0.15f }, { "dv2Drift", 0.15f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.2f }, { reverbMix, 0.05f } }),

        // A frozen-highway wall: the title song's wall of screams, four takes
        // across, torn up, a crushed copy blended in.
        make ("Frozen Highway Wall", O, 11, "Industrial,Nu-Metal,Scream,Doubler,Saturation,Lo-Fi", {
            { rigLevel, -2.5f },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 26 }, { photonOutput, -13 }, { photonAttack, 5 }, { photonRelease, 5 }, { photonRatio, photonAll },
            { driveOn, 1 }, { "mb1Style", brokenTubeStyle }, { "mb1Drive", 0.7f }, { "mb1Presence", 2 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -3 }, { "eq1k", 2 }, { "eq2k", 3 }, { "eq8k", -1 },
            { doublerOn, 1 }, { "dv3On", 1 }, { "dv4On", 1 }, { doublerOutput, -3 },
            { "dv1Pan", -1 }, { "dv2Pan", 1 }, { "dv3Pan", -0.35f }, { "dv4Pan", 0.35f },
            { "dv1Drift", 0.3f }, { "dv2Drift", 0.3f }, { "dv3Drift", 0.3f }, { "dv4Drift", 0.3f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.2f }, { reverbMix, 0.05f },
            { lofiOn, 1 }, { lofiType, bitcrusher }, { lofiAmount, 4 }, { lofiNoise, 0 }, { lofiMix, 0.25f } }),

        // A rooted stomp shout: a heavy, dry shout, doubled dead centre,
        // amp crunch, nothing else.
        make ("Rooted Stomp Shout", O, 12, "Industrial,Nu-Metal,Doubler,Saturation", {
            { rigLevel, -4.5f },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 24 }, { photonOutput, -12 }, { photonAttack, 6 }, { photonRelease, 6 }, { photonRatio, photonAll },
            { driveOn, 1 }, { "mb1Style", americanAmpStyle }, { "mb1Drive", 0.5f }, { "mb1Presence", 2 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -2 }, { "eq1k", 2 }, { "eq2k", 2 },
            { doublerOn, 1 }, { "dv1Gain", -3 }, { "dv2Gain", -3 }, { "dv1Pan", -0.3f }, { "dv2Pan", 0.3f },
            { "dv1Detune", 5 }, { "dv2Detune", -5 }, { "dv1Drift", 0.1f }, { "dv2Drift", 0.1f } }),

        // A winter-drift ballad: the slow closer, a soft, dreamy sung voice,
        // a slow wide chorus, a long echo and a hall.
        make ("Winter Drift Ballad", O, 13, "Industrial,Nu-Metal,Clean,Ballad,Modulation,Reverb", {
            { rigLevel, 2 },
            { compOn, 1 }, { compType, abyssComp }, { abyssPeakReduction, 40 }, { abyssGain, 10 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 7 },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -2 }, { "eq4k", 1 }, { "eq8k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -9 }, { "dv2Gain", -9 }, { "dv1Pan", -0.8f }, { "dv2Pan", 0.8f },
            { modOn, 1 }, { modType, dimension }, { modRate, 0.3f }, { modDepth, 0.45f }, { modMix, 0.3f },
            { delayOn, 1 }, { delaySync, quarter }, { delayFeedback, 0.35f }, { delayMix, 0.1f }, { delayDuck, 0.6f },
            { delayHighCut, 4500 },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.75f }, { reverbMix, 0.22f }, { reverbPreDelay, 40 } }),

        // A sinking shout: a heavy, driving shout with crunch, doubled, a
        // short slap and a small room.
        make ("Sinking Shout", O, 14, "Industrial,Nu-Metal,Doubler,Saturation", {
            { rigLevel, -4.5f },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 22 }, { photonOutput, -11 }, { photonAttack, 5 }, { photonRelease, 6 }, { photonRatio, photonEight },
            { driveOn, 1 }, { "mb1Style", crunchyAmpStyle }, { "mb1Drive", 0.45f },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -2 }, { "eq500", 1 }, { "eq1k", 2 }, { "eq2k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -4 }, { "dv2Gain", -4 }, { "dv1Pan", -0.5f }, { "dv2Pan", 0.5f },
            { "dv1Drift", 0.2f }, { "dv2Drift", 0.2f },
            { delayOn, 1 }, { delayType, analogDelay }, { delaySync, syncOff }, { delayTime, 100 }, { delayFeedback, 0.08f },
            { delayMix, 0.06f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.25f }, { reverbMix, 0.06f } }),

        // A low backing roar: for the second, deeper voice that roars behind
        // the lead: an octave-down take under a gritty roar, wide.
        make ("Low Backing Roar", O, 15, "Industrial,Nu-Metal,Scream,Growl,Pitch,Saturation", {
            { rigLevel, -3 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 26 }, { photonOutput, -14 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, photonAll },
            { driveOn, 1 }, { "mb1Style", brokenTubeStyle }, { "mb1Drive", 0.55f }, { "mb1Bass", 2 }, { "mb1Treble", -3 },
            { eqOn, 1 }, { "eq63", -2 }, { "eq125", 3 }, { "eq250", 1 }, { "eq2k", 0 }, { "eq4k", -2 }, { "eq8k", -6 }, { "eq16k", -6 },
            { doublerOn, 1 }, { "dv1Gain", -10 }, { "dv2Gain", -10 }, { "dv1Pan", -0.9f }, { "dv2Pan", 0.9f },
            { "dv3On", 1 }, { "dv3Octave", 1 }, { "dv3Gain", 0 }, { "dv3Pan", 0 }, { "dv3Detune", 0 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.3f }, { reverbMix, 0.07f } }),
        // ---- P: a ninth album's vocal sounds ----------------------------------
        // Early-90s grunge / hard rock: a deep, rich baritone, snarled in the
        // heavy songs, crooned in the soft ones, now and then shouted through
        // a megaphone; recorded dry and close, the choruses doubled, short
        // rooms and plates. Layers are the DOUBLER (no HARMONY). Our reading,
        // not anyone's settings.

        // A bullhorn intro snarl: a snarled intro through a megaphone, most
        // of the voice in the horn, a little crunch, a room.
        make ("Bullhorn Intro Snarl", P, 1, "Grunge,Rock,Lo-Fi,Saturation", {
            { rigLevel, -2.5f },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 20 }, { photonOutput, -10 }, { photonAttack, 5 }, { photonRelease, 6 }, { photonRatio, photonEight },
            { driveOn, 1 }, { "mb1Style", crunchyAmpStyle }, { "mb1Drive", 0.25f },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -2 }, { "eq1k", 2 }, { "eq2k", 2 },
            { doublerOn, 1 }, { "dv1Gain", -6 }, { "dv2Gain", -6 }, { "dv1Pan", -1 }, { "dv2Pan", 1 },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.35f }, { reverbMix, 0.08f },
            { lofiOn, 1 }, { lofiType, megaphone }, { lofiAmount, 6 }, { lofiNoise, 1 }, { lofiMix, 0.6f } }),

        // A swollen baritone grind: the deep baritone with grit, doubled
        // tight, a slapback and a small room.
        make ("Swollen Baritone Grind", P, 2, "Grunge,Rock,Doubler,Saturation", {
            { rigLevel, -4 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 20 }, { photonOutput, -10 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, photonEight },
            { deessOn, 1 }, { deessAmount, 5 },
            { driveOn, 1 }, { "mb1Style", saturatedTubeStyle }, { "mb1Drive", 0.35f }, { "mb1Bass", 1 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", 1 }, { "eq250", 1 }, { "eq1k", 1 }, { "eq2k", 1 }, { "eq8k", -1 },
            { doublerOn, 1 }, { "dv1Gain", -4 }, { "dv2Gain", -4 }, { "dv1Pan", -0.35f }, { "dv2Pan", 0.35f },
            { "dv1Detune", 5 }, { "dv2Detune", -5 }, { "dv1Drift", 0.15f }, { "dv2Drift", 0.15f },
            { delayOn, 1 }, { delayType, analogDelay }, { delaySync, syncOff }, { delayTime, 110 }, { delayFeedback, 0.1f },
            { delayMix, 0.07f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.3f }, { reverbMix, 0.07f } }),

        // A swagger snarl verse: an aggressive, sneered baritone with amp
        // crunch, doubled tight, dry.
        make ("Swagger Snarl Verse", P, 3, "Grunge,Rock,Doubler,Saturation", {
            { rigLevel, -6.5f },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 22 }, { photonOutput, -11 }, { photonAttack, 5 }, { photonRelease, 6 }, { photonRatio, photonEight },
            { deessOn, 1 }, { deessAmount, 5 },
            { driveOn, 1 }, { "mb1Style", americanAmpStyle }, { "mb1Drive", 0.4f }, { "mb1Mid", 2 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", 1 }, { "eq500", 1 }, { "eq1k", 2 }, { "eq2k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -4 }, { "dv2Gain", -4 }, { "dv1Pan", -0.3f }, { "dv2Pan", 0.3f },
            { "dv1Drift", 0.15f }, { "dv2Drift", 0.15f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.2f }, { reverbMix, 0.05f } }),

        // An overgrown anthem hook: a big sung baritone chorus, a breath of
        // tube warmth, the strong double, a dotted-eighth echo and a plate.
        make ("Overgrown Anthem Hook", P, 4, "Grunge,Rock,Doubler", {
            { rigLevel, -1.5f },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 18 }, { photonOutput, -9 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, 0 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 6 },
            { driveOn, 1 }, { "mb1Style", warmTubeStyle }, { "mb1Drive", 0.15f },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", 1 }, { "eq250", -1 }, { "eq2k", 1 }, { "eq4k", 1 }, { "eq8k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -2 }, { "dv2Gain", -2 }, { "dv1Pan", -0.45f }, { "dv2Pan", 0.45f },
            { "dv1Detune", 7 }, { "dv2Detune", -7 }, { "dv1Delay", 14 }, { "dv2Delay", 22 },
            { "dv1Drift", 0.15f }, { "dv2Drift", 0.15f },
            { delayOn, 1 }, { delaySync, dottedEighth }, { delayFeedback, 0.2f }, { delayMix, 0.06f }, { delayDuck, 0.8f },
            { delayHighCut, 5000 },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.4f }, { reverbMix, 0.11f } }),

        // A penance croon: a moody, low croon, close, a soft tape echo and a
        // plate.
        make ("Penance Croon", P, 5, "Grunge,Rock,Clean,Delay", {
            { rigLevel, 1 },
            { compOn, 1 }, { compType, abyssComp }, { abyssPeakReduction, 40 }, { abyssGain, 10 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 6 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", 1 }, { "eq250", 1 }, { "eq4k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -10 }, { "dv2Gain", -10 },
            { delayOn, 1 }, { delayType, tapeDelay }, { delaySync, quarter }, { delayFeedback, 0.25f }, { delayMix, 0.08f },
            { delayDuck, 0.7f }, { delayHighCut, 4000 },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.5f }, { reverbMix, 0.13f } }),

        // A lazy funk drawl: a loose, laid-back baritone over a funky groove,
        // doubled tight, a quick slap, dry.
        make ("Lazy Funk Drawl", P, 6, "Grunge,Rock,Clean,Doubler", {
            { rigLevel, -3 },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -18 },
            { deessOn, 1 }, { deessAmount, 6 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", 1 }, { "eq1k", 1 }, { "eq2k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -5 }, { "dv2Gain", -5 }, { "dv1Pan", -0.4f }, { "dv2Pan", 0.4f },
            { "dv1Detune", 5 }, { "dv2Detune", -5 }, { "dv1Drift", 0.15f }, { "dv2Drift", 0.15f },
            { delayOn, 1 }, { delayType, analogDelay }, { delaySync, syncOff }, { delayTime, 95 }, { delayFeedback, 0.08f },
            { delayMix, 0.06f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.25f }, { reverbMix, 0.06f } }),

        // A hushed acoustic confession: a soft, close voice over an acoustic
        // guitar, one faint take, a small chamber.
        make ("Hushed Acoustic Confession", P, 7, "Grunge,Ballad,Clean,Intimate", {
            { rigLevel, 1.5f },
            { compOn, 1 }, { compType, abyssComp }, { abyssPeakReduction, 35 }, { abyssGain, 9 },
            { deessOn, 1 }, { deessFreq, 6500 }, { deessAmount, 6 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", 1 }, { "eq250", 1 }, { "eq4k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -15 }, { "dv2On", 0 }, { "dv1Pan", 0.3f },
            { reverbOn, 1 }, { reverbType, chamber }, { reverbSize, 0.4f }, { reverbMix, 0.14f }, { reverbPreDelay, 15 } }),

        // A lifting chorus swell: the soft song's chorus opening up, the
        // strong double, a soft echo and a hall.
        make ("Lifting Chorus Swell", P, 8, "Grunge,Ballad,Clean,Doubler,Reverb", {
            { rigLevel, -4 },
            { compOn, 1 }, { compType, revival }, { revivalCompression, -18 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 6 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", 1 }, { "eq4k", 1 }, { "eq8k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -2 }, { "dv2Gain", -2 }, { "dv1Pan", -0.45f }, { "dv2Pan", 0.45f },
            { "dv1Detune", 7 }, { "dv2Detune", -7 }, { "dv1Delay", 14 }, { "dv2Delay", 22 },
            { "dv1Drift", 0.15f }, { "dv2Drift", 0.15f },
            { delayOn, 1 }, { delaySync, quarter }, { delayFeedback, 0.2f }, { delayMix, 0.06f }, { delayDuck, 0.7f },
            { delayHighCut, 4500 },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.6f }, { reverbMix, 0.16f } }),

        // A bitter groove lead: a grungy, mid-heavy groove vocal with crunch,
        // doubled, a small room.
        make ("Bitter Groove Lead", P, 9, "Grunge,Rock,Doubler,Saturation", {
            { rigLevel, -3.5f },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 20 }, { photonOutput, -10 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, photonEight },
            { deessOn, 1 }, { deessAmount, 5 },
            { driveOn, 1 }, { "mb1Style", crunchyAmpStyle }, { "mb1Drive", 0.35f },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", 1 }, { "eq500", 1 }, { "eq1k", 1 }, { "eq2k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -4 }, { "dv2Gain", -4 }, { "dv1Pan", -0.4f }, { "dv2Pan", 0.4f },
            { "dv1Drift", 0.2f }, { "dv2Drift", 0.2f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.3f }, { reverbMix, 0.07f } }),

        // A velvet baritone verse: the dark, rich baritone close on the mic,
        // a faint take behind, a short plate.
        make ("Velvet Baritone Verse", P, 10, "Grunge,Rock,Clean,Intimate", {
            { rigLevel, 1 },
            { compOn, 1 }, { compType, abyssComp }, { abyssPeakReduction, 45 }, { abyssGain, 11 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 6 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", 2 }, { "eq250", 1 }, { "eq2k", 1 }, { "eq8k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -13 }, { "dv2On", 0 }, { "dv1Pan", -0.3f }, { "dv1Drift", 0.2f },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.35f }, { reverbMix, 0.09f } }),

        // A velvet baritone chorus: the same voice opened up, a little tube
        // grit, the strong double, an echo kept under and a plate.
        make ("Velvet Baritone Chorus", P, 11, "Grunge,Rock,Doubler", {
            { rigLevel, -2 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 18 }, { photonOutput, -9 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, 0 },
            { deessOn, 1 }, { deessFreq, 7000 }, { deessAmount, 6 },
            { driveOn, 1 }, { "mb1Style", warmTubeStyle }, { "mb1Drive", 0.2f },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", 2 }, { "eq250", 1 }, { "eq2k", 1 }, { "eq4k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -2 }, { "dv2Gain", -2 }, { "dv1Pan", -0.45f }, { "dv2Pan", 0.45f },
            { "dv1Detune", 7 }, { "dv2Detune", -7 }, { "dv1Delay", 14 }, { "dv2Delay", 22 },
            { "dv1Drift", 0.15f }, { "dv2Drift", 0.15f },
            { delayOn, 1 }, { delaySync, quarter }, { delayFeedback, 0.2f }, { delayMix, 0.06f }, { delayDuck, 0.8f },
            { delayHighCut, 4500 },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.45f }, { reverbMix, 0.12f } }),

        // A drowsy haze murmur: a murmured, sleepy voice half through an old
        // radio band, a slow flanger drifting through it, a room.
        make ("Drowsy Haze Murmur", P, 12, "Grunge,Lo-Fi,Modulation,Intimate", {
            { rigLevel, -1.5f },
            { compOn, 1 }, { compType, abyssComp }, { abyssPeakReduction, 50 }, { abyssGain, 12 },
            { deessOn, 1 }, { deessAmount, 6 },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", -1 }, { "eq1k", 1 }, { "eq8k", -2 },
            { doublerOn, 1 }, { "dv1Gain", -9 }, { "dv2Gain", -9 }, { "dv1Drift", 0.5f }, { "dv2Drift", 0.5f },
            { modOn, 1 }, { modType, flanger }, { modRate, 0.1f }, { modDepth, 0.5f }, { modManual, 5 }, { modRes, 3 }, { modMix, 0.3f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.45f }, { reverbMix, 0.12f },
            { lofiOn, 1 }, { lofiType, amRadio }, { lofiAmount, 4 }, { lofiNoise, 2 }, { lofiMix, 0.5f } }),

        // A megaphone rush shout: a fast, punky shout half through a
        // megaphone, amp crunch, doubled, dry.
        make ("Megaphone Rush Shout", P, 13, "Grunge,Rock,Punk,Lo-Fi,Saturation", {
            { rigLevel, -2 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 22 }, { photonOutput, -11 }, { photonAttack, 6 }, { photonRelease, 6 }, { photonRatio, photonEight },
            { driveOn, 1 }, { "mb1Style", crunchyAmpStyle }, { "mb1Drive", 0.4f },
            { eqOn, 1 }, { "eq63", -8 }, { "eq125", -1 }, { "eq1k", 2 }, { "eq2k", 2 },
            { doublerOn, 1 }, { "dv1Gain", -5 }, { "dv2Gain", -5 }, { "dv1Pan", -0.4f }, { "dv2Pan", 0.4f },
            { reverbOn, 1 }, { reverbType, room }, { reverbSize, 0.25f }, { reverbMix, 0.05f },
            { lofiOn, 1 }, { lofiType, megaphone }, { lofiAmount, 6 }, { lofiNoise, 1 }, { lofiMix, 0.5f } }),

        // A drifting current lead: a long, rolling rock vocal with a little
        // grit, doubled, a tape echo and a plate.
        make ("Drifting Current Lead", P, 14, "Grunge,Rock,Doubler,Delay", {
            { rigLevel, -1 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 18 }, { photonOutput, -9 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, 0 },
            { deessOn, 1 }, { deessAmount, 6 },
            { driveOn, 1 }, { "mb1Style", warmTubeStyle }, { "mb1Drive", 0.25f },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", 1 }, { "eq1k", 1 }, { "eq2k", 1 }, { "eq4k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -5 }, { "dv2Gain", -5 }, { "dv1Pan", -0.5f }, { "dv2Pan", 0.5f },
            { "dv1Drift", 0.2f }, { "dv2Drift", 0.2f },
            { delayOn, 1 }, { delayType, tapeDelay }, { delaySync, eighth }, { delayFeedback, 0.25f }, { delayMix, 0.07f },
            { delayDuck, 0.7f }, { delayHighCut, 4500 },
            { reverbOn, 1 }, { reverbType, plate }, { reverbSize, 0.45f }, { reverbMix, 0.11f } }),

        // A long jam wail: the voice wailing over a long closing jam, grit
        // from a tube, doubled wide, a dotted echo and a hall.
        make ("Long Jam Wail", P, 15, "Grunge,Rock,Doubler,Saturation,Reverb", {
            { rigLevel, -4 },
            { compOn, 1 }, { compType, photonComp }, { photonInput, 22 }, { photonOutput, -11 }, { photonAttack, 4 }, { photonRelease, 5 }, { photonRatio, photonEight },
            { deessOn, 1 }, { deessAmount, 6 },
            { driveOn, 1 }, { "mb1Style", saturatedTubeStyle }, { "mb1Drive", 0.4f },
            { eqOn, 1 }, { "eq63", -6 }, { "eq125", 1 }, { "eq1k", 1 }, { "eq2k", 2 }, { "eq4k", 1 },
            { doublerOn, 1 }, { "dv1Gain", -4 }, { "dv2Gain", -4 }, { "dv1Pan", -0.8f }, { "dv2Pan", 0.8f },
            { "dv1Drift", 0.3f }, { "dv2Drift", 0.3f },
            { delayOn, 1 }, { delaySync, dottedEighth }, { delayPingPong, 1 }, { delayFeedback, 0.3f }, { delayMix, 0.08f },
            { delayDuck, 0.7f }, { delayHighCut, 5000 },
            { reverbOn, 1 }, { reverbType, hall }, { reverbSize, 0.6f }, { reverbMix, 0.14f } }),
    };
}
