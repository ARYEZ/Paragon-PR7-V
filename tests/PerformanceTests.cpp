// Phase 6: the "performance" features: tap tempo, undo/redo, the tuner's
// mute and the expression pedal.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "dsp/TapTempo.h"
#include "dsp/effects/CompGate.h"
#include "dsp/effects/Delay.h"
#include "dsp/effects/FilterPitch.h"
#include "dsp/effects/Glitch.h"
#include "dsp/effects/LoFi.h"
#include "dsp/effects/Mars.h"
#include "dsp/effects/Modulation.h"
#include "dsp/effects/Reverb.h"
#include "dsp/effects/Vocoder.h"
#include "rigs/UndoHistory.h"

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    struct TestRig
    {
        TestRig()
        {
            folder.createDirectory();
            processor.useRigFolder (folder);
            processor.setPlayConfigDetails (1, 2, sampleRate, blockSize);
            processor.prepareToPlay (sampleRate, blockSize);
        }
        ~TestRig() { folder.deleteRecursively(); }

        juce::RangedAudioParameter& parameter (const char* id) { return *processor.getParameters().getParameter (id); }
        float value (const char* id) { return parameter (id).convertFrom0to1 (parameter (id).getValue()); }

        // A knob turned with the mouse: a gesture around the change, as the UI does.
        void turn (const char* id, float plainValue)
        {
            auto& p = parameter (id);
            p.beginChangeGesture();
            p.setValueNotifyingHost (p.convertTo0to1 (plainValue));
            p.endChangeGesture();
            processor.flushUndoStep();
        }

        // Plays a 220 Hz tone for `seconds`; returns the peak of the last block.
        float play (double seconds, juce::MidiBuffer midi = {})
        {
            juce::AudioBuffer<float> buffer (2, blockSize);
            float peak = 0.0f;
            for (int b = 0; b < (int) (seconds * sampleRate / blockSize); ++b)
            {
                buffer.clear();
                for (int i = 0; i < blockSize; ++i)
                    buffer.setSample (0, i, 0.25f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * (phase++) / sampleRate));
                processor.processBlock (buffer, midi);
                midi.clear();
                peak = buffer.getMagnitude (0, 0, blockSize);
            }
            return peak;
        }

        juce::File folder { juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("ParagonX1-perf-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64())) };
        ParagonX1AudioProcessor processor;
        long phase = 0;
    };
}

TEST_CASE ("Tap tempo averages the taps and starts again after a pause", "[tap]")
{
    TapTempo taps;
    CHECK_FALSE (taps.tap (0.0).has_value());   // one tap is not a tempo

    auto bpm = taps.tap (500.0);
    REQUIRE (bpm.has_value());
    CHECK_THAT (*bpm, WithinAbs (120.0, 1.0e-9));

    // One late tap moves the average only a little.
    taps.tap (1000.0);
    taps.tap (1500.0);
    bpm = taps.tap (2060.0);
    CHECK (*bpm > 113.0);
    CHECK (*bpm < 120.0);

    // A long pause: the next tap is the first of a new count.
    CHECK_FALSE (taps.tap (6000.0).has_value());
    CHECK_THAT (*taps.tap (6600.0), WithinAbs (100.0, 1.0e-9));

    // Absurdly fast tapping is limited to something a delay can use.
    TapTempo fast;
    fast.tap (0.0);
    CHECK_THAT (*fast.tap (10.0), WithinAbs (TapTempo::maxBpm, 1.0e-9));
}

TEST_CASE ("Undo history steps back and forward and forgets redo after a new edit", "[undo]")
{
    UndoHistory<int> history;
    history.record (1);
    CHECK_FALSE (history.canUndo());
    history.record (2);
    history.record (2);   // no change: no extra step
    history.record (3);

    CHECK (*history.undo() == 2);
    CHECK (*history.undo() == 1);
    CHECK_FALSE (history.undo().has_value());
    CHECK (*history.redo() == 2);

    history.record (9);   // a new edit after undoing
    CHECK_FALSE (history.canRedo());
    CHECK (*history.undo() == 2);

    for (int i = 0; i < 500; ++i)
        history.record (100 + i);
    int steps = 0;
    while (history.undo())
        ++steps;
    CHECK (steps == (int) UndoHistory<int>::maxSteps - 1);
}

TEST_CASE ("Undo and redo take back whole knob moves", "[undo]")
{
    TestRig rig;
    rig.turn (ParamIDs::lofiAmount, 8.0f);
    rig.turn (ParamIDs::delayMix, 0.6f);
    REQUIRE (rig.processor.canUndo());

    rig.processor.undo();
    CHECK_THAT (rig.value (ParamIDs::delayMix), WithinAbs (0.25, 1.0e-4));   // back to the default
    CHECK_THAT (rig.value (ParamIDs::lofiAmount), WithinAbs (8.0, 1.0e-4));   // untouched

    rig.processor.undo();
    CHECK_THAT (rig.value (ParamIDs::lofiAmount), WithinAbs (5.0, 1.0e-4));
    CHECK_FALSE (rig.processor.canUndo());

    rig.processor.redo();
    rig.processor.redo();
    CHECK_THAT (rig.value (ParamIDs::lofiAmount), WithinAbs (8.0, 1.0e-4));
    CHECK_THAT (rig.value (ParamIDs::delayMix), WithinAbs (0.6, 1.0e-4));
    CHECK_FALSE (rig.processor.canRedo());
}

TEST_CASE ("Undo doesn't take back the global knobs or the pedal position", "[undo]")
{
    TestRig rig;
    rig.turn (ParamIDs::lofiAmount, 7.0f);
    rig.turn (ParamIDs::outputGain, -6.0f);
    rig.turn (ParamIDs::expression, 0.3f);

    rig.processor.undo();
    CHECK_THAT (rig.value (ParamIDs::lofiAmount), WithinAbs (5.0, 1.0e-4));
    CHECK_THAT (rig.value (ParamIDs::outputGain), WithinAbs (-6.0, 1.0e-4));
    CHECK_THAT (rig.value (ParamIDs::expression), WithinAbs (0.3, 1.0e-3));
}

TEST_CASE ("Undoing a rig change brings back the previous rig and its name", "[undo][rigs]")
{
    TestRig rig;
    rig.turn (ParamIDs::reverbMix, 0.5f);

    const auto* surf = rig.processor.getRigLibrary().findFactory ("Tremolo Ghost");
    REQUIRE (surf != nullptr);
    rig.processor.loadRig (*surf);
    rig.processor.flushUndoStep();
    CHECK (rig.processor.getCurrentRig().name == "Tremolo Ghost");
    CHECK_FALSE (rig.processor.isRigModified());

    rig.processor.undo();
    CHECK (rig.processor.getCurrentRig().name == "Untitled");
    CHECK_THAT (rig.value (ParamIDs::reverbMix), WithinAbs (0.5, 1.0e-4));
    CHECK (rig.processor.isRigModified());   // it was edited before the rig change

    rig.processor.redo();
    CHECK (rig.processor.getCurrentRig().name == "Tremolo Ghost");
    CHECK_FALSE (rig.processor.isRigModified());
}

TEST_CASE ("Tapped tempo replaces the DAW's until cleared, and is saved", "[tap]")
{
    TestRig rig;
    CHECK_THAT (rig.processor.getTempoBpm(), WithinAbs (120.0, 1.0e-9));   // no DAW tempo: 120

    rig.processor.tapTempo();
    juce::Thread::sleep (400);
    rig.processor.tapTempo();
    const auto tapped = rig.processor.getTappedBpm();
    CHECK (tapped > 100.0);    // ~150, allowing for a slow test machine
    CHECK (tapped < 160.0);
    CHECK_THAT (rig.processor.getTempoBpm(), WithinAbs (tapped, 1.0e-9));

    juce::MemoryBlock state;
    rig.processor.getStateInformation (state);
    rig.processor.clearTappedTempo();
    CHECK_THAT (rig.processor.getTempoBpm(), WithinAbs (120.0, 1.0e-9));

    rig.processor.setStateInformation (state.getData(), (int) state.getSize());
    CHECK_THAT (rig.processor.getTappedBpm(), WithinAbs (tapped, 1.0e-9));
}

TEST_CASE ("The tuner mutes the output, unless told not to", "[tuner]")
{
    TestRig rig;
    const auto normal = rig.play (0.3);
    REQUIRE (normal > 0.05f);

    rig.turn (ParamIDs::tunerOn, 1.0f);
    CHECK (rig.play (0.1) == 0.0f);

    rig.turn (ParamIDs::tunerMute, 0.0f);
    CHECK_THAT (rig.play (0.1), WithinAbs (normal, 0.01));

    // And the tuner can hear the guitar while muted.
    rig.turn (ParamIDs::tunerMute, 1.0f);
    rig.play (0.3);
    std::vector<float> heard (8192);
    rig.processor.getInputTap().readLatest (heard.data(), (int) heard.size());
    CHECK (*std::max_element (heard.begin(), heard.end()) > 0.2f);
}

TEST_CASE ("Expression pedal as a volume pedal, within its MIN and MAX", "[expression]")
{
    TestRig rig;
    const auto full = rig.play (0.3);

    rig.turn (ParamIDs::expressionTarget, (float) ExpressionTargets::volume);
    CHECK_THAT (rig.play (0.2), WithinAbs (full * 0.25, 0.01));   // half way by default: -12 dB (the audio taper below)

    rig.turn (ParamIDs::expression, 0.0f);
    CHECK (rig.play (0.2) < 1.0e-4f);                       // heel down: silent, as a real volume pedal

    rig.turn (ParamIDs::expression, 1.0f);
    CHECK_THAT (rig.play (0.2), WithinAbs (full, 0.005));   // toe down: full volume

    rig.turn (ParamIDs::expression, 0.5f);
    CHECK_THAT (rig.play (0.2), WithinAbs (full * 0.25, 0.01));   // audio taper: half way is -12 dB

    rig.turn (ParamIDs::expressionMin, 0.5f);
    rig.turn (ParamIDs::expression, 0.0f);
    CHECK_THAT (rig.play (0.2), WithinAbs (full * 0.25, 0.01));   // heel now stops at half volume
}

TEST_CASE ("MIDI CC 11 moves the expression pedal", "[expression]")
{
    TestRig rig;
    const auto full = rig.play (0.3);   // before assigning VOLUME: the pedal starts half way, which would be quieter
    rig.turn (ParamIDs::expressionTarget, (float) ExpressionTargets::volume);

    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 11, 0), 0);
    CHECK (rig.play (0.2, midi) < 1.0e-4f);
    CHECK_THAT (rig.processor.getExpressionPosition(), WithinAbs (0.0, 1.0e-6));

    // Other controllers are ignored.
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 7, 127), 0);
    CHECK (rig.play (0.1, midi) < 1.0e-4f);

    midi.addEvent (juce::MidiMessage::controllerEvent (1, 11, 127), 0);
    CHECK_THAT (rig.play (0.2, midi), WithinAbs (full, 0.005));
}

TEST_CASE ("Expression pedal as WAH and MORPH FX", "[expression]")
{
    TestRig rig;
    const auto clean = rig.play (0.3);

    // A 220 Hz tone is below the wah's sweep: loud at the heel, cut at the toe.
    rig.turn (ParamIDs::expressionTarget, (float) ExpressionTargets::wah);
    rig.turn (ParamIDs::expression, 0.0f);
    const auto heel = rig.play (0.3);
    rig.turn (ParamIDs::expression, 1.0f);
    const auto toe = rig.play (0.3);
    CHECK (heel > 2.0f * toe);

    // MORPH at the heel: the time effects are dry, so the output is the clean tone.
    rig.turn (ParamIDs::expressionTarget, (float) ExpressionTargets::morph);
    rig.turn (ParamIDs::reverbOn, 1.0f);
    rig.turn (ParamIDs::reverbType, 4.0f);   // plate: dense from the first moment, so its effect shows within the half second
    rig.turn (ParamIDs::reverbMix, 1.0f);
    rig.turn (ParamIDs::expression, 0.0f);
    CHECK_THAT (rig.play (0.5), WithinAbs (clean, 0.005));
    rig.turn (ParamIDs::expression, 1.0f);
    CHECK (std::abs (rig.play (0.5) - clean) > 0.02f);
}

TEST_CASE ("Expression pedal as a WHAMMY bends the pitch shifter", "[expression][pitch]")
{
    // The pitch shifter set an octave up, all wet; the pedal sweeps from
    // no shift at the heel to the full octave at the toe.
    ParagonX1AudioProcessor processor;
    processor.setPlayConfigDetails (1, 1, 48000.0, 256);
    processor.prepareToPlay (48000.0, 256);
    const auto set = [&] (const char* id, float plainValue)
    {
        auto* parameter = processor.getParameters().getParameter (id);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
    };
    set (ParamIDs::fpOn, 1.0f);
    set (ParamIDs::fpType, (float) effects::FilterPitch::Type::pitchShifter);
    set (ParamIDs::fpInterval, 12.0f);
    set (ParamIDs::fpMix, 1.0f);
    set (ParamIDs::expressionTarget, (float) ExpressionTargets::pitch);

    // Plays a 300 Hz tone for half a second; returns its level at `hz` over the last 0.3 s.
    const auto levelAt = [&] (double hz)
    {
        std::vector<float> output;
        juce::AudioBuffer<float> buffer (1, 256);
        juce::MidiBuffer midi;
        for (int start = 0; start < 24000; start += 256)
        {
            for (int i = 0; i < 256; ++i)
                buffer.setSample (0, i, 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 300.0 * (start + i) / 48000.0));
            processor.processBlock (buffer, midi);
            output.insert (output.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + 256);
        }
        const auto coefficient = 2.0 * std::cos (juce::MathConstants<double>::twoPi * hz / 48000.0);
        double s1 = 0.0, s2 = 0.0;
        for (size_t i = 9600; i < output.size(); ++i)
        {
            const auto s0 = output[i] + coefficient * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - coefficient * s1 * s2));
    };

    set (ParamIDs::expression, 0.0f);
    CHECK (levelAt (300.0) > 10.0 * levelAt (600.0));   // heel: the note as played
    set (ParamIDs::expression, 1.0f);
    CHECK (levelAt (600.0) > 10.0 * levelAt (300.0));   // toe: an octave up
    set (ParamIDs::expression, 7.0f / 12.0f);
    CHECK (levelAt (300.0 * std::exp2 (7.0 / 12.0)) > 10.0 * levelAt (300.0));   // part way: a fifth up
}

TEST_CASE ("Expression pedal as LO-FI sweeps from the full band into the phone", "[expression][lofi]")
{
    TestRig rig;
    const auto clean = rig.play (0.3);

    // A 220 Hz tone is below the telephone's band: it passes at the heel
    // (no lo-fi) and is mostly cut at the toe (all lo-fi).
    rig.turn (ParamIDs::lofiOn, 1.0f);
    rig.turn (ParamIDs::lofiType, (float) effects::LoFi::Type::telephone);
    rig.turn (ParamIDs::lofiNoise, 0.0f);
    rig.turn (ParamIDs::expressionTarget, (float) ExpressionTargets::loFi);
    rig.turn (ParamIDs::expression, 0.0f);
    CHECK_THAT (rig.play (0.5), WithinAbs (clean, 0.01));
    rig.turn (ParamIDs::expression, 1.0f);
    CHECK (rig.play (0.5) < 0.3f * clean);
}

TEST_CASE ("SETTINGS: the pedal's MIDI CC and the tuner's reference are saved with the project", "[expression][settings]")
{
    TestRig rig;
    rig.turn (ParamIDs::expressionTarget, (float) ExpressionTargets::volume);
    rig.turn (ParamIDs::expression, 1.0f);   // toe down, so there's sound until CC 4 moves it
    rig.processor.setExpressionCc (4);
    rig.processor.setTunerReferenceHz (432.0);

    // CC 4 now moves the pedal; CC 11 no longer does.
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 11, 0), 0);
    CHECK (rig.play (0.2, midi) > 0.05f);
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 4, 0), 0);
    CHECK (rig.play (0.2, midi) < 1.0e-4f);

    juce::MemoryBlock state;
    rig.processor.getStateInformation (state);
    ParagonX1AudioProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (restored.getExpressionCc() == 4);
    CHECK_THAT (restored.getTunerReferenceHz(), WithinAbs (432.0, 1.0e-9));

    // Out-of-range values are kept sensible.
    restored.setExpressionCc (500);
    CHECK (restored.getExpressionCc() == 127);
}

// Hidden (run with "[cpu]"): how much of one core the plugin takes, at its
// defaults and with every switch off.
TEST_CASE ("CPU with everything off", "[.][cpu]")
{
    TestRig rig;
    const auto measure = [&rig] (const char* label)
    {
        rig.play (1.0);   // warm up
        const auto startTime = juce::Time::getHighResolutionTicks();
        const auto seconds = 10.0;
        rig.play (seconds);
        const auto took = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - startTime);
        std::printf ("%-40s %6.2f%% of a core\n", label, 100.0 * took / seconds);
    };
    measure ("defaults");
    for (auto* parameter : static_cast<juce::AudioProcessor&> (rig.processor).getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            if (ranged->getParameterID().endsWith ("On") && ranged->getParameterID() != "bypass")
            {
                if (ranged->getValue() > 0.5f)
                    std::printf ("  on by default: %s\n", ranged->getParameterID().toRawUTF8());
                ranged->setValueNotifyingHost (0.0f);
            }
    measure ("every ...On switch off");
}

namespace
{
    // A sung phrase for the benchmarks and renders: a vowel (harmonics up to
    // 2.6 kHz, a formant near 700 Hz) with vibrato, moving between notes
    // every 0.3 s with short gaps between syllables, so pitch trackers,
    // TUNE VOICE, HARMONY, the VOCODER and the gates all have work to do.
    float sungSample (long n)
    {
        const auto t = (double) n / sampleRate;
        static constexpr double notes[] = { 220.0, 246.9, 261.6, 293.7, 329.6, 293.7, 261.6, 196.0 };
        const auto syllable = (int) (t / 0.3);
        const auto within = t - syllable * 0.3;
        if (within > 0.27)
            return 0.0f;
        const auto envelope = std::sin (juce::MathConstants<double>::pi * within / 0.27);
        const auto f0 = notes[syllable % 8] * (1.0 + 0.01 * std::sin (juce::MathConstants<double>::twoPi * 5.5 * t));
        // Phase from the note's own start, so a note change doesn't click.
        double v = 0.0;
        for (int harmonic = 1; harmonic <= 12; ++harmonic)
        {
            const auto shape = 1.0 / (1.0 + std::pow ((harmonic * notes[syllable % 8] - 700.0) / 400.0, 2.0)) + 0.1;
            v += shape * std::sin (juce::MathConstants<double>::twoPi * f0 * harmonic * within);
        }
        return (float) (0.06 * envelope * v);
    }

    // Plays the sung phrase for `seconds` from sample `start`.
    void sing (ParagonX1AudioProcessor& processor, long& position, double seconds)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        for (int b = 0; b < (int) (seconds * sampleRate / blockSize); ++b)
        {
            buffer.clear();
            for (int i = 0; i < blockSize; ++i)
                buffer.setSample (0, i, sungSample (position++));
            processor.processBlock (buffer, midi);
        }
    }

    // PARAGON_CPU_ONLY=text: measure only what's labelled with it (for
    // counting instructions under valgrind, where every run takes long).
    bool wanted (const juce::String& label)
    {
        const auto only = juce::SystemStats::getEnvironmentVariable ("PARAGON_CPU_ONLY", {});
        return only.isEmpty() || label.contains (only);
    }
}

// Hidden (run with "[rigcpu]"): the sound's share of one core for every
// factory rig, singing, dearest first, so an optimisation round can see where
// the time goes (0.56.1, from PR7-G 0.82.1). PARAGON_RIGCPU_SECONDS sets how
// long each is timed (default 2); PARAGON_CPU_ONLY picks rigs by name.
TEST_CASE ("CPU of every factory rig", "[.][rigcpu]")
{
    TestRig rig;
    const auto env = juce::SystemStats::getEnvironmentVariable ("PARAGON_RIGCPU_SECONDS", "2").getDoubleValue();
    const auto seconds = env > 0.0 ? env : 2.0;
    std::vector<std::pair<double, juce::String>> costs;
    long position = 0;
    for (const auto& entry : rig.processor.getRigLibrary().getEntries())
    {
        const auto label = entry.rig.getSlotName() + " " + entry.rig.name;
        if (! entry.isFactory || ! wanted (label))
            continue;
        rig.processor.loadRig (entry);
        sing (rig.processor, position, 0.5);   // warm up (and let the last rig's tails go)
        const auto startTime = juce::Time::getHighResolutionTicks();
        sing (rig.processor, position, seconds);
        const auto took = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - startTime);
        costs.emplace_back (100.0 * took / seconds, label);
    }
    std::sort (costs.begin(), costs.end(), [] (const auto& a, const auto& b) { return a.first > b.first; });
    double total = 0.0;
    for (const auto& [cost, name] : costs)
    {
        total += cost;
        std::printf ("%6.2f%%  %s\n", cost, name.toRawUTF8());
    }
    std::printf ("%zu rigs, mean %.2f%% of a core\n", costs.size(), total / (double) juce::jmax ((size_t) 1, costs.size()));
}

// Hidden (run with "[blockcpu]"): each block on its own, from everything off,
// in its usual settings (every TYPE / MODE / STYLE where it has them), singing:
// its share of one core. PARAGON_CPU_ONLY picks one by label.
TEST_CASE ("CPU of each block on its own", "[.][blockcpu]")
{
    TestRig rig;
    const auto set = [&rig] (const juce::String& id, float plainValue)
    {
        auto& p = *rig.processor.getParameters().getParameter (id);
        p.setValueNotifyingHost (p.convertTo0to1 (plainValue));
    };
    const auto allOff = [&rig, &set]
    {
        for (auto* parameter : static_cast<juce::AudioProcessor&> (rig.processor).getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
                ranged->setValueNotifyingHost (ranged->getDefaultValue());
        for (auto* parameter : static_cast<juce::AudioProcessor&> (rig.processor).getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
                if (ranged->getParameterID().endsWith ("On") && ranged->getParameterID() != "bypass"
                    && ! ranged->getParameterID().startsWith ("hv") && ! ranged->getParameterID().startsWith ("mb")
                    && ! ranged->getParameterID().startsWith ("dv"))
                    ranged->setValueNotifyingHost (0.0f);
        juce::ignoreUnused (set);
    };
    long position = 0;
    const auto measure = [&] (const juce::String& label, std::function<void()> setUp)
    {
        if (! wanted (label))
            return;
        allOff();
        setUp();
        sing (rig.processor, position, 0.5);
        const auto startTime = juce::Time::getHighResolutionTicks();
        constexpr double seconds = 3.0;
        sing (rig.processor, position, seconds);
        const auto took = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - startTime);
        std::printf ("%-36s %6.2f%% of a core\n", label.toRawUTF8(), 100.0 * took / seconds);
    };
    measure ("everything off", [] {});
    for (int type = 0; type < effects::CompGate::typeNames.size(); ++type)
        measure ("dynamics " + effects::CompGate::typeNames[type], [&, type] { set (ParamIDs::compOn, 1); set (ParamIDs::compType, (float) type); });
    measure ("de-esser", [&] { set (ParamIDs::deessOn, 1); });
    for (int type = 0; type < effects::FilterPitch::typeNames.size(); ++type)
        measure ("filter/pitch " + effects::FilterPitch::typeNames[type], [&, type] { set (ParamIDs::fpOn, 1); set (ParamIDs::fpType, (float) type); });
    measure ("octaver", [&] { set (ParamIDs::octOn, 1); });
    for (int quality = 0; quality < effects::Mars::qualityNames.size(); ++quality)
        for (const int bands : { 1, 4 })
            measure ("mars " + juce::String (bands) + " band(s) " + effects::Mars::qualityNames[quality],
                     [&, quality, bands] { set (ParamIDs::driveOn, 1); set (ParamIDs::marsBands, (float) (bands - 1)); set (ParamIDs::marsQuality, (float) quality); });
    measure ("graphic eq", [&] { set (ParamIDs::eqOn, 1); });
    for (int mode = 0; mode < effects::Glitch::modeNames.size(); ++mode)
        measure ("glitch " + effects::Glitch::modeNames[mode], [&, mode] { set (ParamIDs::glitchOn, 1); set (ParamIDs::glitchMode, (float) mode); });
    measure ("harmony (default voices)", [&] { set (ParamIDs::harmonyOn, 1); });
    measure ("harmony + generate notes", [&] { set (ParamIDs::harmonyOn, 1); set (ParamIDs::harmonyGenerate, 1); });
    measure ("tune voice natural", [&] { set (ParamIDs::harmonyCorrect, 1); });
    measure ("tune voice hard", [&] { set (ParamIDs::harmonyCorrect, 2); });
    measure ("formant +5", [&] { set (ParamIDs::formantOn, 1); set (ParamIDs::formantShift, 5); });
    for (int mode = 0; mode < effects::Vocoder::modeNames.size(); ++mode)
        measure ("vocoder " + effects::Vocoder::modeNames[mode], [&, mode] { set (ParamIDs::vocoderOn, 1); set (ParamIDs::vocoderMode, (float) mode); });
    measure ("doubler (2 takes)", [&] { set (ParamIDs::doublerOn, 1); });
    measure ("doubler (4 takes)", [&] { set (ParamIDs::doublerOn, 1); set ("dv3On", 1); set ("dv4On", 1); });
    for (int type = 0; type < effects::Modulation::typeNames.size(); ++type)
        measure ("mod " + effects::Modulation::typeNames[type], [&, type] { set (ParamIDs::modOn, 1); set (ParamIDs::modType, (float) type); });
    for (int type = 0; type < effects::Delay::typeNames.size(); ++type)
        measure ("delay " + effects::Delay::typeNames[type], [&, type] { set (ParamIDs::delayOn, 1); set (ParamIDs::delayType, (float) type); });
    for (int type = 0; type < effects::Reverb::typeNames.size(); ++type)
        measure ("reverb " + effects::Reverb::typeNames[type], [&, type] { set (ParamIDs::reverbOn, 1); set (ParamIDs::reverbType, (float) type); });
    for (int type = 0; type < effects::LoFi::typeNames.size(); ++type)
        measure ("lofi " + effects::LoFi::typeNames[type], [&, type] { set (ParamIDs::lofiOn, 1); set (ParamIDs::lofiType, (float) type); });
}

// Hidden (run with "[rigrender]"): writes every factory rig's output (the
// sung phrase, 2.5 s, both channels as interleaved 32-bit floats) to the
// folder in PARAGON_RIGRENDER, one file per rig, so two builds can be compared
// sample by sample (an optimisation must not change the sound).
TEST_CASE ("Render every factory rig to files", "[.][rigrender]")
{
    const auto folder = juce::File (juce::SystemStats::getEnvironmentVariable ("PARAGON_RIGRENDER", {}));
    REQUIRE (folder.isDirectory());
    TestRig rig;
    for (const auto& entry : rig.processor.getRigLibrary().getEntries())
    {
        if (! entry.isFactory || ! wanted (entry.rig.getSlotName() + " " + entry.rig.name))
            continue;
        rig.processor.loadRig (entry);
        rig.processor.reset();
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        juce::MemoryOutputStream out;
        long position = 0;
        for (int b = 0; b < (int) (2.5 * sampleRate / blockSize); ++b)
        {
            buffer.clear();
            for (int i = 0; i < blockSize; ++i)
                buffer.setSample (0, i, sungSample (position++));
            rig.processor.processBlock (buffer, midi);
            for (int i = 0; i < blockSize; ++i)
            {
                const float frame[2] = { buffer.getSample (0, i), buffer.getSample (1, i) };
                out.write (frame, sizeof (frame));
            }
        }
        folder.getChildFile (entry.rig.getSlotName() + ".f32").replaceWithData (out.getData(), out.getDataSize());
    }
}
