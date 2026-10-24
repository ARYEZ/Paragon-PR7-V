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

    inline constexpr auto driveOn        = "driveOn";
    inline constexpr auto driveGain      = "driveGain";
    inline constexpr auto driveTone      = "driveTone";
    inline constexpr auto driveLevel     = "driveLevel";

    inline constexpr auto modOn          = "modOn";
    inline constexpr auto modType        = "modType";
    inline constexpr auto modRate        = "modRate";
    inline constexpr auto modDepth       = "modDepth";
    inline constexpr auto modMix         = "modMix";

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

    // Version 10: the drive's model (Tube Screamer or the original), and
    // the FILTER / PITCH block: auto-wah and pitch shifter.
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
    inline constexpr auto doublerRate  = "doublerRate";
    inline constexpr auto doublerDepth = "doublerDepth";
    inline constexpr auto doublerMix   = "doublerMix";

    // Global settings: kept when switching rigs. Everything else is part of a rig.
    // The expression pedal's position is global for the same reason a real
    // pedal's is: it's where your foot is, not part of the sound. What the
    // pedal is assigned to, and its range, are saved with each rig. LOW CUT
    // goes with INPUT: it's set for your microphone and room, not a sound.
    inline const juce::StringArray notInRigs { inputGain, outputGain, bypass, tunerOn, tunerMute, expression, inputLowCut };
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
