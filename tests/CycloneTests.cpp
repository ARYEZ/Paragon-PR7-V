#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "dsp/effects/Cyclone.h"
#include "rigs/Rig.h"
#include "ui/EffectPanel.h"
#include "ui/Footswitch.h"
#include "ui/SignalChain.h"

// CYCLONE, the rotary speaker (inspired by the Neo Ventilator II): what it
// does to the sound, measured against the numbers in the pedal's manual
// where it gives them.

using Catch::Matchers::WithinAbs;
using effects::Cyclone;

namespace
{
    constexpr double rate = 48000.0;
    constexpr int blockSize = 480;   // 10 ms
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    // A Cyclone on the bench, with a signal generator.
    struct Bench
    {
        Cyclone cyclone;
        Cyclone::Settings settings;
        juce::AudioBuffer<float> buffer { 2, blockSize };
        double time = 0.0;
        Bench() { cyclone.prepare (rate, blockSize); }

        // Plays `seconds` of a sine through, calling `each` with every block's output.
        template <typename Each>
        void play (float seconds, float hz, float amplitude, Each&& each)
        {
            const auto blocks = (int) std::lround (seconds * rate / blockSize);
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < blockSize; ++i)
                {
                    const auto x = amplitude * std::sin ((float) std::fmod (twoPi * hz * (time + i / rate), (double) twoPi));
                    buffer.setSample (0, i, x);
                    buffer.setSample (1, i, x);
                }
                time += blockSize / rate;
                cyclone.process (juce::dsp::AudioBlock<float> (buffer), settings);
                each (buffer);
            }
        }
        void play (float seconds, float hz = 1000.0f, float amplitude = 0.25f)
        {
            play (seconds, hz, amplitude, [] (const auto&) {});
        }

        // Each block's RMS on one side, for `seconds`.
        // (The first 50 ms are left out: a new tone starts with a click.)
        std::vector<float> envelope (float seconds, float hz, int channel = 0, float amplitude = 0.25f)
        {
            play (0.05f, hz, amplitude);
            std::vector<float> levels;
            play (seconds, hz, amplitude, [&] (const juce::AudioBuffer<float>& b)
            {
                // Two 5 ms halves per block: fine enough for a 7 Hz swing.
                for (int half = 0; half < 2; ++half)
                    levels.push_back (b.getRMSLevel (channel, half * blockSize / 2, blockSize / 2));
            });
            return levels;
        }
    };

    float swingDb (const std::vector<float>& levels)
    {
        const auto [low, high] = std::minmax_element (levels.begin(), levels.end());
        return 20.0f * std::log10 (*high / juce::jmax (1.0e-9f, *low));
    }

    float meanRms (const std::vector<float>& levels)
    {
        double sum = 0.0;
        for (auto l : levels)
            sum += (double) l * l;
        return (float) std::sqrt (sum / (double) levels.size());
    }

    float toDb (float gain) { return 20.0f * std::log10 (gain); }

    // How many times a level swings up through its average: the AM's rate.
    int risingCrossings (const std::vector<float>& levels)
    {
        const auto mean = std::accumulate (levels.begin(), levels.end(), 0.0f) / (float) levels.size();
        int count = 0;
        for (size_t i = 1; i < levels.size(); ++i)
            count += levels[i - 1] < mean && levels[i] >= mean ? 1 : 0;
        return count;
    }

    // One frequency's amplitude in a buffer side (Goertzel).
    float amplitudeAt (const juce::AudioBuffer<float>& b, int channel, float hz)
    {
        const auto w = twoPi * hz / (float) rate;
        const auto coefficient = 2.0f * std::cos (w);
        float s1 = 0.0f, s2 = 0.0f;
        const auto* x = b.getReadPointer (channel);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const auto s0 = x[i] + coefficient * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const auto power = s1 * s1 + s2 * s2 - coefficient * s1 * s2;
        return 2.0f * std::sqrt (juce::jmax (0.0f, power)) / (float) b.getNumSamples();
    }
}

TEST_CASE ("Cyclone: the rotors run at the manual's speeds, the drum a little slower than the horn", "[cyclone]")
{
    Bench rig;
    rig.play (0.1f);   // starts at speed
    CHECK_THAT (rig.cyclone.getHornHz(), WithinAbs (0.8, 0.005));    // an average 122, slow
    CHECK_THAT (rig.cyclone.getDrumHz(), WithinAbs (0.76, 0.01));

    rig.settings.fast = true;
    rig.play (20.0f);
    CHECK_THAT (rig.cyclone.getHornHz(), WithinAbs (6.8, 0.01));     // and fast
    CHECK_THAT (rig.cyclone.getDrumHz(), WithinAbs (6.5, 0.05));

    // The knobs' ranges are the manual's.
    rig.settings.fastHz = 100.0f;
    rig.play (20.0f);
    CHECK_THAT (rig.cyclone.getHornHz(), WithinAbs (Cyclone::fastMaxHz, 0.01));
}

TEST_CASE ("Cyclone: ACCELERATION follows the manual's table, and the drum takes far longer than the horn", "[cyclone]")
{
    // The knob's ends and its 1 o'clock (6), where the manual gives an average 122's times.
    const auto fastest = Cyclone::rampSeconds (0.0f), slowest = Cyclone::rampSeconds (10.0f), typical = Cyclone::rampSeconds (6.0f);
    CHECK_THAT (fastest.hornUp, WithinAbs (0.5, 1e-4));
    CHECK_THAT (fastest.drumDown, WithinAbs (0.8, 1e-4));
    CHECK_THAT (slowest.drumUp, WithinAbs (8.0, 1e-4));
    CHECK_THAT (slowest.hornDown, WithinAbs (8.0, 1e-4));
    CHECK_THAT (typical.drumUp, WithinAbs (5.5, 0.1));
    CHECK_THAT (typical.drumDown, WithinAbs (5.5, 0.1));
    CHECK_THAT (typical.hornUp, WithinAbs (1.0, 0.02));
    CHECK_THAT (typical.hornDown, WithinAbs (1.6, 0.02));

    // Heard: from slow to fast, the horn gets 95% of the way in about 1 s, the drum in about 5.5 s.
    Bench rig;
    rig.play (0.1f);
    rig.settings.fast = true;
    const auto timeTo95 = [&] (auto speed, float target)
    {
        const auto start = speed();
        for (int block = 1; block < 1500; ++block)
        {
            rig.play (0.01f);
            if (std::abs (speed() - start) >= 0.95f * std::abs (target - start))
                return (float) block * 0.01f;
        }
        return 99.0f;
    };
    CHECK_THAT (timeTo95 ([&] { return rig.cyclone.getHornHz(); }, 6.8f), WithinAbs (1.0, 0.05));
    Bench drumRig;
    drumRig.play (0.1f);
    drumRig.settings.fast = true;
    const auto drumStart = drumRig.cyclone.getDrumHz();
    float drumTime = 99.0f;
    for (int block = 1; block < 1500; ++block)
    {
        drumRig.play (0.01f);
        if (drumRig.cyclone.getDrumHz() - drumStart >= 0.95f * (6.8f * Cyclone::drumRatio - drumStart))
        {
            drumTime = (float) block * 0.01f;
            break;
        }
    }
    CHECK_THAT (drumTime, WithinAbs (5.5, 0.15));
}

TEST_CASE ("Cyclone: the horn's swirl is heard at its speed, with Doppler on top", "[cyclone]")
{
    Bench rig;
    rig.settings.fast = true;
    rig.play (0.1f);

    // A treble note (all horn) rises and falls once per turn on each mic.
    const auto levels = rig.envelope (4.0f, 3000.0f);
    CHECK (risingCrossings (levels) >= 26);   // 6.8 Hz x 4 s
    CHECK (risingCrossings (levels) <= 29);
    CHECK (swingDb (levels) > 6.0f);

    // Its pitch swings too: the horn's mouth moving at 2 pi r f, against the speed of sound.
    juce::AudioBuffer<float> recording (1, (int) rate);
    int written = 0;
    rig.play (1.0f, 4000.0f, 0.25f, [&] (const juce::AudioBuffer<float>& b)
    {
        recording.copyFrom (0, written, b, 0, 0, blockSize);
        written += blockSize;
    });
    std::vector<float> periods;
    float last = -1.0f;
    const auto* x = recording.getReadPointer (0);
    for (int i = (int) (0.05 * rate); i < recording.getNumSamples(); ++i)   // after the new tone's click
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            const auto at = (float) (i - 1) + x[i - 1] / (x[i - 1] - x[i]);
            if (last >= 0.0f)
                periods.push_back (at - last);
            last = at;
        }
    // Ten-cycle averages, so the horn's tone filter doesn't add jitter.
    std::vector<float> averaged;
    for (size_t i = 0; i + 10 < periods.size(); i += 5)
        averaged.push_back (std::accumulate (periods.begin() + (long) i, periods.begin() + (long) i + 10, 0.0f) / 10.0f);
    const auto [shortest, longest] = std::minmax_element (averaged.begin(), averaged.end());
    const auto swingPercent = 100.0f * (*longest / *shortest - 1.0f) / 2.0f;
    const auto expected = 100.0f * twoPi * Cyclone::hornRadiusMetres * 6.8f / 343.0f;   // about 2.2%
    CAPTURE (swingPercent, expected);
    CHECK (swingPercent > expected * 0.6f);
    CHECK (swingPercent < expected * 1.2f);
}

TEST_CASE ("Cyclone: MIX / DIST mixes each rotor in, then moves its mics back", "[cyclone]")
{
    // At 0 a rotor's band passes untouched: no swirl, at its own level.
    Bench rig;
    rig.settings.mixDistHigh = 0.0f;
    rig.settings.mixDistLow = 0.0f;
    rig.settings.mode = Cyclone::Mode::guitar1;   // neutral
    rig.play (0.5f);
    for (const auto hz : { 100.0f, 3000.0f })
    {
        const auto levels = rig.envelope (2.0f, hz);
        CAPTURE (hz);
        CHECK (swingDb (levels) < 0.3f);
        CHECK_THAT (toDb (meanRms (levels) / (0.25f / std::sqrt (2.0f))), WithinAbs (0.0, 0.5));
    }

    // Close mics (5) swirl more than far ones (10).
    const auto swingAt = [] (float knob, float hz, bool low)
    {
        Bench r;
        (low ? r.settings.mixDistLow : r.settings.mixDistHigh) = knob;
        r.settings.fast = true;
        r.play (0.5f, hz);
        return swingDb (r.envelope (2.0f, hz));
    };
    CHECK (swingAt (5.0f, 3000.0f, false) > swingAt (10.0f, 3000.0f, false) + 6.0f);
    CHECK (swingAt (5.0f, 100.0f, true) > swingAt (10.0f, 100.0f, true) + 2.0f);
    // The drum swirls less than the horn (its sound spreads more).
    CHECK (swingAt (5.0f, 100.0f, true) < swingAt (5.0f, 3000.0f, false));
}

TEST_CASE ("Cyclone: STOP brakes both rotors to a standstill, facing forward", "[cyclone]")
{
    Bench rig;
    rig.settings.fast = true;
    rig.play (0.5f);
    rig.settings.stop = true;
    rig.play (12.0f);
    CHECK (rig.cyclone.getHornHz() == 0.0f);
    CHECK (rig.cyclone.getDrumHz() == 0.0f);
    CHECK (rig.cyclone.getHornTurn() == 0.0f);
    CHECK (rig.cyclone.getDrumTurn() == 0.0f);

    // Still, and the horn facing between the mics: the same on both sides.
    const auto left = rig.envelope (1.0f, 3000.0f, 0), right = rig.envelope (1.0f, 3000.0f, 1);
    CHECK (swingDb (left) < 0.2f);
    CHECK_THAT (toDb (meanRms (left) / meanRms (right)), WithinAbs (0.0, 0.2));

    // Released, it spins up again.
    rig.settings.stop = false;
    rig.play (3.0f);
    CHECK (rig.cyclone.getHornHz() > 6.0f);
}

TEST_CASE ("Cyclone: BALANCE turns down the drum one way and the horn the other", "[cyclone]")
{
    const auto levelAt = [] (float balance, float hz)
    {
        Bench r;
        r.settings.balance = balance;
        r.settings.stop = true;   // still, so the levels are steady
        r.play (0.5f, hz);
        return toDb (meanRms (r.envelope (1.0f, hz)));
    };
    CHECK_THAT (levelAt (10.0f, 100.0f) - levelAt (5.0f, 100.0f), WithinAbs (-15.0, 1.0));
    CHECK_THAT (levelAt (10.0f, 4000.0f) - levelAt (5.0f, 4000.0f), WithinAbs (0.0, 0.5));
    CHECK_THAT (levelAt (0.0f, 4000.0f) - levelAt (5.0f, 4000.0f), WithinAbs (-15.0, 1.0));
    CHECK_THAT (levelAt (0.0f, 100.0f) - levelAt (5.0f, 100.0f), WithinAbs (0.0, 0.5));
}

TEST_CASE ("Cyclone: DRIVE distorts like a valve amp, at about the same level", "[cyclone]")
{
    // The rotors out of the way (MIX / DIST at 0), so only the drive is heard.
    const auto measure = [] (float drive)
    {
        Bench r;
        r.settings.drive = drive;
        r.settings.mixDistLow = r.settings.mixDistHigh = 0.0f;
        r.play (0.5f, 200.0f);
        juce::AudioBuffer<float> last (2, blockSize);
        r.play (0.1f, 200.0f, 0.25f, [&] (const auto& b) { last.makeCopyOf (b); });
        return std::array<float, 3> { amplitudeAt (last, 0, 200.0f), amplitudeAt (last, 0, 400.0f), amplitudeAt (last, 0, 600.0f) };   // whole cycles in a block
    };
    const auto clean = measure (0.0f), driven = measure (10.0f);
    CAPTURE (clean[0], clean[1], clean[2], driven[0], driven[1], driven[2]);
    CHECK (clean[1] < clean[0] * 0.001f);   // clean is clean
    // A push-pull power amp (as in the 122) clips mostly in odd harmonics,
    // with a little even from the valves' unevenness.
    CHECK (driven[2] > driven[0] * 0.05f);
    CHECK (driven[1] > driven[0] * 0.01f);
    CHECK_THAT (toDb (driven[0] / clean[0]), WithinAbs (0.0, 3.0));   // level-compensated
}

TEST_CASE ("Cyclone: MODE: GIT 1 is neutral, GIT 2 lifts the mids and highs, KEY is the cabinet's speakers", "[cyclone]")
{
    const auto levelAt = [] (Cyclone::Mode mode, float hz)
    {
        Bench r;
        r.settings.mode = mode;
        r.settings.stop = true;
        r.play (0.5f, hz);
        return toDb (meanRms (r.envelope (0.5f, hz)));
    };
    using Mode = Cyclone::Mode;
    CHECK (levelAt (Mode::guitar2, 2000.0f) - levelAt (Mode::guitar1, 2000.0f) > 2.0f);
    CHECK_THAT (levelAt (Mode::guitar2, 200.0f) - levelAt (Mode::guitar1, 200.0f), WithinAbs (0.0, 0.5));
    // KEY: the horn's top falls away, and the woofer has little at the very bottom.
    CHECK (levelAt (Mode::keys, 12000.0f) - levelAt (Mode::guitar1, 12000.0f) < -8.0f);
    CHECK (levelAt (Mode::keys, 30.0f) - levelAt (Mode::guitar1, 30.0f) < -4.0f);
}

TEST_CASE ("Cyclone: LEVEL at 5 is about the bypassed level; MORPH at 0 is the dry sound", "[cyclone]")
{
    for (const auto mode : { Cyclone::Mode::guitar1, Cyclone::Mode::guitar2 })
        for (const auto hz : { 110.0f, 440.0f, 2000.0f })
        {
            Bench r;
            r.settings.fast = true;
            r.settings.mode = mode;
            r.play (1.0f, hz);
            CAPTURE ((int) mode, hz);
            // GIT 2 is brighter on purpose, so a little louder in the mids and highs.
            const auto slack = mode == Cyclone::Mode::guitar1 ? 1.5f : 3.5f;
            CHECK_THAT (toDb (meanRms (r.envelope (2.0f, hz)) / (0.25f / std::sqrt (2.0f))), WithinAbs (0.0, slack));
        }
    // Each step of LEVEL is 2.4 dB: 10 is 12 dB up on 5.
    const auto stoppedAt = [] (float level)
    {
        Bench r;
        r.settings.level = level;
        r.settings.stop = true;
        r.play (0.5f, 440.0f);
        return toDb (meanRms (r.envelope (0.5f, 440.0f)));
    };
    CHECK_THAT (stoppedAt (10.0f) - stoppedAt (5.0f), WithinAbs (12.0, 0.2));

    Bench dry;
    dry.settings.morph = 0.0f;
    dry.settings.fast = true;
    dry.play (0.5f);
    float worst = 0.0f;
    dry.play (0.2f, 1000.0f, 0.25f, [&] (const juce::AudioBuffer<float>& b)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const auto x = 0.25f * std::sin ((float) std::fmod (twoPi * 1000.0 * (dry.time - blockSize / rate + i / rate), (double) twoPi));
            worst = juce::jmax (worst, std::abs (b.getSample (0, i) - x));
        }
    });
    CHECK (worst < 1.0e-4f);
}

TEST_CASE ("Cyclone: the expression pedal sets the speed from SLOW SPEED at the heel to FAST SPEED at the toe", "[cyclone]")
{
    for (const auto [pedal, hz] : { std::pair { 0.0f, 0.8f }, std::pair { 1.0f, 6.8f }, std::pair { 0.5f, std::sqrt (0.8f * 6.8f) } })
    {
        Bench r;
        r.settings.pedal = pedal;
        r.settings.fast = pedal < 0.5f;   // SLOW / FAST and STOP don't count while the pedal sets the speed
        r.settings.stop = true;
        r.play (0.1f);
        r.play (20.0f);
        CAPTURE (pedal);
        CHECK_THAT (r.cyclone.getHornHz(), WithinAbs (hz, 0.02));
    }
}


//==============================================================================
// Around the sound: saved work, the pedal, the footswitch and the panel.

namespace
{
    void setPlain (ParagonX1AudioProcessor& processor, const char* id, float value)
    {
        auto* parameter = processor.getParameters().getParameter (id);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }
    float plain (ParagonX1AudioProcessor& processor, const char* id)
    {
        return processor.getParameters().getRawParameterValue (id)->load();
    }
    template <typename Type>
    void findAllOf (juce::Component& parent, std::vector<Type*>& found)
    {
        for (auto* child : parent.getChildren())
        {
            if (auto* match = dynamic_cast<Type*> (child))
                found.push_back (match);
            findAllOf (*child, found);
        }
    }
    constexpr auto rotaryType = (float) effects::Modulation::Type::rotary;
}

TEST_CASE ("Cyclone: rigs saved with the original rotary come in as Cyclone, set to match", "[cyclone]")
{
    const auto rigWith = [] (std::initializer_list<std::pair<const char*, float>> values, int parameterVersion = 36)
    {
        juce::XmlElement xml ("ParagonRig");
        xml.setAttribute ("version", 1);
        xml.setAttribute ("parameterVersion", parameterVersion);
        for (const auto& [id, value] : values)
        {
            auto* parameter = xml.createNewChildElement ("Param");
            parameter->setAttribute ("id", id);
            parameter->setAttribute ("value", value);
        }
        return *Rig::fromXml (xml);
    };

    // A fast one, part dry: FAST at its speed, the MIX half of MIX / DIST.
    auto fast = rigWith ({ { ParamIDs::modType, rotaryType }, { ParamIDs::modRate, 6.5f }, { ParamIDs::modDepth, 0.7f }, { ParamIDs::modMix, 0.85f } });
    CHECK (fast.parameters[ParamIDs::modCycFast] == 1.0f);
    CHECK_THAT (fast.parameters[ParamIDs::modCycFastSpeed], WithinAbs (6.5, 1e-4));
    CHECK_THAT (fast.parameters[ParamIDs::modCycHigh], WithinAbs (4.25, 1e-4));
    CHECK_THAT (fast.parameters[ParamIDs::modCycLow], WithinAbs (4.25, 1e-4));
    CHECK (fast.parameters[ParamIDs::modCycMode] == (float) Cyclone::Mode::guitar1);

    // A slow one, all wet: SLOW at its speed, DEPTH as how close the mics are.
    auto slow = rigWith ({ { ParamIDs::modType, rotaryType }, { ParamIDs::modRate, 0.7f }, { ParamIDs::modDepth, 0.6f }, { ParamIDs::modMix, 1.0f } });
    CHECK (slow.parameters[ParamIDs::modCycFast] == 0.0f);
    CHECK_THAT (slow.parameters[ParamIDs::modCycSlowSpeed], WithinAbs (0.7, 1e-4));
    CHECK_THAT (slow.parameters[ParamIDs::modCycHigh], WithinAbs (7.0, 1e-4));

    // Other types, and rigs saved since (with Cyclone), are left alone.
    auto chorus = rigWith ({ { ParamIDs::modType, 0.0f }, { ParamIDs::modRate, 6.5f } });
    CHECK (chorus.parameters.count (ParamIDs::modCycFast) == 0);
    auto since = rigWith ({ { ParamIDs::modType, rotaryType }, { ParamIDs::modRate, 6.5f } }, 37);
    CHECK (since.parameters.count (ParamIDs::modCycFast) == 0);
}

TEST_CASE ("Cyclone: projects saved with the original rotary come in as Cyclone, set to match", "[cyclone]")
{
    juce::MemoryBlock saved;
    {
        ParagonX1AudioProcessor processor;
        setPlain (processor, ParamIDs::modType, rotaryType);
        setPlain (processor, ParamIDs::modRate, 6.5f);
        setPlain (processor, ParamIDs::modMix, 0.85f);
        processor.getStateInformation (saved);
    }
    // As a project from before Cyclone: none of its controls in it.
    auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
    REQUIRE (xml != nullptr);
    for (auto* child = xml->getFirstChildElement(); child != nullptr;)
    {
        auto* next = child->getNextElement();
        if (child->getStringAttribute ("id").startsWith ("modCyc"))
            xml->removeChildElement (child, true);
        child = next;
    }
    xml->setAttribute ("parameterVersion", 36);   // saved before Cyclone
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*xml, old);

    ParagonX1AudioProcessor reopened;
    reopened.setStateInformation (old.getData(), (int) old.getSize());
    CHECK (plain (reopened, ParamIDs::modCycFast) == 1.0f);
    CHECK_THAT (plain (reopened, ParamIDs::modCycFastSpeed), WithinAbs (6.5, 0.01));
    CHECK_THAT (plain (reopened, ParamIDs::modCycHigh), WithinAbs (4.25, 0.06));   // (the knob moves in steps of 0.1)
    CHECK (plain (reopened, ParamIDs::modCycMode) == (float) Cyclone::Mode::guitar1);
}

TEST_CASE ("Cyclone: the expression pedal on CYCLONE SPEED spins the plugin's rotors", "[cyclone]")
{
    ParagonX1AudioProcessor processor;
    processor.prepareToPlay (48000.0, 480);
    setPlain (processor, ParamIDs::modOn, 1.0f);
    setPlain (processor, ParamIDs::modType, rotaryType);
    setPlain (processor, ParamIDs::expressionTarget, (float) ExpressionTargets::cycloneSpeed);
    juce::AudioBuffer<float> block (2, 480);
    juce::MidiBuffer midi;
    const auto run = [&] (float seconds)
    {
        for (int i = 0; i < juce::roundToInt (seconds * 100.0f); ++i)
        {
            block.clear();
            processor.processBlock (block, midi);
        }
    };
    setPlain (processor, ParamIDs::expression, 1.0f);   // toe down
    run (12.0f);
    CHECK_THAT (processor.getCyclone().getHornHz(), WithinAbs (6.8, 0.05));
    setPlain (processor, ParamIDs::expression, 0.0f);   // heel
    run (12.0f);
    CHECK_THAT (processor.getCyclone().getHornHz(), WithinAbs (0.8, 0.02));
}

TEST_CASE ("Cyclone: holding the MODULATION stomp switches SLOW / FAST; the TONE page is Cyclone's alone", "[cyclone][ui]")
{
    ParagonX1AudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    std::vector<Footswitch*> footswitches;
    findAllOf (*editor, footswitches);
    REQUIRE (footswitches.size() == 7);
    auto& modSwitch = *footswitches[4];   // stomp 5, MODULATION
    REQUIRE (modSwitch.onHold != nullptr);

    // Not a Cyclone: the hold does nothing (and a tap still switches the block).
    setPlain (processor, ParamIDs::modType, 0.0f);
    CHECK_FALSE (modSwitch.onHold());

    setPlain (processor, ParamIDs::modType, rotaryType);
    CHECK (modSwitch.onHold());
    CHECK (plain (processor, ParamIDs::modCycFast) == 1.0f);
    CHECK (modSwitch.onHold());
    CHECK (plain (processor, ParamIDs::modCycFast) == 0.0f);

    // The MOD panel's page keys: ROTORS and TONE for Cyclone, none for the others.
    std::vector<EffectPanel*> panels;
    findAllOf (*editor, panels);
    std::vector<juce::TextButton*> buttons;
    findAllOf (*editor, buttons);
    const auto visiblePageKeys = [&]
    {
        int count = 0;
        for (auto* panel : panels)
            for (auto* button : buttons)
                if (button->getParentComponent() == panel && button->isVisible()
                    && (button->getButtonText() == "ROTORS" || button->getButtonText() == "TONE"))
                    ++count;
        return count;
    };
    editor->setSize (1600, 900);
    std::vector<SignalChain*> chains;
    findAllOf (*editor, chains);
    REQUIRE (chains.size() == 1);
    const auto openMod = [&]   // MOD, from the signal chain (after another block, so it opens afresh)
    {
        chains[0]->onSelect (10);
        chains[0]->onSelect (9);
    };
    openMod();
    CHECK (visiblePageKeys() == 2);
    setPlain (processor, ParamIDs::modType, 0.0f);
    openMod();
    CHECK (visiblePageKeys() == 0);
}
