#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <optional>

// Parameter IDs are saved in DAW projects and presets, so never rename one
// once it has shipped. Add new parameters with a new ID instead.
namespace ParamIDs
{
    inline constexpr auto inputGain    = "inputGain";
    inline constexpr auto outputGain   = "outputGain";
    // (Version 2's ampOn, ampNormalise and cabOn went with the NAM amp and
    // cabinet in 0.3.0. Never reuse those IDs.)

    // Phase 3
    inline constexpr auto presence       = "presence";

    inline constexpr auto compOn         = "compOn";
    inline constexpr auto compThreshold  = "compThreshold";
    inline constexpr auto compRatio      = "compRatio";
    inline constexpr auto compLevel      = "compLevel";
    inline constexpr auto gateThreshold  = "gateThreshold";

    inline constexpr auto driveOn        = "driveOn";   // SATURATION (MARS) on
    // The pedal drive's DRIVE, TONE and LEVEL: no longer parameters since
    // 0.14.0, when MARS replaced the pedal models. Read from older rigs and
    // projects only, which convertOldDrive turns into MARS. Never reuse these IDs.
    inline constexpr auto driveGain      = "driveGain";
    inline constexpr auto driveTone      = "driveTone";
    inline constexpr auto driveLevel     = "driveLevel";

    inline constexpr auto modOn          = "modOn";
    inline constexpr auto modType        = "modType";
    inline constexpr auto modRate        = "modRate";
    inline constexpr auto modDepth       = "modDepth";
    inline constexpr auto modMix         = "modMix";
    // AEON's (the flanger's) MANUAL and RES; it shares RATE and DEPTH.
    inline constexpr auto modManual      = "modManual";
    inline constexpr auto modRes         = "modRes";

    inline constexpr auto delayOn        = "delayOn";
    inline constexpr auto delayTime      = "delayTime";
    inline constexpr auto delaySync      = "delaySync";
    inline constexpr auto delayFeedback  = "delayFeedback";
    inline constexpr auto delayTone      = "delayTone";
    inline constexpr auto delayMix       = "delayMix";
    inline constexpr auto delayPingPong  = "delayPingPong";

    inline constexpr auto reverbOn       = "reverbOn";
    inline constexpr auto reverbSize     = "reverbSize";
    inline constexpr auto reverbTone     = "reverbTone";
    inline constexpr auto reverbMix      = "reverbMix";

    // Phase 4
    inline constexpr auto rigLevel       = "rigLevel";

    // Phase 5
    inline constexpr auto bypass         = "bypass";

    // Phase 6
    inline constexpr auto tunerOn          = "tunerOn";
    inline constexpr auto tunerMute        = "tunerMute";
    inline constexpr auto expression       = "expression";        // the pedal's position, 0 heel to 1 toe
    inline constexpr auto expressionTarget = "expressionTarget";  // what the pedal controls
    inline constexpr auto expressionMin    = "expressionMin";     // the target's setting at the heel...
    inline constexpr auto expressionMax    = "expressionMax";     // ...and at the toe

    // Phase 8: the 9-band graphic EQ, one band per octave.
    inline constexpr auto eqOn    = "eqOn";
    inline constexpr auto eqLevel = "eqLevel";
    inline const juce::StringArray eqBands { "eq63", "eq125", "eq250", "eq500", "eq1k", "eq2k", "eq4k", "eq8k", "eq16k" };
    inline const juce::StringArray eqBandNames { "63", "125", "250", "500", "1K", "2K", "4K", "8K", "16K" };

    // Modulation tempo sync: Off, or a note length at the tapped or host tempo.
    inline constexpr auto modSync = "modSync";

    // Version 10: the drive's model (Classic, Screamer, Muff, Face, and MARS
    // from 0.13.0; legacy since 0.14.0, like driveGain), and the FILTER /
    // PITCH block: auto-wah and pitch shifter.
    inline constexpr auto driveModel    = "driveModel";
    inline constexpr auto fpOn          = "fpOn";
    inline constexpr auto fpType        = "fpType";
    inline constexpr auto fpSensitivity = "fpSensitivity";   // auto-wah
    inline constexpr auto fpResonance   = "fpResonance";     // auto-wah
    inline constexpr auto fpInterval    = "fpInterval";      // pitch shifter, in semitones
    inline constexpr auto fpMix         = "fpMix";

    // Version 12: the LO-FI block, last in the chain.
    inline constexpr auto lofiOn     = "lofiOn";
    inline constexpr auto lofiType   = "lofiType";
    inline constexpr auto lofiAmount = "lofiAmount";
    inline constexpr auto lofiNoise  = "lofiNoise";
    inline constexpr auto lofiMix    = "lofiMix";
    inline constexpr auto lofiLevel  = "lofiLevel";

    // Version 15: the reverb's TYPE and PRE-DELAY.
    inline constexpr auto reverbType     = "reverbType";
    inline constexpr auto reverbPreDelay = "reverbPreDelay";

    // Version 16: the delay's TYPE.
    inline constexpr auto delayType = "delayType";

    // Version 17: DUCK for the delay and the reverb.
    inline constexpr auto delayDuck  = "delayDuck";
    inline constexpr auto reverbDuck = "reverbDuck";

    // Version 21: the FILTER / PITCH block's Whammy-Wah program (the XP-100's 01 to 28).
    // (Version 20's "fpWhammyWahMode" was replaced by this the same day.)
    inline constexpr auto fpXpProgram = "fpXpProgram";

    // Version 22: the Daybreak compressor (COMP / GATE's second TYPE).
    inline constexpr auto compType      = "compType";
    inline constexpr auto compAmount    = "compAmount";      // COMP
    inline constexpr auto compDetail    = "compDetail";      // DETAIL
    inline constexpr auto compMix       = "compMix";         // MIX
    inline constexpr auto compIntensity = "compIntensity";   // INTENSITY

    // Version 23: the GLITCH block.
    inline constexpr auto glitchOn       = "glitchOn";
    inline constexpr auto glitchMode     = "glitchMode";
    inline constexpr auto glitchPosition = "glitchPosition";   // Post (after the amp) or Pre (before it)
    inline constexpr auto glitchSync     = "glitchSync";
    inline constexpr auto glitchTime     = "glitchTime";       // LENGTH in ms, when not synced
    inline constexpr auto glitchDivision = "glitchDivision";   // LENGTH as a note value, when synced
    inline constexpr auto glitchDensity  = "glitchDensity";
    inline constexpr auto glitchPitch    = "glitchPitch";
    inline constexpr auto glitchChaos    = "glitchChaos";
    inline constexpr auto glitchMix      = "glitchMix";

    // Version 24: the PITCH block (first in the chain).
    inline constexpr auto pitchTune      = "pitchTune";        // TUNING, semitones; 0 is standard (off)
    inline constexpr auto octOn          = "octOn";            // the OCTAVER
    inline constexpr auto octSubTwo      = "octSubTwo";
    inline constexpr auto octSubOne      = "octSubOne";
    inline constexpr auto octDry         = "octDry";
    inline constexpr auto octUp          = "octUp";
    inline constexpr auto freezeOn       = "freezeOn";         // FREEZE
    inline constexpr auto freezeLevel    = "freezeLevel";
    inline constexpr auto freezeAttack   = "freezeAttack";     // ms
    inline constexpr auto freezeRelease  = "freezeRelease";    // ms

    // Version 25: LOW CUT on the INPUT strip, a high-pass filter first in
    // the chain for the microphone. Its lowest setting is OFF.
    inline constexpr auto inputLowCut = "inputLowCut";   // Hz

    // Version 26: the DOUBLER stomp (until then MODULATION's Doubler type).
    inline constexpr auto doublerOn    = "doublerOn";
    // Version 26's RATE, DEPTH and MIX: no longer parameters since 0.7.0's
    // four-voice DOUBLER. Read from older rigs and projects only, which
    // convertOldDoubler turns into voices. Never reuse these IDs.
    inline constexpr auto doublerRate  = "doublerRate";
    inline constexpr auto doublerDepth = "doublerDepth";
    inline constexpr auto doublerMix   = "doublerMix";

    // Version 27: the master LIMITER under OUTPUT, last in the chain. Its
    // top setting is OFF.
    inline constexpr auto outputLimiter = "outputLimiter";   // the ceiling, dB

    // Version 28: the DE-ESSER, in DYNAMICS after the compressor.
    inline constexpr auto deessOn     = "deessOn";
    inline constexpr auto deessFreq   = "deessFreq";     // Hz
    inline constexpr auto deessAmount = "deessAmount";   // dB: the most it turns the esses down
    inline constexpr auto deessListen = "deessListen";   // hear only what's taken away

    // Version 29: HARMONY, on the PITCH stomp (inspired by Waves Harmony).
    inline constexpr auto harmonyOn             = "harmonyOn";
    inline constexpr auto harmonyKey            = "harmonyKey";            // C to B
    inline constexpr auto harmonyScale          = "harmonyScale";
    inline constexpr auto harmonySource         = "harmonySource";         // dB: the singer's own voice
    inline constexpr auto harmonyLevel          = "harmonyLevel";          // dB: all the harmony voices
    inline constexpr auto harmonyCorrectFormant = "harmonyCorrectFormant"; // 0 to 1
    inline constexpr auto harmonySpread         = "harmonySpread";         // 0 to 1
    inline constexpr auto harmonyCorrect        = "harmonyCorrect";        // version 38: CORRECT, Off / Natural / Hard
    // Each of the eight voices: "hv1Pitch", "hv2Pan"... (voice 1 to 8).
    inline const juce::StringArray harmonyVoiceParts { "Active", "On", "Pitch", "Fine", "Formant", "Correct", "Pan", "Level" };
    inline juce::String harmonyVoice (int voice, const juce::String& part) { return "hv" + juce::String (voice + 1) + part; }

    // Version 30: the four-voice DOUBLER (inspired by Waves Doubler).
    inline constexpr auto doublerDirectOn   = "doublerDirectOn";
    inline constexpr auto doublerDirectGain = "doublerDirectGain";
    inline constexpr auto doublerDirectPan  = "doublerDirectPan";
    inline constexpr auto doublerFbHighPass = "doublerFbHighPass";
    inline constexpr auto doublerLowOn      = "doublerLowOn";
    inline constexpr auto doublerLowGain    = "doublerLowGain";
    inline constexpr auto doublerLowFreq    = "doublerLowFreq";
    inline constexpr auto doublerHighOn     = "doublerHighOn";
    inline constexpr auto doublerHighGain   = "doublerHighGain";
    inline constexpr auto doublerHighFreq   = "doublerHighFreq";
    inline constexpr auto doublerOutput     = "doublerOutput";

    // Version 31: Revival, the compressor's third TYPE (inspired by Waves
    // Renaissance Vox): its GATE, COMPRESSION and GAIN.
    inline constexpr auto revivalGate        = "revivalGate";
    inline constexpr auto revivalCompression = "revivalCompression";
    inline constexpr auto revivalGain        = "revivalGain";
    // Version 33: the delay's controls inspired by Waves H-Delay.
    inline constexpr auto delayDual       = "delayDual";
    inline constexpr auto delayTimeRight  = "delayTimeRight";
    inline constexpr auto delaySyncRight  = "delaySyncRight";
    inline constexpr auto delayPingStart  = "delayPingStart";
    inline constexpr auto delayPhaseLeft  = "delayPhaseLeft";
    inline constexpr auto delayPhaseRight = "delayPhaseRight";
    inline constexpr auto delayLowCut     = "delayLowCut";
    inline constexpr auto delayHighCut    = "delayHighCut";
    inline constexpr auto delayModDepth   = "delayModDepth";
    inline constexpr auto delayModRate    = "delayModRate";
    inline constexpr auto delayAnalog     = "delayAnalog";
    inline constexpr auto delayLoFi       = "delayLoFi";
    // Version 34: the reverb's controls inspired by the Lexicon LXP reverbs.
    inline constexpr auto reverbDecay     = "reverbDecay";
    inline constexpr auto reverbBassRt    = "reverbBassRt";
    inline constexpr auto reverbCrossover = "reverbCrossover";
    inline constexpr auto reverbRolloff   = "reverbRolloff";
    inline constexpr auto reverbDiffusion = "reverbDiffusion";
    inline constexpr auto reverbEarly     = "reverbEarly";
    inline constexpr auto reverbLevel     = "reverbLevel";
    inline constexpr auto reverbSpread    = "reverbSpread";
    inline constexpr auto reverbShape     = "reverbShape";
    inline constexpr auto reverbSpin      = "reverbSpin";
    inline constexpr auto reverbWander    = "reverbWander";
    // Version 35: MARS, SATURATION's multi-band MODEL (inspired by FabFilter Saturn 2).
    inline constexpr auto marsBands   = "marsBands";
    inline constexpr auto marsCross1  = "marsCross1";
    inline constexpr auto marsCross2  = "marsCross2";
    inline constexpr auto marsCross3  = "marsCross3";
    inline constexpr auto marsSlope   = "marsSlope";
    inline constexpr auto marsInput   = "marsInput";
    inline constexpr auto marsOutput  = "marsOutput";
    inline constexpr auto marsMix     = "marsMix";
    inline constexpr auto marsQuality = "marsQuality";
    // Each of the four bands: "mb1Drive", "mb3Style"... (band 1 to 4).
    inline const juce::StringArray marsBandParts { "On", "Style", "Drive", "Dynamics", "Feedback", "FbFreq",
                                                   "Bass", "Mid", "Treble", "Presence", "Mix", "Level" };
    inline juce::String marsBand (int band, const juce::String& part) { return "mb" + juce::String (band + 1) + part; }
    // Version 37: MARS's modulation (stage 2). Sources and slots, numbered from 1:
    // "mmLfo1Rate", "mmLfo2S5Value" (XLFO 2's step 5), "mmEg1Attack",
    // "mmEf2Mode", "mmMidi1Input", "mmXy1X", "mmSlot12Target"...
    inline const juce::StringArray marsLfoParts { "Steps", "Rate", "Sync", "Offset", "Balance", "Glide", "Phase", "Midi" };
    inline const juce::StringArray marsStepParts { "Value", "Glide", "Curve", "Random" };
    inline const juce::StringArray marsEgParts { "Trigger", "Threshold", "Delay", "Attack", "Decay", "Sustain", "Hold", "Release",
                                                 "AttackSlope", "DecaySlope", "ReleaseSlope", "Range" };
    inline const juce::StringArray marsEfParts { "Trigger", "Attack", "Release", "Mode", "Gain" };
    inline const juce::StringArray marsMidiParts { "Input", "Controller", "Response" };
    inline const juce::StringArray marsXyParts { "X", "Y", "Mode", "Range" };
    inline const juce::StringArray marsSlotParts { "Source", "Target", "Amount" };
    inline juce::String marsLfo (int lfo, const juce::String& part) { return "mmLfo" + juce::String (lfo + 1) + part; }
    inline juce::String marsStep (int lfo, int step, const juce::String& part) { return "mmLfo" + juce::String (lfo + 1) + "S" + juce::String (step + 1) + part; }
    inline juce::String marsEg (int eg, const juce::String& part) { return "mmEg" + juce::String (eg + 1) + part; }
    inline juce::String marsEf (int ef, const juce::String& part) { return "mmEf" + juce::String (ef + 1) + part; }
    inline juce::String marsMidi (int midi, const juce::String& part) { return "mmMidi" + juce::String (midi + 1) + part; }
    inline juce::String marsXy (int xy, const juce::String& part) { return "mmXy" + juce::String (xy + 1) + part; }
    inline juce::String marsSlot (int slot, const juce::String& part) { return "mmSlot" + juce::String (slot + 1) + part; }
    // The parameter a modulation target moves ("mb2Drive", "marsCross1", "mmLfo1Rate"...).
    juce::String marsTargetParameter (int target);
    // Each of the four voices: "dv1Gain", "dv2Detune"... (voice 1 to 4).
    // Drift (version 32) is appended: only ever add to the end.
    inline const juce::StringArray doublerVoiceParts { "On", "Gain", "Pan", "Delay", "Feedback", "Octave", "Detune", "Depth", "Rate", "Drift" };
    inline juce::String doublerVoice (int voice, const juce::String& part) { return "dv" + juce::String (voice + 1) + part; }

    // Global settings: kept when switching rigs. Everything else is part of a rig.
    // The expression pedal's position is global for the same reason a real
    // pedal's is: it's where your foot is, not part of the sound. What the
    // pedal is assigned to, and its range, are saved with each rig. LOW CUT
    // goes with INPUT: it's set for your microphone and room, not a sound.
    // LIMITER goes with OUTPUT, for the same reason. HARMONY's KEY and SCALE
    // belong to the song, not the sound, so a rig change keeps them (as
    // Waves Harmony's lock does).
    inline const juce::StringArray notInRigs { inputGain, outputGain, bypass, tunerOn, tunerMute, expression, inputLowCut, outputLimiter,
                                               harmonyKey, harmonyScale };
}

namespace ExpressionTargets
{
    // Saved by number: only ever add to the end.
    enum { off, volume, wah, morph, pitch, loFi, whammyWah, glitch, talkBox };
    inline const juce::StringArray names { "Off", "Volume", "Wah / Filter", "Morph FX", "Pitch / Whammy", "Lo-Fi", "Whammy-Wah", "Glitch", "Talk Box" };
}

namespace ParamRanges
{
    inline constexpr float gainMinDb = -24.0f;
    inline constexpr float gainMaxDb = 24.0f;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

// Settings saved before 0.3.0 with MODULATION's TYPE on Doubler: moves them to
// the DOUBLER stomp (same RATE, DEPTH and MIX, switched on if MODULATION was)
// and leaves MODULATION off on Chorus, so they sound as they did. `get`
// returns a parameter's saved value (or nothing), and `set` stores one.
void moveOldDoublerSettings (const std::function<std::optional<float> (const char*)>& get,
                             const std::function<void (const char*, float)>& set);

// Settings saved before 0.7.0 with the two-take DOUBLER (RATE, DEPTH, MIX):
// sets up voices 1 and 2 to sound close to it (its takes 19 and 27 ms late,
// one each side, at MIX's level, with a wobble of about the same depth and
// speed; the new voices are truly detuned, so it's close rather than exact).
// Settings saved with 0.7.0's or 0.8.0's DOUBLER (before its DRIFT and
// stronger starting settings in 0.9.0): no DRIFT, and any voice setting not
// saved keeps the value it started with then, so they sound as they did.
// Settings saved before 0.14.0 with one of the pedal drive models (Classic,
// Screamer, Muff Fuzz, Face Fuzz): sets MARS up to sound close to it, its
// DRIVE, TONE and LEVEL carried over (a different engine: close, not the
// same). Nothing changes if MARS was already picked, or if the drive was
// off and never touched.
void convertOldDrive (const std::function<std::optional<float> (const juce::String&)>& get,
                      const std::function<void (const juce::String&, float)>& set);

void keepDoublerAsSaved (const std::function<std::optional<float> (const juce::String&)>& get,
                         const std::function<void (const juce::String&, float)>& set);

void convertOldDoubler (const std::function<std::optional<float> (const juce::String&)>& get,
                        const std::function<void (const juce::String&, float)>& set);
