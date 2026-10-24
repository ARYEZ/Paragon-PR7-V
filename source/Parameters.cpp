#include "Parameters.h"

#include "dsp/effects/CompGate.h"
#include "dsp/effects/Delay.h"
#include "dsp/effects/Doubler.h"
#include "dsp/effects/Drive.h"
#include "dsp/effects/FilterPitch.h"
#include "dsp/effects/LoFi.h"
#include "dsp/effects/Glitch.h"
#include "dsp/effects/GraphicEq.h"
#include "dsp/effects/Harmony.h"
#include "dsp/effects/Limiter.h"
#include "dsp/effects/LowCut.h"
#include "dsp/effects/Modulation.h"
#include "dsp/effects/Reverb.h"

namespace
{
    using Range = juce::NormalisableRange<float>;
    using ToText = std::function<juce::String (float, int)>;

    const ToText asDb = [] (float value, int) { return juce::String (value, 1) + " dB"; };
    const ToText asKnob = [] (float value, int) { return juce::String (value, 1); };
    const ToText asPercent = [] (float value, int) { return juce::String (juce::roundToInt (value * 100.0f)) + "%"; };
    const ToText asHz = [] (float value, int) { return juce::String (value, value < 1.0f ? 2 : 1) + " Hz"; };
    const ToText asMs = [] (float value, int) { return juce::String (juce::roundToInt (value)) + " ms"; };
    const ToText asTime = [] (float ms, int) { return ms < 1000.0f ? juce::String (juce::roundToInt (ms)) + " ms" : juce::String (ms / 1000.0f, 1) + " s"; };
    const ToText asSemitones = [] (float value, int)
    {
        const auto semitones = juce::roundToInt (value);
        if (semitones == 0)
            return juce::String ("Unison");
        if (std::abs (semitones) % 12 == 0)
            return juce::String (semitones > 0 ? "+" : "-") + juce::String (std::abs (semitones) / 12) + " oct";
        return (semitones > 0 ? "+" : "") + juce::String (semitones) + " st";
    };
    const ToText asRatio = [] (float value, int) { return juce::String (value, 1) + ":1"; };
    // TUNING: how far the whole voice is shifted, in semitones. (PR7-G
    // showed guitar tunings here: "E standard", "Capo 2"...)
    const ToText asTuning = [] (float value, int)
    {
        const auto semitones = juce::roundToInt (value);
        return (semitones > 0 ? "+" : "") + juce::String (semitones) + " st";
    };
    const ToText asLowCut = [] (float hz, int)
    {
        return effects::LowCut::isOff (hz) ? juce::String ("Off") : juce::String (juce::roundToInt (hz)) + " Hz";
    };
    const ToText asCeiling = [] (float db, int)
    {
        return effects::Limiter::isOff (db) ? juce::String ("Off") : juce::String (db, 1) + " dB";
    };
    const ToText asGate = [] (float value, int)
    {
        return value <= effects::CompGate::gateOffDb ? juce::String ("Off") : juce::String (juce::roundToInt (value)) + " dB";
    };

    // Range with its centre value at the middle of the knob's travel, for
    // ranges where the useful settings are bunched at one end (rate, time).
    Range skewed (float min, float max, float centre, float step = 0.0f)
    {
        Range range { min, max, step };
        range.setSkewForCentre (centre);
        return range;
    }

    std::unique_ptr<juce::AudioParameterFloat> makeFloat (const char* id, const juce::String& name, Range range,
                                                          float defaultValue, int versionHint, const ToText& toText)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, versionHint }, name, range, defaultValue,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (toText));
    }

    // Version hint 1: tells hosts which plugin version introduced the parameter.
    std::unique_ptr<juce::AudioParameterFloat> makeGainParameter (const char* id, const juce::String& name)
    {
        return makeFloat (id, name, { ParamRanges::gainMinDb, ParamRanges::gainMaxDb, 0.1f }, 0.0f, 1, asDb);
    }

    std::unique_ptr<juce::AudioParameterBool> makeSwitch (const char* id, const juce::String& name, int versionHint,
                                                          bool defaultValue = true)
    {
        return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, versionHint }, name, defaultValue);
    }

    std::unique_ptr<juce::AudioParameterChoice> makeChoice (const char* id, const juce::String& name, int versionHint,
                                                            const juce::StringArray& choices, int defaultIndex)
    {
        return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, versionHint }, name, choices, defaultIndex);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (makeGainParameter (ParamIDs::inputGain, "Input"));
    layout.add (makeGainParameter (ParamIDs::outputGain, "Output"));

    // Added in plugin version 3 (Phase 3). Effects default to off, so
    // projects saved with earlier versions sound exactly the same.
    constexpr int v3 = 3;
    layout.add (makeFloat (ParamIDs::presence, "Presence", { -10.0f, 10.0f, 0.1f }, 0.0f, v3, asDb));

    layout.add (makeSwitch (ParamIDs::compOn, "Comp On", v3, false));
    layout.add (makeFloat (ParamIDs::compThreshold, "Comp Threshold", { -40.0f, 0.0f, 0.1f }, -18.0f, v3, asDb));
    layout.add (makeFloat (ParamIDs::compRatio, "Comp Ratio", skewed (1.0f, 20.0f, 4.0f, 0.1f), 4.0f, v3, asRatio));
    layout.add (makeFloat (ParamIDs::compLevel, "Comp Level", { -12.0f, 24.0f, 0.1f }, 0.0f, v3, asDb));
    layout.add (makeFloat (ParamIDs::gateThreshold, "Gate Threshold", { effects::CompGate::gateOffDb, -20.0f, 1.0f },
                           effects::CompGate::gateOffDb, v3, asGate));

    layout.add (makeSwitch (ParamIDs::driveOn, "Drive On", v3, false));
    layout.add (makeFloat (ParamIDs::driveGain, "Drive", { 0.0f, 10.0f, 0.1f }, 5.0f, v3, asKnob));
    layout.add (makeFloat (ParamIDs::driveTone, "Drive Tone", { 0.0f, 10.0f, 0.1f }, 5.0f, v3, asKnob));
    layout.add (makeFloat (ParamIDs::driveLevel, "Drive Level", { -24.0f, 12.0f, 0.1f }, 0.0f, v3, asDb));

    layout.add (makeSwitch (ParamIDs::modOn, "Mod On", v3, false));
    layout.add (makeChoice (ParamIDs::modType, "Mod Type", v3, effects::Modulation::typeNames, 0));
    layout.add (makeFloat (ParamIDs::modRate, "Mod Rate", skewed (0.05f, 10.0f, 1.0f, 0.01f), 0.8f, v3, asHz));
    layout.add (makeFloat (ParamIDs::modDepth, "Mod Depth", { 0.0f, 1.0f, 0.01f }, 0.45f, v3, asPercent));
    layout.add (makeFloat (ParamIDs::modMix, "Mod Mix", { 0.0f, 1.0f, 0.01f }, 0.5f, v3, asPercent));

    layout.add (makeSwitch (ParamIDs::delayOn, "Delay On", v3, false));
    layout.add (makeFloat (ParamIDs::delayTime, "Delay Time", skewed (1.0f, effects::Delay::maxDelaySeconds * 1000.0f, 400.0f, 1.0f),
                           375.0f, v3, asMs));
    layout.add (makeChoice (ParamIDs::delaySync, "Delay Sync", v3, effects::Delay::syncNames, 3));   // 1/4, as in the mockup
    // Up to 200% since 0.10.0 (was 95%): saved by value, so saved settings sound the same.
    layout.add (makeFloat (ParamIDs::delayFeedback, "Delay Feedback", { 0.0f, effects::Delay::maxFeedback, 0.01f }, 0.35f, v3, asPercent));
    layout.add (makeFloat (ParamIDs::delayTone, "Delay Tone", { 0.0f, 10.0f, 0.1f }, 7.0f, v3, asKnob));
    layout.add (makeFloat (ParamIDs::delayMix, "Delay Mix", { 0.0f, 1.0f, 0.01f }, 0.25f, v3, asPercent));
    layout.add (makeSwitch (ParamIDs::delayPingPong, "Delay Ping-Pong", v3, false));

    layout.add (makeSwitch (ParamIDs::reverbOn, "Reverb On", v3, false));
    layout.add (makeFloat (ParamIDs::reverbSize, "Reverb Size", { 0.0f, 1.0f, 0.01f }, 0.5f, v3, asPercent));
    layout.add (makeFloat (ParamIDs::reverbTone, "Reverb Tone", { 0.0f, 10.0f, 0.1f }, 6.0f, v3, asKnob));
    layout.add (makeFloat (ParamIDs::reverbMix, "Reverb Mix", { 0.0f, 1.0f, 0.01f }, 0.28f, v3, asPercent));

    // Added in plugin version 4 (Phase 4): the RIG LEVEL knob (first called OUTPUT LEVEL) on the
    // mockup's pedal face, saved with each rig to balance rigs' volumes.
    layout.add (makeFloat (ParamIDs::rigLevel, "Rig Level", { -24.0f, 12.0f, 0.1f }, 0.0f, 4, asDb));

    // Added in plugin version 5 (Phase 5): the pedal's BYPASS button. It's
    // also the plugin's official bypass, so the DAW's bypass uses it too.
    layout.add (makeSwitch (ParamIDs::bypass, "Bypass", 5, false));

    // Added in plugin version 6 (Phase 6): the tuner and the expression pedal.
    constexpr int v6 = 6;
    layout.add (makeSwitch (ParamIDs::tunerOn, "Tuner", v6, false));
    layout.add (makeSwitch (ParamIDs::tunerMute, "Tuner Mutes Output", v6, true));
    // Heel down (0%) by default, where a real pedal rests: every target starts
    // at its "off" end (MORPH dry, WAH at the heel, PITCH unshifted, LO-FI full
    // band). Assigned to VOLUME it's silent until rocked forward, as a real
    // volume pedal is.
    layout.add (makeFloat (ParamIDs::expression, "Expression", { 0.0f, 1.0f, 0.001f }, 0.0f, v6, asPercent));
    layout.add (makeChoice (ParamIDs::expressionTarget, "Expression Target", v6, ExpressionTargets::names, ExpressionTargets::off));
    layout.add (makeFloat (ParamIDs::expressionMin, "Expression Min", { 0.0f, 1.0f, 0.01f }, 0.0f, v6, asPercent));
    layout.add (makeFloat (ParamIDs::expressionMax, "Expression Max", { 0.0f, 1.0f, 0.01f }, 1.0f, v6, asPercent));

    // Added in plugin version 8: the graphic EQ. Off by default, so existing
    // projects and rigs sound the same.
    constexpr int v8 = 8;
    layout.add (makeSwitch (ParamIDs::eqOn, "EQ On", v8, false));
    for (int band = 0; band < ParamIDs::eqBands.size(); ++band)
        layout.add (makeFloat (ParamIDs::eqBands[band].toRawUTF8(), "EQ " + ParamIDs::eqBandNames[band] + " Hz",
                               { -effects::GraphicEq::maxGainDb, effects::GraphicEq::maxGainDb, 0.1f }, 0.0f, v8, asDb));
    layout.add (makeFloat (ParamIDs::eqLevel, "EQ Level", { -12.0f, 12.0f, 0.1f }, 0.0f, v8, asDb));

    // Added in plugin version 9: modulation tempo sync. Off by default, so
    // existing rigs keep their RATE. (The six new modulation types were
    // added to the end of Mod Type's list, so saved choices keep their meaning.)
    layout.add (makeChoice (ParamIDs::modSync, "Mod Sync", 9, effects::Delay::syncNames, 0));

    // Added in plugin version 10. The drive defaults to the Tube Screamer
    // model; projects and rigs saved before it existed are given the
    // original model when they load (see ParagonX1AudioProcessor::setStateInformation
    // and Rig::fromXml), so they sound as they did.
    constexpr int v10 = 10;
    layout.add (makeChoice (ParamIDs::driveModel, "Drive Model", v10, effects::Drive::modelNames, (int) effects::Drive::Model::screamer));
    layout.add (makeSwitch (ParamIDs::fpOn, "Filter/Pitch On", v10, false));
    layout.add (makeChoice (ParamIDs::fpType, "Filter/Pitch Type", v10, effects::FilterPitch::typeNames, 0));
    layout.add (makeFloat (ParamIDs::fpSensitivity, "Auto-Wah Sensitivity", { 0.0f, 10.0f, 0.1f }, 5.0f, v10, asKnob));
    layout.add (makeFloat (ParamIDs::fpResonance, "Auto-Wah Resonance", { 0.0f, 10.0f, 0.1f }, 5.0f, v10, asKnob));
    layout.add (makeFloat (ParamIDs::fpInterval, "Pitch Interval", { -24.0f, 24.0f, 1.0f }, 12.0f, v10, asSemitones));
    layout.add (makeFloat (ParamIDs::fpMix, "Filter/Pitch Mix", { 0.0f, 1.0f, 0.01f }, 1.0f, v10, asPercent));

    // Added in plugin version 12: the LO-FI block. Off by default.
    constexpr int v12 = 12;
    layout.add (makeSwitch (ParamIDs::lofiOn, "Lo-Fi On", v12, false));
    layout.add (makeChoice (ParamIDs::lofiType, "Lo-Fi Type", v12, effects::LoFi::typeNames, 0));
    layout.add (makeFloat (ParamIDs::lofiAmount, "Lo-Fi Amount", { 0.0f, 10.0f, 0.1f }, 5.0f, v12, asKnob));
    layout.add (makeFloat (ParamIDs::lofiNoise, "Lo-Fi Noise", { 0.0f, 10.0f, 0.1f }, 3.0f, v12, asKnob));
    layout.add (makeFloat (ParamIDs::lofiMix, "Lo-Fi Mix", { 0.0f, 1.0f, 0.01f }, 1.0f, v12, asPercent));
    layout.add (makeFloat (ParamIDs::lofiLevel, "Lo-Fi Level", { -12.0f, 12.0f, 0.1f }, 0.0f, v12, asDb));

    // Added in plugin version 15: reverb types and pre-delay. New rigs get
    // DIGITAL (the 80s arena-rock reverb); projects and rigs saved before it existed load as CLASSIC, the
    // original reverb (see setStateInformation and Rig::fromXml), so they
    // sound as they did.
    constexpr int v15 = 15;
    layout.add (makeChoice (ParamIDs::reverbType, "Reverb Type", v15, effects::Reverb::typeNames, (int) effects::Reverb::Type::digital));
    layout.add (makeFloat (ParamIDs::reverbPreDelay, "Reverb Pre-Delay", { 0.0f, effects::Reverb::maxPreDelayMs, 1.0f }, 20.0f, v15, asMs));

    // Added in plugin version 16: delay types. DIGITAL, the default, is the
    // original delay, so earlier projects and rigs sound the same.
    layout.add (makeChoice (ParamIDs::delayType, "Delay Type", 16, effects::Delay::typeNames, (int) effects::Delay::Type::digital));

    // Added in plugin version 17: DUCK for the delay and the reverb. Off by
    // default, so earlier projects and rigs sound the same.
    layout.add (makeFloat (ParamIDs::delayDuck, "Delay Duck", { 0.0f, 1.0f, 0.01f }, 0.0f, 17, asPercent));
    layout.add (makeFloat (ParamIDs::reverbDuck, "Reverb Duck", { 0.0f, 1.0f, 0.01f }, 0.0f, 17, asPercent));

    layout.add (makeChoice (ParamIDs::fpXpProgram, "Whammy-Wah Program", 21, effects::FilterPitch::xpProgramNames(), 1));

    constexpr int v22 = 22;
    layout.add (makeChoice (ParamIDs::compType, "Comp Type", v22, effects::CompGate::typeNames, 0));
    layout.add (makeFloat (ParamIDs::compAmount, "Comp Amount", { 0.0f, 10.0f, 0.1f }, 5.0f, v22, asKnob));
    layout.add (makeFloat (ParamIDs::compDetail, "Comp Detail", { 0.0f, 10.0f, 0.1f }, 5.0f, v22, asKnob));
    layout.add (makeFloat (ParamIDs::compMix, "Comp Mix", { 0.0f, 1.0f, 0.01f }, 1.0f, v22, asPercent));
    layout.add (makeSwitch (ParamIDs::compIntensity, "Comp Intensity", v22, false));

    constexpr int v23 = 23;
    layout.add (makeSwitch (ParamIDs::glitchOn, "Glitch On", v23, false));
    layout.add (makeChoice (ParamIDs::glitchMode, "Glitch Mode", v23, effects::Glitch::modeNames, (int) effects::Glitch::Mode::sequence));
    layout.add (makeChoice (ParamIDs::glitchPosition, "Glitch Position", v23, effects::Glitch::positionNames, 0));
    layout.add (makeSwitch (ParamIDs::glitchSync, "Glitch Sync", v23, true));
    layout.add (makeFloat (ParamIDs::glitchTime, "Glitch Length", skewed (10.0f, 1000.0f, 150.0f, 1.0f), 120.0f, v23, asMs));
    layout.add (makeChoice (ParamIDs::glitchDivision, "Glitch Length (Sync)", v23, effects::Glitch::syncNames, 3));
    layout.add (makeFloat (ParamIDs::glitchDensity, "Glitch Density", { 0.0f, 10.0f, 0.1f }, 5.0f, v23, asKnob));
    layout.add (makeFloat (ParamIDs::glitchPitch, "Glitch Pitch", { -12.0f, 12.0f, 1.0f }, 0.0f, v23, asSemitones));
    layout.add (makeFloat (ParamIDs::glitchChaos, "Glitch Chaos", { 0.0f, 10.0f, 0.1f }, 3.0f, v23, asKnob));
    layout.add (makeFloat (ParamIDs::glitchMix, "Glitch Mix", { 0.0f, 1.0f, 0.01f }, 0.5f, v23, asPercent));

    constexpr int v24 = 24;
    layout.add (makeFloat (ParamIDs::pitchTune, "Tuning", { -12.0f, 12.0f, 1.0f }, 0.0f, v24, asTuning));
    layout.add (makeSwitch (ParamIDs::octOn, "Octaver On", v24, false));
    layout.add (makeFloat (ParamIDs::octSubTwo, "Octaver 2 Down", { 0.0f, 1.0f, 0.01f }, 0.0f, v24, asPercent));
    layout.add (makeFloat (ParamIDs::octSubOne, "Octaver 1 Down", { 0.0f, 1.0f, 0.01f }, 0.5f, v24, asPercent));
    layout.add (makeFloat (ParamIDs::octDry, "Octaver Dry", { 0.0f, 1.0f, 0.01f }, 1.0f, v24, asPercent));
    layout.add (makeFloat (ParamIDs::octUp, "Octaver 1 Up", { 0.0f, 1.0f, 0.01f }, 0.0f, v24, asPercent));
    layout.add (makeSwitch (ParamIDs::freezeOn, "Freeze", v24, false));
    layout.add (makeFloat (ParamIDs::freezeLevel, "Freeze Level", { 0.0f, 1.0f, 0.01f }, 0.7f, v24, asPercent));
    layout.add (makeFloat (ParamIDs::freezeAttack, "Freeze Attack", skewed (10.0f, 5000.0f, 300.0f, 1.0f), 300.0f, v24, asTime));
    layout.add (makeFloat (ParamIDs::freezeRelease, "Freeze Release", skewed (50.0f, 8000.0f, 1500.0f, 1.0f), 1500.0f, v24, asTime));

    // Added in plugin version 25: LOW CUT for the microphone. On at 80 Hz by
    // default, the usual starting point for a voice: below the lowest sung
    // notes, but above the rumble a mic stand picks up.
    layout.add (makeFloat (ParamIDs::inputLowCut, "Low Cut",
                           skewed (effects::LowCut::offHz, effects::LowCut::maxHz, 100.0f, 1.0f), 80.0f, 25, asLowCut));

    // Added in plugin version 26: the DOUBLER stomp. Off by default. (Its
    // version 26 RATE, DEPTH and MIX went with 0.7.0's four voices.)
    layout.add (makeSwitch (ParamIDs::doublerOn, "Doubler On", 26, false));

    // Added in plugin version 27: the master LIMITER. On at -1 dB by
    // default, the usual safety margin under digital full scale.
    layout.add (makeFloat (ParamIDs::outputLimiter, "Limiter", { effects::Limiter::minCeilingDb, effects::Limiter::offDb, 0.1f },
                           -1.0f, 27, asCeiling));

    // Added in plugin version 28: the DE-ESSER. Off by default. 6 kHz is
    // where most esses sit; 6 dB is a firm but natural first setting.
    constexpr int v28 = 28;
    const ToText asKHz = [] (float hz, int) { return juce::String (hz / 1000.0f, 1) + " kHz"; };
    layout.add (makeSwitch (ParamIDs::deessOn, "De-Esser On", v28, false));
    layout.add (makeFloat (ParamIDs::deessFreq, "De-Esser Frequency", skewed (2000.0f, 12000.0f, 6000.0f, 10.0f), 6000.0f, v28, asKHz));
    layout.add (makeFloat (ParamIDs::deessAmount, "De-Esser Amount", { 0.0f, 18.0f, 0.1f }, 6.0f, v28, asDb));
    layout.add (makeSwitch (ParamIDs::deessListen, "De-Esser Listen", v28, false));

    // Added in plugin version 29: HARMONY. Off by default. Two voices are
    // set up to start with, a third above on the left and a fifth above on
    // the right: the classic three-part harmony.
    constexpr int v29 = 29;
    const ToText asLevel = [] (float db, int) { return db <= effects::Harmony::silentDb ? juce::String ("Off") : juce::String (db, 1) + " dB"; };
    const ToText asInterval = [] (float semitones, int)
    {
        const auto value = juce::roundToInt (semitones);
        return value == 0 ? juce::String ("U") : (value > 0 ? "+" : "") + juce::String (value);   // U: unison, as Waves Harmony shows it
    };
    const ToText asCents = [] (float cents, int) { return (cents > 0.0f ? "+" : "") + juce::String (juce::roundToInt (cents)) + " ct"; };
    const ToText asPan = [] (float pan, int)
    {
        const auto amount = juce::roundToInt (std::abs (pan) * 100.0f);
        return amount == 0 ? juce::String ("C") : juce::String (amount) + (pan < 0.0f ? " L" : " R");
    };
    const ToText asSemitoneShift = [] (float semitones, int) { return (semitones > 0.0f ? "+" : "") + juce::String (semitones, 1) + " st"; };
    layout.add (makeSwitch (ParamIDs::harmonyOn, "Harmony On", v29, false));
    layout.add (makeChoice (ParamIDs::harmonyKey, "Harmony Key", v29, Scales::keyNames, 0));
    layout.add (makeChoice (ParamIDs::harmonyScale, "Harmony Scale", v29, Scales::scaleNames, (int) Scales::Scale::major));
    layout.add (makeFloat (ParamIDs::harmonySource, "Harmony Source", { effects::Harmony::silentDb, 6.0f, 0.1f }, 0.0f, v29, asLevel));
    layout.add (makeFloat (ParamIDs::harmonyLevel, "Harmony Level", { effects::Harmony::silentDb, 6.0f, 0.1f }, 0.0f, v29, asLevel));
    layout.add (makeFloat (ParamIDs::harmonyCorrectFormant, "Harmony Correct Formant", { 0.0f, 1.0f, 0.01f }, 1.0f, v29, asPercent));
    layout.add (makeFloat (ParamIDs::harmonySpread, "Harmony Spread", { 0.0f, 1.0f, 0.01f }, 1.0f, v29, asPercent));
    for (int voice = 0; voice < effects::Harmony::numVoices; ++voice)
    {
        const auto id = [voice] (const char* part) { return ParamIDs::harmonyVoice (voice, part); };
        const auto name = "Harmony " + juce::String (voice + 1) + " ";
        const auto starts = voice < 2;
        layout.add (makeSwitch (id ("Active").toRawUTF8(), name + "Active", v29, starts));
        layout.add (makeSwitch (id ("On").toRawUTF8(), name + "On", v29, true));
        layout.add (makeFloat (id ("Pitch").toRawUTF8(), name + "Pitch", { -24.0f, 24.0f, 1.0f },
                               voice == 0 ? 4.0f : voice == 1 ? 7.0f : 0.0f, v29, asInterval));
        layout.add (makeFloat (id ("Fine").toRawUTF8(), name + "Fine", { -50.0f, 50.0f, 1.0f }, 0.0f, v29, asCents));
        layout.add (makeFloat (id ("Formant").toRawUTF8(), name + "Formant", { -12.0f, 12.0f, 0.1f }, 0.0f, v29, asSemitoneShift));
        layout.add (makeSwitch (id ("Correct").toRawUTF8(), name + "Correct", v29, true));
        layout.add (makeFloat (id ("Pan").toRawUTF8(), name + "Pan", { -1.0f, 1.0f, 0.01f },
                               voice == 0 ? -0.5f : voice == 1 ? 0.5f : 0.0f, v29, asPan));
        layout.add (makeFloat (id ("Level").toRawUTF8(), name + "Level", { effects::Harmony::silentDb, 6.0f, 0.1f },
                               starts ? -3.0f : 0.0f, v29, asLevel));
    }

    // Added in plugin version 30: the four-voice DOUBLER, inspired by Waves
    // Doubler. Voices 1 and 2 start as its user guide sets them (9.4 and
    // 23.7 ms, -6 dB, one each side) and detuned as its guide suggests (a
    // few cents down on one side, up on the other); 3 and 4 start off.
    constexpr int v30 = 30;
    const ToText asDoublerLevel = [] (float db, int)
    {
        return db <= effects::Doubler::minGainDb ? juce::String ("Off") : juce::String (db, 1) + " dB";
    };
    const ToText asCentsShift = [] (float cents, int) { return (cents > 0.0f ? "+" : "") + juce::String (juce::roundToInt (cents)); };
    const ToText asDelayMs = [] (float ms, int) { return juce::String (ms, 1); };
    const ToText asFrequency = [] (float hz, int) { return hz < 1000.0f ? juce::String (juce::roundToInt (hz)) : juce::String (hz / 1000.0f, 2) + "k"; };
    const ToText asRate = [] (float hz, int) { return juce::String (hz, hz < 10.0f ? 1 : 0); };
    const ToText asDoublerPan = [] (float pan, int)
    {
        const auto amount = juce::roundToInt (std::abs (pan) * 100.0f);
        return amount == 0 ? juce::String ("C") : juce::String (amount) + (pan < 0.0f ? " L" : " R");
    };
    layout.add (makeSwitch (ParamIDs::doublerDirectOn, "Doubler Direct On", v30, true));
    layout.add (makeFloat (ParamIDs::doublerDirectGain, "Doubler Direct Gain", { effects::Doubler::minGainDb, 0.0f, 0.1f }, 0.0f, v30, asDoublerLevel));
    layout.add (makeFloat (ParamIDs::doublerDirectPan, "Doubler Direct Pan", { -1.0f, 1.0f, 0.01f }, 0.0f, v30, asDoublerPan));
    layout.add (makeFloat (ParamIDs::doublerFbHighPass, "Doubler Feedback High-Pass", skewed (16.0f, 20000.0f, 400.0f, 1.0f), 16.0f, v30, asFrequency));
    layout.add (makeSwitch (ParamIDs::doublerLowOn, "Doubler Low Shelf On", v30, true));
    layout.add (makeFloat (ParamIDs::doublerLowGain, "Doubler Low Shelf", { -24.0f, 12.0f, 0.1f }, 0.0f, v30, asDb));
    layout.add (makeFloat (ParamIDs::doublerLowFreq, "Doubler Low Shelf Frequency", skewed (16.0f, 20000.0f, 400.0f, 1.0f), 101.0f, v30, asFrequency));
    layout.add (makeSwitch (ParamIDs::doublerHighOn, "Doubler High Shelf On", v30, true));
    layout.add (makeFloat (ParamIDs::doublerHighGain, "Doubler High Shelf", { -24.0f, 12.0f, 0.1f }, 0.0f, v30, asDb));
    layout.add (makeFloat (ParamIDs::doublerHighFreq, "Doubler High Shelf Frequency", skewed (16.0f, 20000.0f, 400.0f, 1.0f), 1306.0f, v30, asFrequency));
    layout.add (makeFloat (ParamIDs::doublerOutput, "Doubler Output", { -24.0f, 0.0f, 0.1f }, 0.0f, v30, asDb));
    // Since 0.9.0, voices start stronger than the guide's recipe, which is
    // made for tucking a double behind a lead in a mix: louder (-2 dB), more
    // out of tune (9 cents), further apart in time (18 and 31 ms, both past
    // the detuning's own 13 ms), and drifting, so a live voice sounds
    // doubled rather than just wider. 0.7.0's starting settings are kept in
    // keepDoublerAsSaved, for what was saved with them.
    struct VoiceDefaults { bool on; float gain, pan, delay, detune; };
    const VoiceDefaults defaults[] = { { true, -2.0f, -1.0f, 18.0f, -9.0f }, { true, -2.0f, 1.0f, 31.0f, 9.0f },
                                       { false, -6.0f, -0.6f, 24.0f, -14.0f }, { false, -6.0f, 0.6f, 40.0f, 14.0f } };
    constexpr int v32 = 32;   // DRIFT
    for (int voice = 0; voice < effects::Doubler::numVoices; ++voice)
    {
        const auto id = [voice] (const char* part) { return ParamIDs::doublerVoice (voice, part); };
        const auto name = "Doubler " + juce::String (voice + 1) + " ";
        const auto& start = defaults[voice];
        layout.add (makeSwitch (id ("On").toRawUTF8(), name + "On", v30, start.on));
        layout.add (makeFloat (id ("Gain").toRawUTF8(), name + "Gain", { effects::Doubler::minGainDb, 0.0f, 0.1f }, start.gain, v30, asDoublerLevel));
        layout.add (makeFloat (id ("Pan").toRawUTF8(), name + "Pan", { -1.0f, 1.0f, 0.01f }, start.pan, v30, asDoublerPan));
        layout.add (makeFloat (id ("Delay").toRawUTF8(), name + "Delay", { 0.0f, effects::Doubler::maxDelayMs, 0.1f }, start.delay, v30, asDelayMs));
        layout.add (makeFloat (id ("Feedback").toRawUTF8(), name + "Feedback", { 0.0f, 1.0f, 0.01f }, 0.0f, v30, asPercent));
        layout.add (makeSwitch (id ("Octave").toRawUTF8(), name + "Octave Down", v30, false));
        layout.add (makeFloat (id ("Detune").toRawUTF8(), name + "Detune", { -100.0f, 100.0f, 1.0f }, start.detune, v30, asCentsShift));
        layout.add (makeFloat (id ("Depth").toRawUTF8(), name + "Mod Depth", { -200.0f, 200.0f, 1.0f }, 0.0f, v30, asCentsShift));
        layout.add (makeFloat (id ("Rate").toRawUTF8(), name + "Mod Rate", skewed (0.1f, 400.0f, 2.0f, 0.01f), 1.0f, v30, asRate));
        layout.add (makeFloat (id ("Drift").toRawUTF8(), name + "Drift", { 0.0f, 1.0f, 0.01f }, 0.5f, v32, asPercent));
    }

    // Added in plugin version 31: Revival, the compressor's third TYPE,
    // inspired by Waves Renaissance Vox. GATE starts off; COMPRESSION at
    // -12 dB, a moderate squeeze; GAIN (the ceiling) at 0 dB, full scale.
    constexpr int v31 = 31;
    const ToText asRevivalGate = [] (float value, int)
    {
        return value <= effects::CompGate::revivalGateOffDb ? juce::String ("Off") : juce::String (value, 1) + " dB";
    };
    layout.add (makeFloat (ParamIDs::revivalGate, "Revival Gate", { effects::CompGate::revivalGateOffDb, 0.0f, 0.5f },
                           effects::CompGate::revivalGateOffDb, v31, asRevivalGate));
    layout.add (makeFloat (ParamIDs::revivalCompression, "Revival Compression", { effects::CompGate::revivalMinDb, 0.0f, 0.1f }, -12.0f, v31, asDb));
    layout.add (makeFloat (ParamIDs::revivalGain, "Revival Gain", { effects::CompGate::revivalMinDb, 0.0f, 0.1f }, 0.0f, v31, asDb));

    // Added in plugin version 33: the delay's controls inspired by Waves
    // H-Delay. Each starts where it does nothing (filters off, no MOD,
    // ANALOG off...), so saved settings sound as they did.
    constexpr int v33 = 33;
    const ToText asCut = [] (float hz, int)
    {
        if (hz <= effects::Delay::cutOffLowHz + 0.5f || hz >= effects::Delay::cutOffHighHz - 1.0f)
            return juce::String ("Off");
        return hz < 1000.0f ? juce::String (juce::roundToInt (hz)) + " Hz" : juce::String (hz / 1000.0f, 1) + " kHz";
    };
    const auto cutRange = skewed (effects::Delay::cutOffLowHz, effects::Delay::cutOffHighHz, 800.0f, 1.0f);
    layout.add (makeSwitch (ParamIDs::delayDual, "Delay Dual", v33, false));
    layout.add (makeFloat (ParamIDs::delayTimeRight, "Delay Time Right", skewed (1.0f, effects::Delay::maxDelaySeconds * 1000.0f, 400.0f, 1.0f),
                           280.0f, v33, asMs));
    layout.add (makeChoice (ParamIDs::delaySyncRight, "Delay Sync Right", v33, effects::Delay::syncNames, 7));   // 1/8 dotted: with 1/4 on the left, a classic pair
    layout.add (makeChoice (ParamIDs::delayPingStart, "Delay Ping-Pong Start", v33, effects::Delay::pingStartNames, 0));
    layout.add (makeSwitch (ParamIDs::delayPhaseLeft, "Delay Phase Left", v33, false));
    layout.add (makeSwitch (ParamIDs::delayPhaseRight, "Delay Phase Right", v33, false));
    layout.add (makeFloat (ParamIDs::delayLowCut, "Delay Low Cut", cutRange, effects::Delay::cutOffLowHz, v33, asCut));
    layout.add (makeFloat (ParamIDs::delayHighCut, "Delay High Cut", cutRange, effects::Delay::cutOffHighHz, v33, asCut));
    layout.add (makeFloat (ParamIDs::delayModDepth, "Delay Mod Depth", { 0.0f, 1.0f, 0.01f }, 0.0f, v33, asPercent));
    layout.add (makeFloat (ParamIDs::delayModRate, "Delay Mod Rate", skewed (0.05f, 20.0f, 1.0f, 0.01f), 0.5f, v33, asHz));
    layout.add (makeChoice (ParamIDs::delayAnalog, "Delay Analog", v33, effects::Delay::analogNames, 0));
    layout.add (makeSwitch (ParamIDs::delayLoFi, "Delay Lo-Fi", v33, false));
    return layout;
}

void keepDoublerAsSaved (const std::function<std::optional<float> (const juce::String&)>& get,
                         const std::function<void (const juce::String&, float)>& set)
{
    // 0.7.0's starting settings: the Waves Doubler guide's recipe.
    struct Was { float gain, pan, delay, detune; };
    const Was was[] = { { -6.0f, -1.0f, 9.4f, -4.0f }, { -6.0f, 1.0f, 23.7f, 4.0f },
                        { -12.0f, 1.0f, 16.0f, -7.0f }, { -12.0f, -1.0f, 21.0f, 7.0f } };
    for (int voice = 0; voice < effects::Doubler::numVoices; ++voice)
    {
        const auto id = [voice] (const char* part) { return ParamIDs::doublerVoice (voice, part); };
        const auto keep = [&] (const char* part, float value)
        {
            if (! get (id (part)).has_value())
                set (id (part), value);
        };
        keep ("Gain", was[voice].gain);
        keep ("Pan", was[voice].pan);
        keep ("Delay", was[voice].delay);
        keep ("Detune", was[voice].detune);
        set (id ("Drift"), 0.0f);
    }
}

void convertOldDoubler (const std::function<std::optional<float> (const juce::String&)>& get,
                        const std::function<void (const juce::String&, float)>& set)
{
    const auto rate = get (ParamIDs::doublerRate), depth = get (ParamIDs::doublerDepth), mix = get (ParamIDs::doublerMix);
    if (! rate.has_value() && ! depth.has_value() && ! mix.has_value())
        return;   // never set up: the new defaults it is

    // The old takes: 19 and 27 ms late, one each side, at MIX's level, their
    // delay wandering by (0.15 + 1.2 x DEPTH) ms (a sine at a quarter of RATE,
    // and a smaller one 2.71 times faster). A wandering delay bends the pitch
    // by its speed of change: that's the wobble's depth in cents.
    const auto driftHz = 0.25f * rate.value_or (0.8f);
    const auto wanderSeconds = (0.15f + 1.2f * depth.value_or (0.4f)) * 0.001f;
    const auto bend = juce::MathConstants<float>::twoPi * driftHz * wanderSeconds * (0.6f + 0.4f * 2.71f);
    const auto wobbleCents = 1200.0f * std::log2 (1.0f + bend);
    const auto gainDb = juce::jmax (effects::Doubler::minGainDb, juce::Decibels::gainToDecibels (mix.value_or (0.7f), effects::Doubler::minGainDb));

    const std::pair<float, float> takes[] = { { -1.0f, 19.0f }, { 1.0f, 27.0f } };   // pan, delay
    for (int voice = 0; voice < effects::Doubler::numVoices; ++voice)
    {
        const auto id = [voice] (const char* part) { return ParamIDs::doublerVoice (voice, part); };
        set (id ("On"), voice < 2 ? 1.0f : 0.0f);
        set (id ("Drift"), 0.0f);   // its wander is in DEPTH and RATE
        if (voice >= 2)
            continue;
        set (id ("Gain"), gainDb);
        set (id ("Pan"), takes[voice].first);
        set (id ("Delay"), takes[voice].second);
        set (id ("Detune"), 0.0f);
        set (id ("Depth"), voice == 0 ? wobbleCents : -wobbleCents);   // the two takes wandered independently
        set (id ("Rate"), driftHz);
    }
}

void moveOldDoublerSettings (const std::function<std::optional<float> (const char*)>& get,
                             const std::function<void (const char*, float)>& set)
{
    const auto type = get (ParamIDs::modType);
    if (! type.has_value() || juce::roundToInt (*type) != effects::Modulation::oldDoublerType)
        return;

    // MODULATION's knobs had these defaults when the Doubler was one of its types.
    set (ParamIDs::doublerOn, get (ParamIDs::modOn).value_or (0.0f));
    set (ParamIDs::doublerRate, get (ParamIDs::modRate).value_or (0.8f));
    set (ParamIDs::doublerDepth, get (ParamIDs::modDepth).value_or (0.45f));
    set (ParamIDs::doublerMix, get (ParamIDs::modMix).value_or (0.5f));
    set (ParamIDs::modOn, 0.0f);
    set (ParamIDs::modType, (float) effects::Modulation::Type::chorus);
}
