#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <set>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "dsp/effects/MarsModulation.h"

using Catch::Matchers::WithinAbs;
using Mod = effects::MarsModulation;

namespace
{
    constexpr double rate = 48000.0;
    constexpr double tickSeconds = 16.0 / rate;

    // A modulation engine with one slot from `source` to band 1's DRIVE, so it's active.
    struct Engine
    {
        Mod modulation;
        Mod::Settings settings;
        Mod::Context context;
        Mod::MidiState midi;

        Engine()
        {
            modulation.prepare (rate);
            context.midi = &midi;
        }

        // Runs for `seconds`, feeding `level` (a steady amplitude) to the
        // ears, and returns `source`'s value at every tick.
        std::vector<float> run (int source, double seconds, double level = 0.0)
        {
            std::vector<float> values;
            const auto ticks = (int) std::round (seconds / tickSeconds);
            for (int t = 0; t < ticks; ++t)
            {
                for (int i = 0; i < 16; ++i)
                {
                    const auto x = level * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * (t * 16 + i) / rate);
                    modulation.listen (x, { x, 0.0, 0.0, 0.0 });
                }
                modulation.tick (settings, context, tickSeconds);
                midi.blockDone();
                values.push_back (modulation.sourceValue (source));
            }
            return values;
        }
    };

    int crossingsUp (const std::vector<float>& values)
    {
        int count = 0;
        for (size_t i = 1; i < values.size(); ++i)
            count += values[i - 1] < 0.0f && values[i] >= 0.0f;
        return count;
    }

    float at (const std::vector<float>& values, double seconds)
    {
        return values[(size_t) juce::jlimit (0, (int) values.size() - 1, (int) std::round (seconds / tickSeconds) - 1)];
    }
}

TEST_CASE ("MARS XLFO: starts as a sine at RATE; steps, glide, curves and BALANCE shape it", "[mars][modulation]")
{
    SECTION ("The starting XLFO is a smooth wave from -1 to 1, RATE times a second")
    {
        Engine engine;
        engine.settings.lfos[0].rateHz = 3.0f;
        const auto values = engine.run (Mod::lfo1, 2.0);
        CHECK (crossingsUp (values) == 6);
        CHECK_THAT (*std::max_element (values.begin(), values.end()), WithinAbs (1.0, 0.01));
        CHECK_THAT (*std::min_element (values.begin(), values.end()), WithinAbs (-1.0, 0.01));
        // Smooth: no step bigger than a 3 Hz sine's steepest moves in a tick.
        auto biggest = 0.0f;
        for (size_t i = 1; i < values.size(); ++i)
            biggest = std::max (biggest, std::abs (values[i] - values[i - 1]));
        CHECK (biggest < 0.0065f);
    }

    SECTION ("Steps with no glide hold their values; full glide slides between them")
    {
        Mod::Lfo lfo;
        lfo.numSteps = 4;
        const std::array<float, 4> stepValues { 1.0f, 0.5f, -0.5f, -1.0f };
        std::array<float, Mod::maxSteps> values {};
        for (size_t i = 0; i < 4; ++i)
        {
            lfo.steps[i] = { stepValues[i], 0.0f, Mod::Curve::linear, false };
            values[i] = stepValues[i];
        }
        for (size_t i = 0; i < 4; ++i)
            for (const auto within : { 0.1, 0.5, 0.9 })
                CHECK_THAT (Mod::lfoShape (lfo, (i + within) / 4.0, values), WithinAbs (stepValues[i], 1.0e-6));

        // GLIDE 1 (the global knob overrides the steps' 0): halfway through a step, halfway to the next.
        lfo.glide = 1.0f;
        CHECK_THAT (Mod::lfoShape (lfo, 0.5 / 4.0, values), WithinAbs (0.75, 1.0e-5));
        // Glide 0.5: the first half holds, the second half slides.
        lfo.glide = 0.5f;
        CHECK_THAT (Mod::lfoShape (lfo, 0.4 / 4.0, values), WithinAbs (1.0, 1.0e-5));
        CHECK_THAT (Mod::lfoShape (lfo, 0.75 / 4.0, values), WithinAbs (0.75, 1.0e-5));
        // Curves: Sqr starts slow, Sqrt starts fast.
        lfo.glide = 1.0f;
        lfo.steps[0].curve = Mod::Curve::sqr;
        CHECK_THAT (Mod::lfoShape (lfo, 0.25 / 4.0, values), WithinAbs (1.0 - 0.5 * 0.0625, 1.0e-5));
        lfo.steps[0].curve = Mod::Curve::sqrt;
        CHECK_THAT (Mod::lfoShape (lfo, 0.25 / 4.0, values), WithinAbs (1.0 - 0.5 * 0.5, 1.0e-5));
    }

    SECTION ("BALANCE left: the first half of the wave goes by quickly")
    {
        // How far into the cycle the starting sine first reaches its bottom.
        const auto bottomAt = [] (float balance)
        {
            Engine engine;
            engine.settings.lfos[0].rateHz = 1.0f;
            engine.settings.lfos[0].balance = balance;
            const auto values = engine.run (Mod::lfo1, 1.0);
            for (size_t i = 0; i < values.size(); ++i)
                if (values[i] < -0.999f)
                    return (double) (i + 1) * tickSeconds;
            return 1.0;
        };
        CHECK_THAT (bottomAt (0.0f), WithinAbs (0.5, 0.01));
        CHECK_THAT (bottomAt (-1.0f), WithinAbs (0.05, 0.01));
        CHECK_THAT (bottomAt (1.0f), WithinAbs (0.95, 0.02));
    }

    SECTION ("PHASE moves where the cycle starts")
    {
        Engine engine;
        engine.settings.lfos[0].rateHz = 0.02f;   // nearly still
        engine.settings.lfos[0].phase = 0.5f;
        CHECK_THAT (engine.run (Mod::lfo1, 0.01).back(), WithinAbs (-1.0, 0.01));
    }

    SECTION ("A random step changes each time round; the others don't")
    {
        Engine engine;
        auto& lfo = engine.settings.lfos[0];
        lfo.numSteps = 2;
        lfo.rateHz = 10.0f;
        lfo.steps[0] = { 0.3f, 0.0f, Mod::Curve::linear, false };
        lfo.steps[1] = { 0.0f, 0.0f, Mod::Curve::linear, true };
        const auto values = engine.run (Mod::lfo1, 1.0);
        std::set<int> randomValues;
        for (int cycle = 0; cycle < 9; ++cycle)
        {
            CHECK_THAT (at (values, (cycle + 0.25) / 10.0), WithinAbs (0.3, 1.0e-6));
            randomValues.insert (juce::roundToInt (at (values, (cycle + 0.75) / 10.0) * 1000.0f));
        }
        CHECK (randomValues.size() >= 8);
    }
}

TEST_CASE ("MARS XLFO: tempo sync, song lock and MIDI restart", "[mars][modulation]")
{
    SECTION ("Synced to 1/4 at 120 BPM: two cycles a second; OFFSET x2 doubles it")
    {
        Engine engine;
        engine.settings.lfos[0].sync = Mod::syncNames.indexOf ("1/4");
        engine.context.bpm = 120.0;
        CHECK (crossingsUp (engine.run (Mod::lfo1, 2.0)) == 4);
        Engine faster;
        faster.settings.lfos[0].sync = Mod::syncNames.indexOf ("1/4");
        faster.settings.lfos[0].offset = 2.0f;
        faster.context.bpm = 120.0;
        CHECK (crossingsUp (faster.run (Mod::lfo1, 2.0)) == 8);
    }

    SECTION ("While the song plays, a synced XLFO is locked to the bar")
    {
        Engine engine;
        engine.settings.lfos[0].sync = Mod::syncNames.indexOf ("1 bar");
        engine.context.songBeats = 2.0;   // halfway through a bar
        engine.run (Mod::lfo1, tickSeconds);
        CHECK_THAT (engine.modulation.lfoPosition (0), WithinAbs (0.5, 1.0e-6));
    }

    SECTION ("RETRIGGER: a MIDI note starts the cycle again; LEGATO only the first of held notes")
    {
        for (const auto mode : { Mod::MidiRestart::retrigger, Mod::MidiRestart::legato })
        {
            Engine engine;
            engine.settings.lfos[0].rateHz = 1.0f;
            engine.settings.lfos[0].midi = mode;
            engine.run (Mod::lfo1, 0.3);
            juce::MidiBuffer notes;
            notes.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
            engine.midi.read (notes);
            engine.run (Mod::lfo1, tickSeconds);
            CHECK (engine.modulation.lfoPosition (0) < 0.001f);
            engine.run (Mod::lfo1, 0.3);
            juce::MidiBuffer second;
            second.addEvent (juce::MidiMessage::noteOn (1, 64, 0.8f), 0);   // the first still held
            engine.midi.read (second);
            engine.run (Mod::lfo1, tickSeconds);
            if (mode == Mod::MidiRestart::retrigger)
                CHECK (engine.modulation.lfoPosition (0) < 0.001f);
            else
                CHECK_THAT (engine.modulation.lfoPosition (0), WithinAbs (0.3, 0.002));
        }
    }
}

TEST_CASE ("MARS EG: the voice over THRESHOLD plays delay, attack, decay, sustain, hold, release", "[mars][modulation]")
{
    Engine engine;
    auto& eg = engine.settings.egs[0];
    eg.thresholdDb = -30.0f;
    eg.delayMs = 50.0f;
    eg.attackMs = 100.0f;
    eg.decayMs = 100.0f;
    eg.sustain = 0.4f;
    eg.holdMs = 100.0f;
    eg.releaseMs = 200.0f;

    // Silence: nothing. Then a note at -12 dB.
    CHECK (engine.run (Mod::eg1, 0.2).back() == 0.0f);
    const auto sung = engine.run (Mod::eg1, 0.6, 0.25);
    CHECK_THAT (at (sung, 0.04), WithinAbs (0.0, 1.0e-6));            // the DELAY
    CHECK_THAT (at (sung, 0.10), WithinAbs (0.5, 0.02));              // halfway up the ATTACK (straight)
    CHECK_THAT (at (sung, 0.15), WithinAbs (1.0, 0.02));              // the top
    CHECK_THAT (at (sung, 0.20), WithinAbs (0.7, 0.02));              // halfway down the DECAY
    CHECK_THAT (at (sung, 0.50), WithinAbs (0.4, 1.0e-6));            // SUSTAIN while the note lasts

    // The note stops: HOLD, then RELEASE (the peak follower takes a moment to hear it stop).
    const auto after = engine.run (Mod::eg1, 0.5);
    CHECK_THAT (at (after, 0.12), WithinAbs (0.4, 1.0e-6));
    CHECK (at (after, 0.30) < 0.3f);
    CHECK (at (after, 0.30) > 0.05f);
    CHECK (after.back() == 0.0f);

    SECTION ("RANGE centred: 0 while it sustains, below 0 at rest")
    {
        Engine centred;
        centred.settings.egs[0] = eg;
        centred.settings.egs[0].centred = true;
        CHECK_THAT (centred.run (Mod::eg1, 0.1).back(), WithinAbs (-0.4, 1.0e-6));
        CHECK_THAT (centred.run (Mod::eg1, 0.6, 0.25).back(), WithinAbs (0.0, 1.0e-6));
    }

    SECTION ("A quiet voice under THRESHOLD doesn't set it off")
    {
        Engine quiet;
        quiet.settings.egs[0] = eg;
        CHECK (quiet.run (Mod::eg1, 0.5, 0.01).back() == 0.0f);   // -40 dB
    }

    SECTION ("Slopes bow the curves: positive starts slowly")
    {
        CHECK (Mod::egCurve (0.5, 1.0f) < 0.2);
        CHECK (Mod::egCurve (0.5, -1.0f) > 0.8);
        CHECK_THAT (Mod::egCurve (0.5, 0.0f), WithinAbs (0.5, 1.0e-9));
    }

    SECTION ("MIDI NOTES: held notes hold it; the last one let go releases it")
    {
        Engine midi;
        midi.settings.egs[0] = eg;
        midi.settings.egs[0].trigger = Mod::Trigger::midi;
        juce::MidiBuffer on;
        on.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
        midi.midi.read (on);
        CHECK_THAT (midi.run (Mod::eg1, 0.5).back(), WithinAbs (0.4, 1.0e-6));
        juce::MidiBuffer off;
        off.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
        midi.midi.read (off);
        CHECK (midi.run (Mod::eg1, 0.5).back() == 0.0f);
    }
}

TEST_CASE ("MARS EF: follows how loud the voice is; TRANSIENT only its attacks", "[mars][modulation]")
{
    SECTION ("ENVELOPE: -48 dB to 0 dB as 0 to 1, louder higher; GAIN shifts it")
    {
        const auto settled = [] (double amplitude, float gainDb)
        {
            Engine engine;
            engine.settings.efs[0].gainDb = gainDb;
            return engine.run (Mod::ef1, 1.0, amplitude).back();
        };
        CHECK_THAT (settled (1.0, 0.0f), WithinAbs (1.0, 0.01));
        CHECK_THAT (settled (0.0631, 0.0f), WithinAbs (0.5, 0.015));   // -24 dB
        CHECK_THAT (settled (0.0631, 12.0f), WithinAbs (0.75, 0.015));
        CHECK (settled (0.0, 0.0f) == 0.0f);
    }

    SECTION ("ATTACK and RELEASE set how quickly it follows")
    {
        Engine engine;
        engine.settings.efs[0].attackMs = 100.0f;
        engine.settings.efs[0].releaseMs = 500.0f;
        const auto rising = engine.run (Mod::ef1, 0.1, 0.5);
        // After one ATTACK time: 63% of the way to -6 dB's reading.
        CHECK_THAT (rising.back(), WithinAbs ((1.0 - 6.02 / 48.0) * (1.0 - std::exp (-1.0)), 0.03));
        engine.run (Mod::ef1, 1.0, 0.5);
        const auto falling = engine.run (Mod::ef1, 0.5);
        CHECK (falling.back() > 0.3f);   // still letting go
    }

    SECTION ("TRANSIENT: high as a note starts, back near 0 while it holds")
    {
        Engine engine;
        engine.settings.efs[0].transient = true;
        engine.settings.efs[0].attackMs = 1.0f;
        engine.settings.efs[0].releaseMs = 50.0f;
        engine.run (Mod::ef1, 0.3);
        const auto note = engine.run (Mod::ef1, 1.0, 0.3);
        CHECK (*std::max_element (note.begin(), note.begin() + 200) > 0.7f);
        CHECK (note.back() < 0.1f);
    }
}

TEST_CASE ("MARS MIDI, XY and pedal sources", "[mars][modulation]")
{
    SECTION ("MIDI: the chosen controller through the RESPONSE curve")
    {
        Engine engine;
        engine.settings.midis[0] = { Mod::MidiInput::controller, 74, Mod::Response::linear };
        engine.settings.midis[1] = { Mod::MidiInput::controller, 74, Mod::Response::square };
        juce::MidiBuffer cc;
        cc.addEvent (juce::MidiMessage::controllerEvent (1, 74, 64), 0);
        cc.addEvent (juce::MidiMessage::pitchWheel (1, 0), 0);
        engine.midi.read (cc);
        engine.run (Mod::midi1, tickSeconds);
        CHECK_THAT (engine.modulation.sourceValue (Mod::midi1), WithinAbs (64.0 / 127.0, 1.0e-5));
        CHECK_THAT (engine.modulation.sourceValue (Mod::midi2), WithinAbs (std::pow (64.0 / 127.0, 2.0), 1.0e-5));
        engine.settings.midis[0].input = Mod::MidiInput::pitchBend;
        engine.run (Mod::midi1, tickSeconds);
        CHECK_THAT (engine.modulation.sourceValue (Mod::midi1), WithinAbs (-1.0, 1.0e-3));
        engine.settings.midis[0].input = Mod::MidiInput::velocity;
        juce::MidiBuffer note;
        note.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        engine.midi.read (note);
        engine.run (Mod::midi1, tickSeconds);
        CHECK_THAT (engine.modulation.sourceValue (Mod::midi1), WithinAbs (100.0 / 127.0, 1.0e-3));
    }

    SECTION ("Every RESPONSE runs 0 to 1, and they bend differently in between")
    {
        std::set<int> middles;
        for (int r = 0; r < Mod::responseNames.size(); ++r)
        {
            CHECK_THAT (Mod::respond ((Mod::Response) r, 0.0f), WithinAbs (0.0, 1.0e-5));
            CHECK_THAT (Mod::respond ((Mod::Response) r, 1.0f), WithinAbs (1.0, 1.0e-5));
            middles.insert (juce::roundToInt (Mod::respond ((Mod::Response) r, 0.3f) * 1000.0f));
        }
        CHECK (middles.size() == (size_t) Mod::responseNames.size());
    }

    SECTION ("XY: bipolar -1 to 1 or unipolar 0 to 1; a SLIDER has only Y")
    {
        Engine engine;
        engine.settings.xys[0] = { 0.25f, 1.0f, false, false };
        engine.settings.xys[1] = { 0.25f, 0.75f, true, true };
        engine.context.pedal = 0.6f;
        engine.run (Mod::xy1x, tickSeconds);
        CHECK_THAT (engine.modulation.sourceValue (Mod::xy1x), WithinAbs (-0.5, 1.0e-6));
        CHECK_THAT (engine.modulation.sourceValue (Mod::xy1y), WithinAbs (1.0, 1.0e-6));
        CHECK (engine.modulation.sourceValue (Mod::xy2x) == 0.0f);
        CHECK_THAT (engine.modulation.sourceValue (Mod::xy2y), WithinAbs (0.75, 1.0e-6));
        CHECK_THAT (engine.modulation.sourceValue (Mod::pedal), WithinAbs (0.6, 1.0e-6));
    }
}

TEST_CASE ("MARS slots: AMOUNT scales the source; a slot can turn another slot up or down", "[mars][modulation]")
{
    Engine engine;
    engine.settings.xys[0] = { 1.0f, 0.0f, false, true };   // unipolar: X at 1, Y at 0
    auto& slots = engine.settings.slots;
    slots[0] = { Mod::xy1x, Mod::bandTarget (1, Mod::drive), 0.5f };
    slots[1] = { Mod::xy1x, Mod::globalTarget (Mod::output), -0.25f };
    slots[2] = { Mod::xy1x, Mod::globalTarget (Mod::output), -0.25f };   // slots on one target add up
    CHECK (Mod::isActive (engine.settings));
    engine.run (Mod::xy1x, tickSeconds);
    CHECK_THAT (engine.modulation.offset (Mod::bandTarget (1, Mod::drive)), WithinAbs (0.5, 1.0e-6));
    CHECK_THAT (engine.modulation.offset (Mod::globalTarget (Mod::output)), WithinAbs (-0.5, 1.0e-6));
    CHECK (engine.modulation.offset (Mod::bandTarget (0, Mod::drive)) == 0.0f);

    // A slot from XY's Y to slot 1's AMOUNT: Y at 0.5 adds half its own amount to slot 1's.
    slots[3] = { Mod::xy1y, Mod::slotTarget (0), 1.0f };
    engine.settings.xys[0].y = 0.5f;
    engine.run (Mod::xy1x, tickSeconds);
    CHECK_THAT (engine.modulation.offset (Mod::bandTarget (1, Mod::drive)), WithinAbs (1.0, 1.0e-6));
    engine.settings.slots[3].amount = -1.0f;
    engine.run (Mod::xy1x, tickSeconds);
    CHECK_THAT (engine.modulation.offset (Mod::bandTarget (1, Mod::drive)), WithinAbs (0.0, 1.0e-6));

    // A slot with no source, no target or no amount does nothing.
    Mod::Settings empty;
    empty.slots[0] = { Mod::lfo1, 0, 1.0f };
    empty.slots[1] = { Mod::none, Mod::bandTarget (0, Mod::drive), 1.0f };
    empty.slots[2] = { Mod::lfo1, Mod::bandTarget (0, Mod::drive), 0.0f };
    CHECK_FALSE (Mod::isActive (empty));
    empty.slots[2].amount = 1.0e-8f;   // 0% stored a hair off
    CHECK_FALSE (Mod::isActive (empty));
    empty.slots[2].amount = 0.01f;     // 1%: on
    CHECK (Mod::isActive (empty));
}

TEST_CASE ("MARS modulation: every target's range is its knob's, and every part has a parameter", "[mars][modulation][parameters]")
{
    ParagonX1AudioProcessor processor;
    auto& parameters = processor.getParameters();
    for (int target = 1; target < Mod::numTargets; ++target)
    {
        const auto id = ParamIDs::marsTargetParameter (target);
        CAPTURE (target, Mod::targetName (target), id);
        auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (parameters.getParameter (id));
        REQUIRE (parameter != nullptr);
        const auto& knob = parameter->getNormalisableRange();
        const auto range = Mod::targetRange (target);
        CHECK (knob.start == range.start);
        CHECK (knob.end == range.end);
        CHECK_THAT (knob.skew, WithinAbs (range.skew, 1.0e-6));
        for (const auto position : { 0.1f, 0.5f, 0.9f })
            CHECK_THAT (knob.convertFrom0to1 (position), WithinAbs (range.convertFrom0to1 (position), 1.0e-3 * (range.end - range.start)));
    }
    CHECK (Mod::targetNames().size() == Mod::numTargets);
    CHECK (Mod::targetName (Mod::bandTarget (2, Mod::presence)) == "Band 3 Presence");
    CHECK (Mod::targetName (Mod::slotTarget (15)) == "Slot 16 Amount");
}
