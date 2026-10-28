#include <catch2/catch_test_macros.hpp>
#include <set>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "rigs/Rig.h"
#include "dsp/effects/Modulation.h"
#include "dsp/effects/Delay.h"
#include "dsp/effects/Mars.h"
#include "dsp/effects/Reverb.h"
#include "dsp/effects/Glitch.h"
#include "dsp/effects/TalkBox.h"
#include "ui/EqPresets.h"
#include "ui/NoteMapEditor.h"

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    // Reports a fixed tempo, like a DAW with its transport set to `bpm`.
    struct FakePlayHead final : juce::AudioPlayHead
    {
        double bpm = 120.0;
        bool playing = false;              // a transport that's playing reports the song position
        double startBeats = 0.0;           // where the song was when the run began, in beats
        juce::int64 samplesPlayed = 0;     // moved on by EffectRig::run

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (bpm);
            info.setIsPlaying (playing);
            if (playing)
                info.setPpqPosition (startBeats + (double) samplesPlayed / sampleRate * bpm / 60.0);
            return info;
        }
    };

    struct EffectRig
    {
        explicit EffectRig (int outputChannels = 2) : channels (outputChannels)
        {
            processor.setPlayConfigDetails (1, channels, sampleRate, blockSize);
            processor.setPlayHead (&playHead);
            // LOW CUT off, so each test hears only the effect it's about.
            // The LOW CUT tests turn it back on.
            set (ParamIDs::inputLowCut, effects::LowCut::offHz);
            processor.prepareToPlay (sampleRate, blockSize);
        }

        void set (const char* id, float plainValue)
        {
            auto* parameter = processor.getParameters().getParameter (id);
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
        }

        // Runs `input` through the plugin in blocks; returns every output channel.
        std::vector<std::vector<float>> run (const std::vector<float>& input)
        {
            std::vector<std::vector<float>> output ((size_t) channels);
            juce::AudioBuffer<float> buffer (channels, blockSize);
            juce::MidiBuffer midi;

            for (size_t start = 0; start < input.size(); start += blockSize)
            {
                const auto count = (int) std::min<size_t> (blockSize, input.size() - start);
                buffer.setSize (channels, count, false, false, true);
                buffer.clear();
                std::copy (input.begin() + (long) start, input.begin() + (long) start + count, buffer.getWritePointer (0));
                processor.processBlock (buffer, midi);
                playHead.samplesPlayed += count;
                for (int ch = 0; ch < channels; ++ch)
                    output[(size_t) ch].insert (output[(size_t) ch].end(), buffer.getReadPointer (ch), buffer.getReadPointer (ch) + count);
            }
            return output;
        }

        std::vector<float> runMono (const std::vector<float>& input) { return run (input)[0]; }

        int channels;
        FakePlayHead playHead;
        ParagonX1AudioProcessor processor;
    };

    std::vector<float> sine (double hz, float amplitude, double seconds)
    {
        std::vector<float> samples ((size_t) (seconds * sampleRate));
        for (size_t i = 0; i < samples.size(); ++i)
            samples[i] = amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) i / sampleRate);
        return samples;
    }

    std::vector<float> impulse (double seconds, float amplitude = 1.0f)
    {
        std::vector<float> samples ((size_t) (seconds * sampleRate), 0.0f);
        samples[0] = amplitude;
        return samples;
    }

    std::vector<float> silence (double seconds) { return std::vector<float> ((size_t) (seconds * sampleRate), 0.0f); }

    float rms (const std::vector<float>& samples, double fromSeconds = 0.0, double toSeconds = 1.0e9)
    {
        const auto from = (size_t) (fromSeconds * sampleRate);
        const auto to = std::min (samples.size(), (size_t) (toSeconds * sampleRate));
        double sum = 0.0;
        for (auto i = from; i < to; ++i)
            sum += samples[i] * samples[i];
        return (float) std::sqrt (sum / (double) std::max<size_t> (1, to - from));
    }

    float dB (float ratio) { return 20.0f * std::log10 (ratio); }

    // Level of one frequency in a signal (Goertzel algorithm), skipping the
    // first `fromSeconds` so filters and fades have settled.
    float levelAt (const std::vector<float>& samples, double hz, double fromSeconds = 0.1)
    {
        const auto from = (size_t) (fromSeconds * sampleRate);
        const auto n = samples.size() - from;
        const auto coefficient = 2.0 * std::cos (juce::MathConstants<double>::twoPi * hz / sampleRate);
        double s1 = 0.0, s2 = 0.0;
        for (auto i = from; i < samples.size(); ++i)
        {
            const auto s0 = samples[i] + coefficient * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return (float) (2.0 * std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - coefficient * s1 * s2)) / (double) n);
    }

    bool allFinite (const std::vector<float>& samples)
    {
        return std::all_of (samples.begin(), samples.end(), [] (float s) { return std::isfinite (s); });
    }

    float maxAbsDifference (const std::vector<float>& a, const std::vector<float>& b, size_t from = 0)
    {
        float result = 0.0f;
        for (auto i = from; i < std::min (a.size(), b.size()); ++i)
            result = std::max (result, std::abs (a[i] - b[i]));
        return result;
    }
}

TEST_CASE ("With every effect off, both channels are the clean input", "[effects]")
{
    EffectRig rig;
    const auto input = sine (440.0, 0.3f, 0.5);
    const auto output = rig.run (input);
    CHECK (maxAbsDifference (input, output[0]) < 1.0e-6f);
    CHECK (maxAbsDifference (input, output[1]) < 1.0e-6f);
}

TEST_CASE ("Compressor turns loud playing down and leaves quiet playing alone", "[effects][comp]")
{
    // Threshold -18 dB, ratio 4:1. A peak at -6 dB is 12 dB over, so it
    // should come out 3 dB over: turned down by about 9 dB.
    EffectRig rig;
    rig.set (ParamIDs::compOn, 1.0f);
    rig.set (ParamIDs::compThreshold, -18.0f);
    rig.set (ParamIDs::compRatio, 4.0f);

    const auto loud = sine (220.0, juce::Decibels::decibelsToGain (-6.0f), 1.0);
    CHECK_THAT (dB (rms (rig.runMono (loud), 0.5) / rms (loud, 0.5)), WithinAbs (-9.0, 1.5));

    const auto quiet = sine (220.0, juce::Decibels::decibelsToGain (-30.0f), 1.0);
    CHECK_THAT (dB (rms (rig.runMono (quiet), 0.5) / rms (quiet, 0.5)), WithinAbs (0.0, 0.3));
}

TEST_CASE ("Daybreak compressor: COMP, DETAIL, MIX and INTENSITY do what they say", "[effects][comp]")
{
    const auto daybreak = [] (EffectRig& rig, float amount, float detail = 5.0f, float mixAmount = 1.0f, bool intensity = false)
    {
        rig.set (ParamIDs::compOn, 1.0f);
        rig.set (ParamIDs::compType, (float) effects::CompGate::Type::daybreak);
        rig.set (ParamIDs::compAmount, amount);
        rig.set (ParamIDs::compDetail, detail);
        rig.set (ParamIDs::compMix, mixAmount);
        rig.set (ParamIDs::compIntensity, intensity ? 1.0f : 0.0f);
    };

    SECTION ("Its curve: nothing below the threshold, more squeeze with more COMP, harder with INTENSITY")
    {
        CHECK (effects::CompGate::daybreakCurveDb (-50.0f, 5.0f, false) == 0.0f);
        CHECK (effects::CompGate::daybreakCurveDb (-6.0f, 8.0f, false) < effects::CompGate::daybreakCurveDb (-6.0f, 2.0f, false) - 6.0f);
        CHECK (effects::CompGate::daybreakCurveDb (-6.0f, 5.0f, true) < effects::CompGate::daybreakCurveDb (-6.0f, 5.0f, false) - 6.0f);
    }

    SECTION ("It evens out loud and soft playing, and the meter shows it working")
    {
        const auto levelFor = [&] (float amplitude, float amount)
        {
            EffectRig rig;
            daybreak (rig, amount);
            return rms (rig.runMono (sine (440.0, amplitude, 1.0)), 0.5);
        };
        // 24 dB apart going in; with COMP up, much closer coming out.
        const auto spreadIn = dB (0.5f / 0.0315f);
        const auto spreadOut = dB (levelFor (0.5f, 8.0f) / levelFor (0.0315f, 8.0f));
        CHECK (spreadOut < spreadIn - 10.0f);

        EffectRig rig;
        daybreak (rig, 8.0f);
        rig.runMono (sine (440.0, 0.5f, 0.5));
        CHECK (rig.processor.getCompGainReductionDb() < -6.0f);
    }

    SECTION ("MIX at 0 leaves the guitar as it was")
    {
        EffectRig rig;
        daybreak (rig, 10.0f, 5.0f, 0.0f, true);
        const auto input = sine (440.0, 0.5f, 0.5);
        CHECK (maxAbsDifference (rig.runMono (input), input, (size_t) (0.2 * sampleRate)) < 1.0e-3f);
    }

    SECTION ("DETAIL lets more of the pick attack through")
    {
        // A note that starts loud from silence: the first few milliseconds
        // against the settled level.
        auto input = silence (0.3);
        const auto note = sine (440.0, 0.5f, 0.5);
        input.insert (input.end(), note.begin(), note.end());
        const auto attackRatio = [&] (float detail)
        {
            EffectRig rig;
            daybreak (rig, 8.0f, detail);
            const auto output = rig.runMono (input);
            return rms (output, 0.3, 0.305) / rms (output, 0.6, 0.8);
        };
        CHECK (dB (attackRatio (10.0f) / attackRatio (0.0f)) > 4.0f);
    }

    SECTION ("Low bass doesn't make it pump")
    {
        const auto reductionFor = [&] (double hz)
        {
            EffectRig rig;
            daybreak (rig, 7.0f);
            rig.runMono (sine (hz, 0.4f, 0.5));
            return rig.processor.getCompGainReductionDb();
        };
        CHECK (reductionFor (40.0) > reductionFor (440.0) + 3.0f);
    }
}

TEST_CASE ("Revival compressor: GATE, COMPRESSION and GAIN do what they say", "[effects][comp][revival]")
{
    const auto revival = [] (EffectRig& rig, float compressionDb, float gainDb = 0.0f, float gateDb = effects::CompGate::revivalGateOffDb)
    {
        rig.set (ParamIDs::compOn, 1.0f);
        rig.set (ParamIDs::compType, (float) effects::CompGate::Type::revival);
        rig.set (ParamIDs::revivalCompression, compressionDb);
        rig.set (ParamIDs::revivalGain, gainDb);
        rig.set (ParamIDs::revivalGate, gateDb);
        rig.set (ParamIDs::outputLimiter, effects::Limiter::offDb);   // the master limiter out of the way
    };
    const auto levelOut = [&] (float inDb, float compressionDb, float gainDb = 0.0f, float gateDb = effects::CompGate::revivalGateOffDb)
    {
        EffectRig rig;
        revival (rig, compressionDb, gainDb, gateDb);
        return dB (rms (rig.runMono (sine (440.0, juce::Decibels::decibelsToGain (inDb), 1.0)), 0.5)
                   / rms (sine (440.0, juce::Decibels::decibelsToGain (inDb), 1.0), 0.5));
    };

    SECTION ("Its curve: a full-scale peak stays at full scale; lower COMPRESSION brings the rest up")
    {
        for (const auto compression : { 0.0f, -12.0f, -24.0f, -36.0f })
            CHECK_THAT (effects::CompGate::revivalCurveDb (0.0f, compression), WithinAbs (0.0, 0.01));
        CHECK (effects::CompGate::revivalCurveDb (-30.0f, -24.0f) > effects::CompGate::revivalCurveDb (-30.0f, -6.0f) + 10.0f);
        CHECK (effects::CompGate::revivalCurveDb (-30.0f, 0.0f) == 0.0f);   // 0: nothing at all
    }

    SECTION ("Pulling COMPRESSION down evens the voice out and makes it louder")
    {
        // 24 dB between a loud and a soft note going in...
        const auto spreadAt = [&] (float compression) { return (levelOut (-6.0f, compression) - 6.0f) - (levelOut (-30.0f, compression) - 30.0f); };
        CHECK_THAT (spreadAt (0.0f), WithinAbs (24.0, 0.5));
        CHECK (spreadAt (-36.0f) < 10.0f);                      // ...much less coming out
        CHECK (levelOut (-30.0f, -24.0f) > levelOut (-30.0f, -6.0f) + 8.0f);   // and the soft one louder

        EffectRig rig;
        revival (rig, -24.0f);
        rig.runMono (sine (440.0, 0.5f, 0.5));
        CHECK (rig.processor.getCompGainReductionDb() < -6.0f);   // the TOTAL meter shows it working
    }

    SECTION ("GAIN is a ceiling: nothing comes out above it, however hard it's pushed")
    {
        for (const auto gain : { 0.0f, -6.0f, -18.0f })
        {
            EffectRig rig;
            revival (rig, -36.0f, gain);
            const auto output = rig.runMono (sine (440.0, 0.9f, 1.0));
            auto peak = 0.0f;
            for (size_t i = (size_t) (0.2 * sampleRate); i < output.size(); ++i)
                peak = std::max (peak, std::abs (output[i]));
            CHECK (dB (peak) <= gain + 0.1f);
            CHECK (dB (peak) > gain - 1.0f);   // and it's right up at it: the limiter holds it there
        }
    }

    SECTION ("GATE eases quiet sounds down gently and leaves the voice alone")
    {
        // GATE at -30 dB: a sound 20 dB under it is taken well down; one 6 dB
        // under, by about 6 dB (2:1, gently); the voice well above, untouched.
        CHECK (levelOut (-50.0f, 0.0f, 0.0f, -30.0f) < levelOut (-50.0f, 0.0f) - 15.0f);
        const auto sixUnder = levelOut (-36.0f, 0.0f, 0.0f, -30.0f) - levelOut (-36.0f, 0.0f);
        CHECK_THAT (sixUnder, WithinAbs (-6.0, 1.5));
        CHECK_THAT (levelOut (-10.0f, 0.0f, 0.0f, -30.0f) - levelOut (-10.0f, 0.0f), WithinAbs (0.0, 0.2));
        {
            // A low note above GATE: left exactly as it was, not fluttered by its slow waves.
            EffectRig rig;
            revival (rig, 0.0f, 0.0f, -30.0f);
            const auto low = sine (110.0, juce::Decibels::decibelsToGain (-20.0f), 1.0);
            CHECK (maxAbsDifference (rig.runMono (low), low, (size_t) (0.3 * sampleRate)) < 1.0e-3f);
        }
        // Off: quiet sounds pass as they are.
        CHECK_THAT (levelOut (-60.0f, 0.0f), WithinAbs (0.0, 0.05));
    }

    SECTION ("Classic's noise gate and LEVEL don't act on Revival")
    {
        EffectRig rig;
        revival (rig, 0.0f);
        rig.set (ParamIDs::gateThreshold, -20.0f);
        rig.set (ParamIDs::compLevel, 12.0f);
        const auto input = sine (440.0, juce::Decibels::decibelsToGain (-40.0f), 1.0);
        CHECK_THAT (dB (rms (rig.runMono (input), 0.5) / rms (input, 0.5)), WithinAbs (0.0, 0.05));
    }
}

namespace
{
    // SATURATION on its MARS model, the master limiter out of the way.
    struct MarsRig : EffectRig
    {
        MarsRig()
        {
            set (ParamIDs::driveOn, 1.0f);
            set (ParamIDs::outputLimiter, effects::Limiter::offDb);
        }
        void band (int index, const char* part, float value) { set (ParamIDs::marsBand (index, part).toRawUTF8(), value); }
    };
}

TEST_CASE ("MARS: the bands split and add back up, and off is off", "[effects][drive][mars]")
{
    SECTION ("Switched off, it's exactly the input")
    {
        MarsRig rig;
        rig.set (ParamIDs::driveOn, 0.0f);
        const auto input = sine (440.0, 0.3f, 0.5);
        CHECK (maxAbsDifference (input, rig.runMono (input)) < 1.0e-6f);
    }

    SECTION ("MIX at 0: your voice as it was (in level and tone)")
    {
        for (const auto hz : { 100.0, 1000.0, 8000.0 })
        {
            MarsRig rig;
            rig.set (ParamIDs::marsMix, 0.0f);
            rig.band (0, "Drive", 1.0f);
            const auto input = sine (hz, 0.3f, 0.5);
            CHECK_THAT (dB (rms (rig.runMono (input), 0.2) / rms (input, 0.2)), WithinAbs (0.0, 0.1));
        }
    }

    SECTION ("At every slope, four bands with their saturation off add back up flat")
    {
        for (int slope = 0; slope < effects::Mars::slopeNames.size(); ++slope)
            for (const auto hz : { 60.0, 250.0, 700.0, 2500.0, 5000.0, 12000.0 })
            {
                MarsRig rig;
                rig.set (ParamIDs::marsBands, 3.0f);   // 4 bands
                rig.set (ParamIDs::marsSlope, (float) slope);
                for (int b = 0; b < 4; ++b)
                    rig.band (b, "On", 0.0f);
                const auto input = sine (hz, 0.3f, 0.5);
                INFO (effects::Mars::slopeNames[slope] << " at " << hz << " Hz");
                CHECK_THAT (dB (rms (rig.runMono (input), 0.25) / rms (input, 0.25)), WithinAbs (0.0, 0.3));
            }
    }

    SECTION ("Each band is its own: drive the highs, and a low note stays clean")
    {
        const auto harmonicsOf = [] (double hz, int drivenBand)
        {
            MarsRig rig;
            rig.set (ParamIDs::marsBands, 1.0f);   // 2 bands
            rig.set (ParamIDs::marsCross1, 600.0f);
            for (int b = 0; b < 2; ++b)
            {
                rig.band (b, "Style", (float) effects::Mars::Style::saturatedTube);
                rig.band (b, "Drive", 1.0f);
                rig.band (b, "On", b == drivenBand ? 1.0f : 0.0f);   // the other band passes clean
            }
            const auto output = rig.runMono (sine (hz, 0.3f, 0.5));
            return dB ((levelAt (output, 2.0 * hz, 0.2) + levelAt (output, 3.0 * hz, 0.2)) / levelAt (output, hz, 0.2));
        };
        CHECK (harmonicsOf (2000.0, 1) > -30.0f);   // the high band, driven: rich
        CHECK (harmonicsOf (150.0, 1) < -40.0f);    // a low note, in the clean low band: clean
        CHECK (harmonicsOf (150.0, 0) > -30.0f);    // unless the low band is driven
    }
}

TEST_CASE ("MARS: every style saturates more with DRIVE, its level kept even", "[effects][drive][mars]")
{
    for (int style = 0; style < effects::Mars::numStyles; ++style)
    {
        INFO (effects::Mars::styleNames[style]);
        const auto run = [style] (float drive)
        {
            MarsRig rig;
            rig.band (0, "Style", (float) style);
            rig.band (0, "Drive", drive);
            return rig.runMono (sine (330.0, 0.3f, 0.6));
        };
        const auto light = run (0.1f), heavy = run (1.0f);
        REQUIRE (allFinite (heavy));
        // What isn't the note itself: the harmonics (and, for the FX styles, whatever else they make).
        const auto other = [] (const std::vector<float>& output)
        {
            const auto total = rms (output, 0.3), fundamental = levelAt (output, 330.0, 0.3) / std::sqrt (2.0f);
            return std::sqrt (std::max (0.0f, total * total - fundamental * fundamental)) / total;
        };
        CHECK (other (heavy) > other (light) + 0.02f);
        // Level matching: full DRIVE is within a few dB of the input, not 36 dB louder.
        CHECK (std::abs (dB (rms (heavy, 0.3) / rms (sine (330.0, 0.3f, 0.6), 0.3))) < 8.0f);
    }

    SECTION ("Breakdown pitches the voice down: an octave at full DRIVE")
    {
        MarsRig rig;
        rig.band (0, "Style", (float) effects::Mars::Style::breakdown);
        rig.band (0, "Drive", 1.0f);
        const auto output = rig.runMono (sine (440.0, 0.3f, 0.8));
        CHECK (levelAt (output, 220.0, 0.4) > 0.2f * levelAt (output, 440.0, 0.4));
    }
}

TEST_CASE ("MARS: DYNAMICS, FEEDBACK, TONE, LEVEL and QUALITY do what they say", "[effects][drive][mars]")
{
    const auto clean = [] (MarsRig& rig)
    {
        rig.band (0, "Style", (float) effects::Mars::Style::cleanTube);
        rig.band (0, "Drive", 0.0f);
    };

    SECTION ("DYNAMICS right compresses: loud and soft come out closer")
    {
        const auto spread = [&] (float dynamics)
        {
            const auto levelFor = [&] (float amplitude)
            {
                MarsRig rig;
                clean (rig);
                rig.band (0, "Dynamics", dynamics);
                return rms (rig.runMono (sine (440.0, amplitude, 0.8)), 0.4);
            };
            return dB (levelFor (0.5f) / levelFor (0.02f));
        };
        CHECK (spread (1.0f) < spread (0.0f) - 10.0f);
    }

    SECTION ("DYNAMICS left gates: quiet sounds taken well down, loud ones through")
    {
        const auto through = [&] (float amplitude, float dynamics)
        {
            MarsRig rig;
            clean (rig);
            rig.band (0, "Dynamics", dynamics);
            const auto input = sine (440.0, amplitude, 0.8);
            return dB (rms (rig.runMono (input), 0.4) / rms (input, 0.4));
        };
        CHECK (through (0.003f, -1.0f) < through (0.003f, 0.0f) - 15.0f);
        CHECK_THAT (through (0.3f, -1.0f) - through (0.3f, 0.0f), WithinAbs (0.0, 1.0));
    }

    SECTION ("FEEDBACK rings at FEEDBACK FREQ")
    {
        const auto ringAt = [&] (float feedback)
        {
            MarsRig rig;
            rig.band (0, "Style", (float) effects::Mars::Style::warmTape);
            rig.band (0, "Feedback", feedback);
            rig.band (0, "FbFreq", 2000.0f);
            juce::Random random (7);
            std::vector<float> noise ((size_t) (0.8 * sampleRate));
            for (auto& sample : noise)
                sample = 0.1f * (random.nextFloat() * 2.0f - 1.0f);
            const auto output = rig.runMono (noise);
            return dB (levelAt (output, 2000.0, 0.3) / levelAt (output, 500.0, 0.3));
        };
        CHECK (ringAt (1.0f) > ringAt (0.0f) + 6.0f);
    }

    SECTION ("TONE: BASS and TREBLE shape what comes out")
    {
        const auto levelWith = [&] (const char* part, float db, double hz)
        {
            MarsRig rig;
            clean (rig);
            rig.band (0, part, db);
            return levelAt (rig.runMono (sine (hz, 0.1f, 0.5)), hz, 0.2);
        };
        CHECK_THAT (dB (levelWith ("Bass", 12.0f, 50.0) / levelWith ("Bass", 0.0f, 50.0)), WithinAbs (11.0, 1.5));
        CHECK_THAT (dB (levelWith ("Treble", -12.0f, 16000.0) / levelWith ("Treble", 0.0f, 16000.0)), WithinAbs (-11.0, 1.5));
        CHECK_THAT (dB (levelWith ("Treble", -12.0f, 300.0) / levelWith ("Treble", 0.0f, 300.0)), WithinAbs (0.0, 0.5));
    }

    SECTION ("LEVEL: a band all the way down is silent; OUTPUT moves everything")
    {
        MarsRig off;
        off.band (0, "Level", effects::Mars::minLevelDb);
        CHECK (rms (off.runMono (sine (440.0, 0.3f, 0.5)), 0.2) < 1.0e-4f);

        const auto outputLevel = [&] (float db)
        {
            MarsRig rig;
            clean (rig);
            rig.set (ParamIDs::marsOutput, db);
            return rms (rig.runMono (sine (440.0, 0.1f, 0.5)), 0.2);
        };
        CHECK_THAT (dB (outputLevel (-6.0f) / outputLevel (0.0f)), WithinAbs (-6.0, 0.2));
    }

    SECTION ("QUALITY: oversampling keeps the fold-back (aliasing) down")
    {
        // A 9 kHz note folded hard: its 5th harmonic, 45 kHz, folds back
        // down to 3 kHz without oversampling.
        const auto aliasAt3k = [&] (float quality)
        {
            MarsRig rig;
            rig.set (ParamIDs::marsQuality, quality);
            rig.band (0, "Style", (float) effects::Mars::Style::foldback);
            rig.band (0, "Drive", 0.7f);
            const auto output = rig.runMono (sine (9000.0, 0.3f, 0.5));
            return dB (levelAt (output, 3000.0, 0.2) / levelAt (output, 9000.0, 0.2));
        };
        CHECK (aliasAt3k (1.0f) < aliasAt3k (0.0f) - 15.0f);   // Good (4x) against Off
        CHECK (aliasAt3k (2.0f) <= aliasAt3k (1.0f) + 1.0f);   // Superb (8x) at least as clean
    }
}

TEST_CASE ("GLITCH: each mode chops, repeats or smears the guitar as it says", "[effects][glitch]")
{
    const auto glitchRig = [] (EffectRig& rig, effects::Glitch::Mode mode, float density, float chaos, float pitch = 0.0f, float mixAmount = 1.0f)
    {
        rig.set (ParamIDs::glitchOn, 1.0f);
        rig.set (ParamIDs::glitchMode, (float) mode);
        rig.set (ParamIDs::glitchSync, 0.0f);
        rig.set (ParamIDs::glitchTime, 100.0f);   // 4800 samples at 48 kHz
        rig.set (ParamIDs::glitchDensity, density);
        rig.set (ParamIDs::glitchChaos, chaos);
        rig.set (ParamIDs::glitchPitch, pitch);
        rig.set (ParamIDs::glitchMix, mixAmount);
    };
    // Noise: every stretch of it different, so a repeat is easy to recognise.
    std::vector<float> noise ((size_t) sampleRate);
    juce::Random random (42);
    for (auto& sample : noise)
        sample = (random.nextFloat() * 2.0f - 1.0f) * 0.3f;

    SECTION ("MIX at 0 leaves the guitar as it was, in every mode")
    {
        for (const auto mode : { effects::Glitch::Mode::pitchDelay, effects::Glitch::Mode::sequence,
                                 effects::Glitch::Mode::interfere, effects::Glitch::Mode::texturizer })
        {
            EffectRig rig;
            glitchRig (rig, mode, 10.0f, 10.0f, 7.0f, 0.0f);
            const auto output = rig.runMono (noise);
            REQUIRE (allFinite (output));
            CHECK (maxAbsDifference (output, noise, (size_t) (0.05 * sampleRate)) < 1.0e-3f);
        }
    }

    SECTION ("Sequence, every step and no chaos: each step repeats the one before it")
    {
        EffectRig rig;
        glitchRig (rig, effects::Glitch::Mode::sequence, 10.0f, 0.0f);
        const auto output = rig.runMono (noise);
        constexpr size_t step = 4800;
        // The middle of the fourth step is the third step's audio (clear of the short fades at the edges).
        std::vector<float> got (output.begin() + 3 * step + 200, output.begin() + 4 * step - 200);
        std::vector<float> expected (noise.begin() + 2 * step + 200, noise.begin() + 3 * step - 200);
        CHECK (maxAbsDifference (got, expected) < 0.01f);
    }

    SECTION ("Interfere: the signal drops out in places")
    {
        EffectRig rig;
        glitchRig (rig, effects::Glitch::Mode::interfere, 10.0f, 0.0f);
        const auto output = rig.runMono (noise);
        REQUIRE (allFinite (output));
        auto silentStretches = 0;
        for (double start = 0.1; start < 0.95; start += 0.005)
            if (rms (output, start, start + 0.005) < 0.01f * rms (noise))
                ++silentStretches;
        CHECK (silentStretches >= 3);
    }

    SECTION ("Pitch Delay: each echo an octave above the last")
    {
        EffectRig rig;
        glitchRig (rig, effects::Glitch::Mode::pitchDelay, 4.0f, 0.0f, 12.0f);
        auto input = sine (330.0, 0.3f, 0.3);
        const auto tail = silence (0.7);
        input.insert (input.end(), tail.begin(), tail.end());
        const auto output = rig.runMono (input);
        REQUIRE (allFinite (output));
        // After the note stops, the echoes are up an octave (and more), not at the note.
        std::vector<float> echoes (output.begin() + (size_t) (0.35 * sampleRate), output.begin() + (size_t) (0.5 * sampleRate));
        CHECK (dB (levelAt (echoes, 660.0, 0.0) / (levelAt (echoes, 330.0, 0.0) + 1.0e-9f)) > 10.0f);
    }

    SECTION ("Texturizer: a cloud of grains that carries on after the note")
    {
        EffectRig rig;
        glitchRig (rig, effects::Glitch::Mode::texturizer, 6.0f, 2.0f);
        auto input = sine (440.0, 0.3f, 0.6);
        const auto tail = silence (0.6);
        input.insert (input.end(), tail.begin(), tail.end());
        const auto output = rig.runMono (input);
        REQUIRE (allFinite (output));
        CHECK (rms (output, 0.65, 0.75) > 0.02f);                     // still sounding after the note
        CHECK (dB (rms (output, 0.2, 0.55) / 0.212f) > -12.0f);       // about as loud as the guitar while playing
    }

    SECTION ("The pedal on GLITCH fades it in: dry at the heel")
    {
        EffectRig rig;
        glitchRig (rig, effects::Glitch::Mode::interfere, 10.0f, 5.0f);
        rig.set (ParamIDs::expressionTarget, (float) ExpressionTargets::glitch);
        rig.set (ParamIDs::expression, 0.0f);
        CHECK (maxAbsDifference (rig.runMono (noise), noise, (size_t) (0.1 * sampleRate)) < 1.0e-3f);
    }

    SECTION ("Synced, with the song playing: steps start on the song's grid lines")
    {
        // 1/16 at 120 BPM: steps of a quarter of a beat (6000 samples). The
        // song starts 0.1 beats in, so the first grid line comes 3600 samples
        // in, then every 6000. Every step repeats the one before it, so the
        // step from 9600 is a repeat of the audio from 3600 to 9600.
        const auto run = [&] (bool playing)
        {
            EffectRig rig;
            glitchRig (rig, effects::Glitch::Mode::sequence, 10.0f, 0.0f);
            rig.set (ParamIDs::glitchSync, 1.0f);
            rig.set (ParamIDs::glitchDivision, 3.0f);   // 1/16
            rig.playHead.playing = playing;
            rig.playHead.startBeats = 0.1;
            return rig.runMono (noise);
        };
        const auto matchesGrid = [&] (const std::vector<float>& output)
        {
            std::vector<float> got (output.begin() + 9600 + 200, output.begin() + 15600 - 200);
            std::vector<float> expected (noise.begin() + 3600 + 200, noise.begin() + 9600 - 200);
            return maxAbsDifference (got, expected) < 0.01f;
        };
        CHECK (matchesGrid (run (true)));
        CHECK_FALSE (matchesGrid (run (false)));   // stopped: steps count from switch-on (0, 6000, 12000...)
    }

    SECTION ("Killswitch: each step plays, then cuts to silence for its last part")
    {
        // DENSITY 5: the last half of each 100 ms step (4800 samples) is cut.
        EffectRig rig;
        glitchRig (rig, effects::Glitch::Mode::killswitch, 5.0f, 0.0f);
        const auto output = rig.runMono (noise);
        for (int step = 2; step < 8; ++step)
        {
            const auto at = [&] (int sample) { return (double) (step * 4800 + sample) / sampleRate; };
            CHECK (rms (output, at (2600), at (4700)) < 1.0e-3f);                            // cut
            CHECK (rms (output, at (300), at (2200)) > 0.8f * rms (noise, at (300), at (2200)));   // playing
        }
    }

    SECTION ("PRE and POST both glitch the guitar")
    {
        for (const auto position : { effects::Glitch::Position::post, effects::Glitch::Position::pre })
        {
            EffectRig rig;
            glitchRig (rig, effects::Glitch::Mode::sequence, 10.0f, 0.0f);
            rig.set (ParamIDs::glitchPosition, (float) position);
            const auto output = rig.runMono (noise);
            CHECK (maxAbsDifference (output, noise, (size_t) (0.2 * sampleRate)) > 0.1f);
        }
    }
}

TEST_CASE ("Noise gate silences signal below its threshold", "[effects][comp]")
{
    EffectRig rig;
    rig.set (ParamIDs::compOn, 1.0f);
    rig.set (ParamIDs::compThreshold, 0.0f);   // compressor out of the way
    rig.set (ParamIDs::gateThreshold, -50.0f);

    const auto hiss = sine (3000.0, juce::Decibels::decibelsToGain (-65.0f), 1.0);
    CHECK (dB (rms (rig.runMono (hiss), 0.5) / rms (hiss, 0.5)) < -20.0f);

    const auto note = sine (220.0, juce::Decibels::decibelsToGain (-20.0f), 1.0);
    CHECK_THAT (dB (rms (rig.runMono (note), 0.5) / rms (note, 0.5)), WithinAbs (0.0, 0.5));
}

TEST_CASE ("Modulation effects", "[effects][mod]")
{
    EffectRig rig;
    rig.set (ParamIDs::modOn, 1.0f);
    const auto input = sine (440.0, 0.3f, 1.0);

    SECTION ("Chorus spreads the mono guitar across both speakers")
    {
        rig.set (ParamIDs::modType, 0.0f);
        const auto output = rig.run (input);
        REQUIRE (allFinite (output[0]));
        std::vector<float> difference (output[0].size());
        for (size_t i = 0; i < difference.size(); ++i)
            difference[i] = output[0][i] - output[1][i];
        CHECK (rms (difference, 0.2) > 0.05f * rms (output[0], 0.2));
    }

    SECTION ("Tremolo pulses the volume at the rate setting")
    {
        rig.set (ParamIDs::modType, 3.0f);
        rig.set (ParamIDs::modRate, 4.0f);
        rig.set (ParamIDs::modDepth, 1.0f);
        const auto output = rig.runMono (input);

        // Measure loudness in 10 ms windows: full depth swings from silence to full.
        float loudest = 0.0f, quietest = 1.0f;
        for (double t = 0.2; t < 0.95; t += 0.01)
        {
            const auto level = rms (output, t, t + 0.01);
            loudest = std::max (loudest, level);
            quietest = std::min (quietest, level);
        }
        CHECK (loudest > 0.18f);
        CHECK (quietest < 0.02f);
    }

    SECTION ("Flanger and phaser change the sound and stay stable")
    {
        for (const auto type : { 1.0f, 2.0f })
        {
            rig.set (ParamIDs::modType, type);
            const auto output = rig.runMono (input);
            REQUIRE (allFinite (output));
            CHECK (maxAbsDifference (input, output, (size_t) (0.2 * sampleRate)) > 0.01f);
        }
    }
}

namespace
{
    // Loudness in 10 ms windows from `fromSeconds` on: how the volume moves.
    std::vector<float> envelope (const std::vector<float>& samples, double fromSeconds = 0.2)
    {
        std::vector<float> levels;
        for (auto t = fromSeconds; (t + 0.01) * sampleRate <= (double) samples.size(); t += 0.01)
            levels.push_back (rms (samples, t, t + 0.01));
        return levels;
    }

    // +1 when two envelopes rise and fall together, -1 when one rises as the other falls.
    double correlation (const std::vector<float>& a, const std::vector<float>& b)
    {
        const auto n = std::min (a.size(), b.size());
        double meanA = 0.0, meanB = 0.0;
        for (size_t i = 0; i < n; ++i) { meanA += a[i]; meanB += b[i]; }
        meanA /= (double) n;
        meanB /= (double) n;
        double ab = 0.0, aa = 0.0, bb = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            ab += (a[i] - meanA) * (b[i] - meanB);
            aa += (a[i] - meanA) * (a[i] - meanA);
            bb += (b[i] - meanB) * (b[i] - meanB);
        }
        return ab / std::sqrt (aa * bb + 1.0e-30);
    }

    using ModType = effects::Modulation::Type;
    float typeIndex (ModType type) { return (float) (int) type; }
}

TEST_CASE ("The newer modulation types", "[effects][mod]")
{
    const auto input = sine (440.0, 0.3f, 1.0);

    SECTION ("Every type changes the sound and stays stable")
    {
        for (const auto type : { ModType::dimension, ModType::uniVibe, ModType::rotary, ModType::vibrato,
                                 ModType::harmonicTremolo, ModType::autoPan })
        {
            EffectRig rig;
            rig.set (ParamIDs::modOn, 1.0f);
            rig.set (ParamIDs::modType, typeIndex (type));
            rig.set (ParamIDs::modDepth, 0.8f);
            rig.set (ParamIDs::modMix, 0.5f);
            const auto output = rig.run (input);
            INFO (effects::Modulation::typeNames[(int) type]);
            REQUIRE (allFinite (output[0]));
            REQUIRE (allFinite (output[1]));
            CHECK (maxAbsDifference (input, output[0], (size_t) (0.2 * sampleRate)) > 0.01f);
            CHECK (rms (output[0], 0.2) < 2.0f * rms (input, 0.2));   // nothing runs away
        }
    }

    SECTION ("Dimension chorus and rotary are stereo")
    {
        for (const auto type : { ModType::dimension, ModType::rotary })
        {
            EffectRig rig;
            rig.set (ParamIDs::modOn, 1.0f);
            rig.set (ParamIDs::modType, typeIndex (type));
            rig.set (ParamIDs::modMix, 1.0f);
            const auto output = rig.run (input);
            std::vector<float> difference (output[0].size());
            for (size_t i = 0; i < difference.size(); ++i)
                difference[i] = output[0][i] - output[1][i];
            INFO (effects::Modulation::typeNames[(int) type]);
            CHECK (rms (difference, 0.2) > 0.05f * rms (output[0], 0.2));
        }
    }

    SECTION ("Uni-Vibe's wet signal only shifts phase, so it keeps the level")
    {
        EffectRig rig;
        rig.set (ParamIDs::modOn, 1.0f);
        rig.set (ParamIDs::modType, typeIndex (ModType::uniVibe));
        rig.set (ParamIDs::modMix, 1.0f);
        rig.set (ParamIDs::modDepth, 1.0f);
        CHECK_THAT (rms (rig.runMono (input), 0.2) / rms (input, 0.2), WithinAbs (1.0, 0.05));
    }

    SECTION ("Auto-pan moves the sound from one speaker to the other")
    {
        EffectRig rig;
        rig.set (ParamIDs::modOn, 1.0f);
        rig.set (ParamIDs::modType, typeIndex (ModType::autoPan));
        rig.set (ParamIDs::modRate, 2.0f);
        rig.set (ParamIDs::modDepth, 1.0f);
        const auto output = rig.run (input);
        CHECK (correlation (envelope (output[0]), envelope (output[1])) < -0.9);
    }

    SECTION ("Harmonic tremolo pulses the lows and highs in turn")
    {
        const auto pulse = [] (double hz)
        {
            EffectRig rig;
            rig.set (ParamIDs::modOn, 1.0f);
            rig.set (ParamIDs::modType, typeIndex (ModType::harmonicTremolo));
            rig.set (ParamIDs::modRate, 3.0f);
            rig.set (ParamIDs::modDepth, 1.0f);
            return envelope (rig.runMono (sine (hz, 0.3f, 1.0)));
        };
        CHECK (correlation (pulse (150.0), pulse (5000.0)) < -0.8);
    }
}

TEST_CASE ("The pitch shifter moves notes by its interval", "[effects][pitch]")
{
    const auto shifted = [] (float semitones, double hz)
    {
        EffectRig rig;
        rig.set (ParamIDs::fpOn, 1.0f);
        rig.set (ParamIDs::fpType, (float) effects::FilterPitch::Type::pitchShifter);
        rig.set (ParamIDs::fpInterval, semitones);
        rig.set (ParamIDs::fpMix, 1.0f);
        return rig.runMono (sine (hz, 0.3f, 1.0));
    };

    for (const auto [semitones, ratio] : { std::pair { 12.0f, 2.0 }, { -12.0f, 0.5 }, { 7.0f, 1.4983 }, { -5.0f, 0.7492 }, { 24.0f, 4.0 } })
    {
        INFO (semitones << " semitones");
        const auto output = shifted (semitones, 330.0);
        REQUIRE (allFinite (output));
        const auto target = levelAt (output, 330.0 * ratio, 0.2);
        CHECK (dB (target / levelAt (output, 330.0, 0.2)) > 20.0f);   // the original pitch is gone
        CHECK (dB (target / 0.3f) > -6.0f);                           // and the new one is about as loud
    }
}

TEST_CASE ("Whammy-Wah: the expression pedal plays the XP-100's programs", "[effects][pitch][wah]")
{
    const auto modeIndex = [] (const juce::String& name)
    {
        const auto index = effects::FilterPitch::xpProgramNames().indexOf (name);
        REQUIRE (index >= 0);
        return (float) index;
    };
    const auto play = [&] (const juce::String& mode, float pedal, const std::vector<float>& input, bool assigned = true)
    {
        EffectRig rig;
        rig.set (ParamIDs::fpOn, 1.0f);
        rig.set (ParamIDs::fpType, (float) effects::FilterPitch::Type::whammyWah);
        rig.set (ParamIDs::fpXpProgram, modeIndex (mode));
        rig.set (ParamIDs::fpMix, 1.0f);
        rig.set (ParamIDs::expressionTarget, (float) (assigned ? ExpressionTargets::whammyWah : ExpressionTargets::off));
        rig.set (ParamIDs::expression, pedal);
        const auto output = rig.runMono (input);
        REQUIRE (allFinite (output));
        return output;
    };
    const auto note = sine (330.0, 0.3f, 1.0);

    SECTION ("Whammy: no shift at the heel, the full interval at the toe, wet only")
    {
        const auto heel = play ("13 Up 1 Octave", 0.0f, note);
        CHECK (dB (levelAt (heel, 330.0, 0.2) / (levelAt (heel, 660.0, 0.2) + 1.0e-9f)) > 20.0f);
        const auto toe = play ("13 Up 1 Octave", 1.0f, note);
        CHECK (dB (levelAt (toe, 660.0, 0.2) / levelAt (toe, 330.0, 0.2)) > 20.0f);
        const auto down = play ("18 Down 1 Octave", 1.0f, note);
        CHECK (dB (levelAt (down, 165.0, 0.2) / levelAt (down, 330.0, 0.2)) > 20.0f);
        const auto second = play ("15 Up 2nd", 1.0f, note);
        CHECK (dB (levelAt (second, 330.0 * 1.1225, 0.2) / levelAt (second, 330.0, 0.2)) > 20.0f);

        // 20, down six octaves: the note is gone, into a rumble, and nothing blows up.
        const auto dive = play ("20 Down 6 Octaves", 1.0f, note);
        CHECK (dB (levelAt (dive, 330.0, 0.2) / 0.3f) < -30.0f);
    }

    SECTION ("Whammy: unassigned, the pedal doesn't move it (it rests at the heel)")
    {
        const auto output = play ("13 Up 1 Octave", 1.0f, note, false);
        CHECK (dB (levelAt (output, 330.0, 0.2) / (levelAt (output, 660.0, 0.2) + 1.0e-9f)) > 20.0f);
    }

    SECTION ("Harmony: the guitar plus a voice the pedal moves between two intervals")
    {
        const auto heel = play ("28 Up 5th / Up Oct", 0.0f, note);
        CHECK (dB (levelAt (heel, 330.0 * 1.4983, 0.2) / levelAt (heel, 330.0, 0.2)) > -6.0f);   // a 5th up, beside the note
        CHECK (dB (levelAt (heel, 330.0, 0.2) / 0.3f) > -6.0f);                                   // which is still there
        const auto toe = play ("28 Up 5th / Up Oct", 1.0f, note);
        CHECK (dB (levelAt (toe, 660.0, 0.2) / levelAt (toe, 330.0, 0.2)) > -6.0f);              // now an octave up
        CHECK (dB (levelAt (toe, 330.0 * 1.4983, 0.2) / levelAt (toe, 660.0, 0.2)) < -20.0f);    // and the 5th has gone
    }

    SECTION ("Wah: rocking to the toe opens it up")
    {
        // A bright test note: a 220 Hz tone with its first 15 harmonics.
        std::vector<float> bright ((size_t) sampleRate);
        for (size_t i = 0; i < bright.size(); ++i)
            for (int harmonic = 1; harmonic <= 15; ++harmonic)
                bright[i] += 0.1f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * harmonic * (double) i / sampleRate) / (float) harmonic;
        const auto brightness = [&] (float pedal)
        {
            const auto output = play ("02 Original Cry Wah", pedal, bright);
            return dB (levelAt (output, 220.0 * 9) / levelAt (output, 220.0 * 2));   // 2 kHz against 440 Hz
        };
        CHECK (brightness (1.0f) - brightness (0.0f) > 15.0f);

        // Auto-wah, pedal on sensitivity: the same playing opens it much
        // further with the pedal at the toe.
        const auto autoBrightness = [&] (float pedal)
        {
            const auto output = play ("07 Cry Auto Sens", pedal, bright);
            return dB (levelAt (output, 220.0 * 9) / levelAt (output, 220.0 * 2));
        };
        CHECK (autoBrightness (1.0f) - autoBrightness (0.0f) > 10.0f);
    }

    SECTION ("Volume: silent at the heel, the full guitar at the toe")
    {
        CHECK (rms (play ("01 Volume Pedal", 0.0f, note), 0.2) < 1.0e-4f);
        CHECK_THAT (rms (play ("01 Volume Pedal", 1.0f, note), 0.2), WithinAbs (rms (note, 0.2), 0.01));
    }
}

TEST_CASE ("PITCH: TUNING retunes the guitar, the OCTAVER stacks octaves, FREEZE holds a note", "[effects][pitch]")
{
    const auto note = sine (330.0, 0.3f, 1.0);

    SECTION ("TUNING: -12 an octave down, +2 a whole step up, 0 untouched")
    {
        const auto tuned = [&] (float semitones)
        {
            EffectRig rig;
            rig.set (ParamIDs::pitchTune, semitones);
            const auto output = rig.runMono (note);
            REQUIRE (allFinite (output));
            return output;
        };
        const auto down = tuned (-12.0f);
        CHECK (dB (levelAt (down, 165.0, 0.2) / levelAt (down, 330.0, 0.2)) > 20.0f);
        const auto up = tuned (2.0f);
        CHECK (dB (levelAt (up, 330.0 * 1.1225, 0.2) / levelAt (up, 330.0, 0.2)) > 20.0f);
        CHECK (maxAbsDifference (tuned (0.0f), note) < 1.0e-4f);
    }

    SECTION ("OCTAVER: each voice on its octave; DRY alone is the guitar as it was")
    {
        const auto octaver = [&] (float subTwo, float subOne, float dryLevel, float upOne)
        {
            EffectRig rig;
            rig.set (ParamIDs::octOn, 1.0f);
            rig.set (ParamIDs::octSubTwo, subTwo);
            rig.set (ParamIDs::octSubOne, subOne);
            rig.set (ParamIDs::octDry, dryLevel);
            rig.set (ParamIDs::octUp, upOne);
            const auto output = rig.runMono (note);
            REQUIRE (allFinite (output));
            return output;
        };
        const auto oneDown = octaver (0.0f, 1.0f, 0.0f, 0.0f);
        CHECK (dB (levelAt (oneDown, 165.0, 0.2) / levelAt (oneDown, 330.0, 0.2)) > 20.0f);
        const auto twoDown = octaver (1.0f, 0.0f, 0.0f, 0.0f);
        CHECK (dB (levelAt (twoDown, 82.5, 0.2) / levelAt (twoDown, 330.0, 0.2)) > 20.0f);
        const auto oneUp = octaver (0.0f, 0.0f, 0.0f, 1.0f);
        CHECK (dB (levelAt (oneUp, 660.0, 0.2) / levelAt (oneUp, 330.0, 0.2)) > 20.0f);
        CHECK (maxAbsDifference (octaver (0.0f, 0.0f, 1.0f, 0.0f), note, (size_t) (0.1 * sampleRate)) < 1.0e-3f);
    }

    SECTION ("FREEZE: holds the note after you stop, and fades when switched off")
    {
        EffectRig rig;
        rig.set (ParamIDs::freezeAttack, 50.0f);
        rig.set (ParamIDs::freezeRelease, 200.0f);
        rig.runMono (sine (440.0, 0.3f, 0.6));        // play a note...
        rig.set (ParamIDs::freezeOn, 1.0f);            // ...freeze it...
        const auto held = rig.runMono (silence (1.0)); // ...and stop playing
        REQUIRE (allFinite (held));
        // Still sounding, a second later, and still the note: its pieces come
        // from random places in what was caught, so measure loudness (their
        // phases don't line up over a long stretch) and check that a short
        // stretch is mostly 440 Hz.
        CHECK (rms (held, 0.3) > 0.05f);
        std::vector<float> stretch (held.begin() + (size_t) (0.5 * sampleRate), held.begin() + (size_t) (0.52 * sampleRate));
        CHECK (dB (levelAt (stretch, 440.0, 0.0) / levelAt (stretch, 1000.0, 0.0)) > 20.0f);
        rig.set (ParamIDs::freezeOn, 0.0f);
        const auto released = rig.runMono (silence (0.6));
        CHECK (rms (released, 0.4) < 1.0e-3f);         // gone after its release
    }
}

TEST_CASE ("Talk Box: the pedal shapes the amp's sound from oo to ee", "[effects][wah]")
{
    // The vowels' formants: oo low, ee with its second formant up high.
    CHECK (effects::TalkBox::formantsFor (0.0f)[1] < 1000.0f);
    CHECK (effects::TalkBox::formantsFor (1.0f)[1] > 2000.0f);

    // A bright, harmonic-rich note (like a driven amp): at the toe the energy
    // near 2.3 kHz (ee's second formant) is far stronger than at the heel.
    std::vector<float> bright ((size_t) sampleRate);
    for (size_t i = 0; i < bright.size(); ++i)
        for (int harmonic = 1; harmonic <= 30; ++harmonic)
            bright[i] += 0.05f * (float) std::sin (juce::MathConstants<double>::twoPi * 110.0 * harmonic * (double) i / sampleRate) / std::sqrt ((float) harmonic);
    const auto ee = [&] (float pedal)
    {
        EffectRig rig;
        rig.set (ParamIDs::fpOn, 1.0f);
        rig.set (ParamIDs::fpType, (float) effects::FilterPitch::Type::talkBox);
        rig.set (ParamIDs::fpMix, 1.0f);
        rig.set (ParamIDs::expressionTarget, (float) ExpressionTargets::talkBox);
        rig.set (ParamIDs::expression, pedal);
        const auto output = rig.runMono (bright);
        REQUIRE (allFinite (output));
        return dB (levelAt (output, 110.0 * 21, 0.2) / levelAt (output, 110.0 * 3, 0.2));   // 2.3 kHz against 330 Hz
    };
    CHECK (ee (1.0f) - ee (0.0f) > 9.0f);   // measured about 12 dB: as big a change as between the two vowels in speech
}

TEST_CASE ("The auto-wah opens up as you play harder", "[effects][wah]")
{
    // A bright test note: a 220 Hz tone with its first 15 harmonics.
    std::vector<float> note ((size_t) sampleRate);
    for (size_t i = 0; i < note.size(); ++i)
        for (int harmonic = 1; harmonic <= 15; ++harmonic)
            note[i] += (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * harmonic * (double) i / sampleRate) / (float) harmonic;

    const auto brightness = [&] (float level)
    {
        EffectRig rig;
        rig.set (ParamIDs::fpOn, 1.0f);
        rig.set (ParamIDs::fpType, (float) effects::FilterPitch::Type::autoWah);
        rig.set (ParamIDs::fpSensitivity, 5.0f);
        auto input = note;
        for (auto& sample : input)
            sample *= level;
        const auto output = rig.runMono (input);
        REQUIRE (allFinite (output));
        return dB (levelAt (output, 220.0 * 11) / levelAt (output, 220.0 * 2));   // 2.4 kHz against 440 Hz
    };

    CHECK (brightness (0.3f) - brightness (0.003f) > 12.0f);
}

TEST_CASE ("DOUBLER, as it starts: a take on each side", "[effects][doubler]")
{
    EffectRig rig;
    rig.set (ParamIDs::doublerOn, 1.0f);
    const auto input = sine (440.0, 0.3f, 1.0);
    const auto output = rig.run (input);
    REQUIRE (allFinite (output[0]));
    REQUIRE (allFinite (output[1]));

    std::vector<float> difference (output[0].size());
    for (size_t i = 0; i < difference.size(); ++i)
        difference[i] = output[0][i] - output[1][i];
    CHECK (rms (difference, 0.2) > 0.1f * rms (output[0], 0.2));   // the two sides differ
    CHECK (rms (output[0], 0.2) < 2.0f * rms (input, 0.2));

    // Since 0.9.0 the takes start strong: each nearly as loud as you (-2 dB),
    // one on each side, so a live voice sounds doubled rather than just wider.
    EffectRig takesOnly;
    takesOnly.set (ParamIDs::doublerOn, 1.0f);
    takesOnly.set (ParamIDs::doublerDirectOn, 0.0f);
    const auto note = sine (220.0, 0.2f, 1.5);
    const auto takes = takesOnly.run (note);
    CHECK_THAT (dB (rms (takes[0], 0.5) / rms (note, 0.5)), WithinAbs (-2.0, 1.5));
    CHECK_THAT (dB (rms (takes[1], 0.5) / rms (note, 0.5)), WithinAbs (-2.0, 1.5));
}

TEST_CASE ("Projects saved with a pedal drive model load as MARS, set up close to it", "[effects][drive][mars]")
{
    // As 0.13.0 or before saved it: SATURATION on, a pedal model with its DRIVE, TONE and LEVEL.
    const auto loadOld = [] (int model, float gain, float tone, float level, bool on = true)
    {
        EffectRig original;
        juce::MemoryBlock saved;
        original.processor.getStateInformation (saved);
        auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
        REQUIRE (xml != nullptr);
        xml->setAttribute ("parameterVersion", 35);
        const std::pair<const char*, float> old[] = { { ParamIDs::driveOn, on ? 1.0f : 0.0f }, { ParamIDs::driveModel, (float) model },
                                                      { ParamIDs::driveGain, gain }, { ParamIDs::driveTone, tone }, { ParamIDs::driveLevel, level } };
        for (const auto& [id, value] : old)
        {
            auto* child = xml->getChildByAttribute ("id", id);
            if (child == nullptr)
            {
                child = xml->createNewChildElement ("PARAM");
                child->setAttribute ("id", id);
            }
            child->setAttribute ("value", value);
        }
        juce::MemoryBlock older;
        juce::AudioProcessor::copyXmlToBinary (*xml, older);
        auto rig = std::make_unique<EffectRig>();
        rig->processor.setStateInformation (older.getData(), (int) older.getSize());
        return rig;
    };
    const auto value = [] (EffectRig& rig, const juce::String& id) { return rig.processor.getParameters().getRawParameterValue (id)->load(); };

    SECTION ("Screamer: the bass kept clean below 720 Hz, the mids driven")
    {
        auto rig = loadOld (1, 6.0f, 7.0f, -2.0f);
        CHECK (value (*rig, ParamIDs::driveOn) > 0.5f);
        CHECK (juce::roundToInt (value (*rig, ParamIDs::marsBands)) == 1);   // 2 bands
        CHECK_THAT (value (*rig, ParamIDs::marsCross1), WithinAbs (720.0, 1.0));
        CHECK (value (*rig, ParamIDs::marsBand (0, "On")) < 0.5f);
        CHECK (juce::roundToInt (value (*rig, ParamIDs::marsBand (1, "Style"))) == (int) effects::Mars::Style::saturatedTube);
        CHECK_THAT (value (*rig, ParamIDs::marsBand (1, "Drive")), WithinAbs (0.15 + 0.07 * 6.0, 0.01));
        CHECK_THAT (value (*rig, ParamIDs::marsBand (1, "Treble")), WithinAbs (4.0, 0.01));   // TONE 7: +4 dB
        CHECK_THAT (value (*rig, ParamIDs::marsBand (1, "Level")), WithinAbs (-2.0, 0.01));

        // And it sounds like it: a low note clean, a high one driven.
        const auto harmonics = [&] (double hz)
        {
            auto fresh = loadOld (1, 8.0f, 5.0f, 0.0f);
            fresh->set (ParamIDs::outputLimiter, effects::Limiter::offDb);
            const auto output = fresh->runMono (sine (hz, 0.3f, 0.6));
            return dB ((levelAt (output, 2.0 * hz, 0.3) + levelAt (output, 3.0 * hz, 0.3)) / levelAt (output, hz, 0.3));
        };
        CHECK (harmonics (2000.0) > harmonics (110.0) + 15.0f);
    }

    SECTION ("Muff, Face and Classic each become a MARS style of their own kind")
    {
        CHECK (juce::roundToInt (value (*loadOld (2, 5.0f, 5.0f, 0.0f), ParamIDs::marsBand (0, "Style"))) == (int) effects::Mars::Style::americanAmp);
        CHECK (juce::roundToInt (value (*loadOld (3, 5.0f, 5.0f, 0.0f), ParamIDs::marsBand (0, "Style"))) == (int) effects::Mars::Style::brokenTube);
        CHECK (juce::roundToInt (value (*loadOld (0, 5.0f, 5.0f, 0.0f), ParamIDs::marsBand (0, "Style"))) == (int) effects::Mars::Style::saturatedTube);
    }

    SECTION ("A drive that was off and never touched leaves MARS as it starts")
    {
        auto rig = loadOld (1, 5.0f, 5.0f, 0.0f, false);
        CHECK (juce::roundToInt (value (*rig, ParamIDs::marsBands)) == 0);
        CHECK (juce::roundToInt (value (*rig, ParamIDs::marsBand (0, "Style"))) == (int) effects::Mars::Style::warmTape);
    }

    SECTION ("Saved with MARS already: as it was")
    {
        auto rig = loadOld (4, 9.0f, 0.0f, 6.0f);
        CHECK (juce::roundToInt (value (*rig, ParamIDs::marsBand (0, "Style"))) == (int) effects::Mars::Style::warmTape);
        CHECK_THAT (value (*rig, ParamIDs::marsBand (0, "Level")), WithinAbs (0.0, 0.01));
    }
}

TEST_CASE ("Projects saved with MODULATION on Doubler (before 0.3.0) load with the DOUBLER stomp on", "[effects][doubler]")
{
    // A project as 0.1.0 or 0.2.0 saved it: MODULATION on, TYPE 10 (Doubler),
    // its own RATE, DEPTH and MIX, and no parameterVersion.
    EffectRig original;
    juce::MemoryBlock saved;
    original.processor.getStateInformation (saved);
    auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
    REQUIRE (xml != nullptr);
    xml->removeAttribute ("parameterVersion");
    const std::pair<const char*, double> oldSettings[] = { { ParamIDs::modOn, 1.0 }, { ParamIDs::modType, 10.0 }, { ParamIDs::modRate, 1.5 },
                                                           { ParamIDs::modDepth, 0.3 }, { ParamIDs::modMix, 0.6 } };
    for (const auto& [id, value] : oldSettings)
    {
        auto* child = xml->getChildByAttribute ("id", id);
        REQUIRE (child != nullptr);
        child->setAttribute ("value", value);
    }
    juce::MemoryBlock older;
    juce::AudioProcessor::copyXmlToBinary (*xml, older);

    EffectRig rig;
    rig.processor.setStateInformation (older.getData(), (int) older.getSize());
    const auto value = [&rig] (const char* id) { return rig.processor.getParameters().getRawParameterValue (id)->load(); };
    CHECK (value (ParamIDs::doublerOn) > 0.5f);
    // Then on to 0.7.0's voices: the old takes, 19 and 27 ms late, one each
    // side, at MIX's level (60%: -4.4 dB), wobbling at a quarter of RATE.
    const auto voice = [&value] (int v, const char* part) { return value (ParamIDs::doublerVoice (v, part).toRawUTF8()); };
    CHECK (voice (0, "On") > 0.5f);
    CHECK (voice (1, "On") > 0.5f);
    CHECK (voice (2, "On") < 0.5f);
    CHECK_THAT (voice (0, "Delay"), WithinAbs (19.0, 0.05));
    CHECK_THAT (voice (1, "Delay"), WithinAbs (27.0, 0.05));
    CHECK (voice (0, "Pan") < -0.9f);
    CHECK (voice (1, "Pan") > 0.9f);
    CHECK_THAT (voice (0, "Gain"), WithinAbs (-4.4, 0.1));
    CHECK_THAT (voice (0, "Rate"), WithinAbs (0.375, 0.01));
    CHECK (std::abs (voice (0, "Depth")) > 0.5f);
    CHECK (value (ParamIDs::modOn) < 0.5f);   // MODULATION itself was only ever the doubler

    // And it sounds doubled: the two sides differ, as DOUBLER's takes do.
    const auto output = rig.run (sine (440.0, 0.3f, 1.0));
    std::vector<float> difference (output[0].size());
    for (size_t i = 0; i < difference.size(); ++i)
        difference[i] = output[0][i] - output[1][i];
    CHECK (rms (difference, 0.2) > 0.1f * rms (output[0], 0.2));

    // A project saved now isn't touched: a later MODULATION type numbered 10
    // mustn't be mistaken for the old Doubler.
    juce::MemoryBlock current;
    rig.processor.getStateInformation (current);
    auto currentXml = juce::AudioProcessor::getXmlFromBinary (current.getData(), (int) current.getSize());
    REQUIRE (currentXml != nullptr);
    CHECK (currentXml->getIntAttribute ("parameterVersion") == 36);
}

TEST_CASE ("LO-FI: telephone, radio, megaphone, cassette and bitcrusher", "[effects][lofi]")
{
    using LoFiType = effects::LoFi::Type;
    const auto makeRig = [] (LoFiType type, float amount = 5.0f)
    {
        auto rig = std::make_unique<EffectRig>();
        rig->set (ParamIDs::lofiOn, 1.0f);
        rig->set (ParamIDs::lofiType, (float) type);
        rig->set (ParamIDs::lofiAmount, amount);
        rig->set (ParamIDs::lofiNoise, 0.0f);   // measure the tone without hiss
        return rig;
    };

    SECTION ("Telephone lets only the voice band through")
    {
        auto rig = makeRig (LoFiType::telephone);
        const auto gainAt = [&] (double hz)
        {
            const auto input = sine (hz, 0.05f, 0.5);
            return dB (rms (rig->runMono (input), 0.2) / rms (input, 0.2));
        };
        const auto mid = gainAt (1000.0);
        CHECK (mid - gainAt (100.0) > 25.0f);
        CHECK (mid - gainAt (8000.0) > 20.0f);
        CHECK_THAT (mid, WithinAbs (0.0, 6.0));   // and the voice band is about as loud as before
    }

    SECTION ("The speaker types are mono, the way a phone or radio is")
    {
        for (const auto type : { LoFiType::telephone, LoFiType::amRadio, LoFiType::megaphone })
        {
            auto rig = makeRig (type);
            rig->set (ParamIDs::modOn, 1.0f);   // chorus first, so the input to LO-FI is stereo
            const auto output = rig->run (sine (800.0, 0.3f, 0.6));
            INFO (effects::LoFi::typeNames[(int) type]);
            REQUIRE (allFinite (output[0]));
            CHECK (maxAbsDifference (output[0], output[1], (size_t) (0.1 * sampleRate)) < 1.0e-6f);   // after its 20 ms fade-in
        }
    }

    SECTION ("Megaphone is driven harder than the telephone: more harmonics")
    {
        const auto thirdHarmonic = [&] (LoFiType type)
        {
            auto rig = makeRig (type, 8.0f);
            const auto output = rig->runMono (sine (500.0, 0.3f, 0.5));
            return dB (levelAt (output, 1500.0) / levelAt (output, 500.0));
        };
        CHECK (thirdHarmonic (LoFiType::megaphone) > thirdHarmonic (LoFiType::telephone) + 6.0f);
    }

    SECTION ("Cassette wobbles the pitch")
    {
        // A steady 1 kHz tone comes out smeared across neighbouring frequencies.
        auto rig = makeRig (LoFiType::cassette, 10.0f);
        const auto output = rig->runMono (sine (1000.0, 0.3f, 2.0));
        REQUIRE (allFinite (output));
        auto clean = makeRig (LoFiType::cassette, 0.0f);
        const auto steady = clean->runMono (sine (1000.0, 0.3f, 2.0));
        CHECK (levelAt (output, 1000.0, 0.3) < 0.7f * levelAt (steady, 1000.0, 0.3));
    }

    SECTION ("Bitcrusher at full AMOUNT leaves only a few levels and holds samples")
    {
        auto rig = makeRig (LoFiType::bitcrusher, 10.0f);
        const auto output = rig->runMono (sine (220.0, 0.8f, 1.0));
        std::set<int> levels;
        int repeats = 0;
        for (size_t i = 24000; i < output.size(); ++i)
        {
            levels.insert (juce::roundToInt (output[i] * 10000.0f));
            repeats += juce::approximatelyEqual (output[i], output[i - 1]) ? 1 : 0;
        }
        CHECK (levels.size() <= 9);   // 3 bits: up to 9 levels from -1 to +1
        CHECK (repeats > (int) (0.8 * (double) (output.size() - 24000)));   // each sample held for many
    }

    SECTION ("NOISE adds hiss")
    {
        auto rig = makeRig (LoFiType::telephone);
        rig->set (ParamIDs::lofiNoise, 10.0f);
        const auto output = rig->runMono (silence (0.5));
        REQUIRE (allFinite (output));
        CHECK (dB (rms (output, 0.1)) > -60.0f);
        CHECK (dB (rms (output, 0.1)) < -25.0f);
    }
}

TEST_CASE ("Modulation syncs to the tempo", "[effects][mod]")
{
    // Tremolo synced to quarter notes at the DAW's 120 BPM is two pulses a
    // second, whatever RATE says: the same as RATE at 2 Hz unsynced.
    const auto input = sine (440.0, 0.3f, 1.0);
    const auto tremolo = [&] (float rate, float sync)
    {
        EffectRig rig;
        rig.set (ParamIDs::modOn, 1.0f);
        rig.set (ParamIDs::modType, typeIndex (ModType::tremolo));
        rig.set (ParamIDs::modDepth, 1.0f);
        rig.set (ParamIDs::modRate, rate);
        rig.set (ParamIDs::modSync, sync);
        return rig.runMono (input);
    };

    const auto synced = tremolo (7.0f, 3.0f);   // sync 3 = "1/4"
    const auto free = tremolo (2.0f, 0.0f);
    CHECK (maxAbsDifference (synced, free, 0) < 0.01f);
    CHECK (maxAbsDifference (tremolo (7.0f, 0.0f), free, (size_t) (0.2 * sampleRate)) > 0.05f);   // and RATE alone does matter
}

TEST_CASE ("Delay repeats at the set time", "[effects][delay]")
{
    EffectRig rig;
    rig.set (ParamIDs::delayOn, 1.0f);
    rig.set (ParamIDs::delayMix, 1.0f);
    rig.set (ParamIDs::delayFeedback, 0.5f);

    const auto peakNear = [] (const std::vector<float>& samples, size_t at, size_t window = 64)
    {
        float peak = 0.0f;
        for (auto i = at - window; i <= at + window; ++i)
            peak = std::max (peak, std::abs (samples[i]));
        return peak;
    };

    SECTION ("Free time in milliseconds")
    {
        rig.set (ParamIDs::delaySync, 0.0f);   // Off
        rig.set (ParamIDs::delayTime, 100.0f);
        const auto output = rig.runMono (impulse (0.5, 0.5f));

        // The first echo is an exact copy 100 ms (4800 samples) later.
        CHECK_THAT (output[4800], WithinAbs (0.5, 0.01));
        CHECK (peakNear (output, 2400) < 0.001f);
        // The second has half the energy (feedback 50%). The tone filter in
        // the loop softens it (darker repeats), which spreads it over a few
        // samples, so add those up rather than looking at the peak.
        float secondEcho = 0.0f;
        for (size_t i = 9600 - 8; i < 9600 + 64; ++i)
            secondEcho += output[i];
        CHECK_THAT (secondEcho, WithinAbs (0.25, 0.01));
    }

    SECTION ("Synced to the DAW tempo: 1/4 note at 120 BPM is 500 ms")
    {
        rig.set (ParamIDs::delaySync, 3.0f);   // 1/4
        rig.playHead.bpm = 120.0;
        const auto output = rig.runMono (impulse (0.7, 0.5f));
        CHECK_THAT (output[24000], WithinAbs (0.5, 0.01));

        // Dotted eighth at 90 BPM: 0.75 beats * 60/90 s = 500 ms as well.
        EffectRig other;
        other.set (ParamIDs::delayOn, 1.0f);
        other.set (ParamIDs::delayMix, 1.0f);
        other.set (ParamIDs::delaySync, 7.0f);   // 1/8 dotted
        other.playHead.bpm = 90.0;
        CHECK_THAT (other.runMono (impulse (0.7, 0.5f))[24000], WithinAbs (0.5, 0.01));
    }

    SECTION ("Ping-pong alternates the repeats between left and right")
    {
        rig.set (ParamIDs::delaySync, 0.0f);
        rig.set (ParamIDs::delayTime, 100.0f);
        rig.set (ParamIDs::delayPingPong, 1.0f);
        const auto output = rig.run (impulse (0.5, 0.5f));
        CHECK (peakNear (output[0], 4800) > 0.1f);    // 1st echo: left
        CHECK (peakNear (output[1], 4800) < 0.001f);
        CHECK (peakNear (output[1], 9600) > 0.05f);   // 2nd echo: right
        CHECK (peakNear (output[0], 9600) < 0.001f);
    }
}

TEST_CASE ("Delay engine on its own: one echo, at the set time, at full level", "[effects][delay]")
{
    // The DELAY effect without the rest of the plugin, so a failure here
    // (rather than only in the plugin tests) points at the engine itself.
    effects::Delay delay;
    delay.prepare (sampleRate, blockSize);
    effects::Delay::Settings settings;
    settings.on = true;
    settings.syncIndex = 0;
    settings.timeMs = 100.0f;
    settings.feedback = 0.0f;
    settings.mix = 1.0f;
    CAPTURE (effects::Delay::delaySeconds (settings, 120.0), effects::Delay::delaySecondsRight (settings, 120.0));

    juce::AudioBuffer<float> buffer (2, (int) (0.3 * sampleRate));
    buffer.clear();
    buffer.setSample (0, 0, 0.5f);
    buffer.setSample (1, 0, 0.5f);
    for (int start = 0; start < buffer.getNumSamples(); start += blockSize)
    {
        const auto count = juce::jmin (blockSize, buffer.getNumSamples() - start);
        delay.process (juce::dsp::AudioBlock<float> (buffer).getSubBlock ((size_t) start, (size_t) count), settings, 120.0);
    }
    // Where the echo came, and how loud.
    int loudest = 1;
    for (int i = 1; i < buffer.getNumSamples(); ++i)
        if (std::abs (buffer.getSample (0, i)) > std::abs (buffer.getSample (0, loudest)))
            loudest = i;
    // Where the left side's first sound after the click is.
    int firstLeft = -1;
    for (int i = 1; i < buffer.getNumSamples() && firstLeft < 0; ++i)
        if (std::abs (buffer.getSample (0, i)) > 1.0e-6f)
            firstLeft = i;
    CAPTURE (loudest, buffer.getSample (0, loudest), buffer.getSample (1, 4800), buffer.getSample (0, 0), firstLeft);

    // JUCE's delay line by itself, at the old (2.5 s) and new (3.5 s) sizes.
    for (const auto seconds : { 2.51, 3.51 })
    {
        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> line { 4 };
        line.setMaximumDelayInSamples ((int) std::ceil (seconds * sampleRate) + 4);
        line.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
        std::array<int, 2> arrived { -1, -1 };
        for (int i = 0; i < 6000; ++i)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto out = line.popSample (ch, 4800.0f);
                if (arrived[(size_t) ch] < 0 && std::abs (out) > 0.25f)
                    arrived[(size_t) ch] = i;
            }
            for (int ch = 0; ch < 2; ++ch)
                line.pushSample (ch, i == 0 ? 0.5f : 0.0f);
        }
        CAPTURE (seconds, arrived[0], arrived[1]);
        CHECK (arrived[0] == 4800);
        CHECK (arrived[1] == 4800);
    }

    // The engine on a one-channel block.
    {
        effects::Delay monoDelay;
        monoDelay.prepare (sampleRate, blockSize);
        juce::AudioBuffer<float> monoBuffer (1, (int) (0.3 * sampleRate));
        monoBuffer.clear();
        monoBuffer.setSample (0, 0, 0.5f);
        for (int start = 0; start < monoBuffer.getNumSamples(); start += blockSize)
        {
            const auto count = juce::jmin (blockSize, monoBuffer.getNumSamples() - start);
            monoDelay.process (juce::dsp::AudioBlock<float> (monoBuffer).getSubBlock ((size_t) start, (size_t) count), settings, 120.0);
        }
        CAPTURE (monoBuffer.getSample (0, 4800));
        CHECK_THAT (monoBuffer.getSample (0, 4800), WithinAbs (0.5, 0.01));
    }
    CHECK (loudest == 4800);
    CHECK_THAT (buffer.getSample (0, 4800), WithinAbs (0.5, 0.01));
    CHECK_THAT (buffer.getSample (1, 4800), WithinAbs (0.5, 0.01));
}

TEST_CASE ("Delay: the controls inspired by H-Delay do what they say", "[effects][delay]")
{
    const auto delayRig = [] (EffectRig& rig, float timeMs = 100.0f, float feedback = 0.0f)
    {
        rig.set (ParamIDs::delayOn, 1.0f);
        rig.set (ParamIDs::delayMix, 1.0f);
        rig.set (ParamIDs::delaySync, 0.0f);
        rig.set (ParamIDs::delayTime, timeMs);
        rig.set (ParamIDs::delayFeedback, feedback);
        rig.set (ParamIDs::delayTone, 10.0f);
    };
    const auto peakNear = [] (const std::vector<float>& samples, size_t at, size_t window = 64)
    {
        float peak = 0.0f;
        for (auto i = at - window; i <= at + window; ++i)
            peak = std::max (peak, std::abs (samples[i]));
        return peak;
    };
    // The echoes alone: what comes out, less your own voice (which passes straight through).
    const auto echoesOf = [] (EffectRig& rig, const std::vector<float>& input)
    {
        auto output = rig.runMono (input);
        for (size_t i = 0; i < output.size(); ++i)
            output[i] -= input[i];
        return output;
    };
    // Where the strongest sample is, after the click itself.
    const auto loudestAt = [] (const std::vector<float>& samples, size_t from = 100)
    {
        size_t at = from;
        for (auto i = from; i < samples.size(); ++i)
            if (std::abs (samples[i]) > std::abs (samples[at]))
                at = i;
        return at;
    };

    SECTION ("DUAL: the right side has its own time")
    {
        EffectRig rig;
        delayRig (rig, 100.0f);
        rig.set (ParamIDs::delayDual, 1.0f);
        rig.set (ParamIDs::delaySyncRight, 0.0f);
        rig.set (ParamIDs::delayTimeRight, 150.0f);
        const auto output = rig.run (impulse (0.3, 0.5f));
        CHECK (loudestAt (output[0]) == 4800);   // 100 ms
        CHECK (loudestAt (output[1]) == 7200);   // 150 ms
    }

    SECTION ("Ping-pong's START: the first echo on the right")
    {
        EffectRig rig;
        delayRig (rig, 100.0f, 0.5f);
        rig.set (ParamIDs::delayPingPong, 1.0f);
        rig.set (ParamIDs::delayPingStart, 1.0f);
        const auto output = rig.run (impulse (0.3, 0.5f));
        CHECK (peakNear (output[1], 4800) > 0.1f);
        CHECK (peakNear (output[0], 4800) < 0.001f);
        CHECK (peakNear (output[0], 9600) > 0.05f);
    }

    SECTION ("PHASE flips one side's echoes")
    {
        EffectRig rig;
        delayRig (rig, 100.0f);
        rig.set (ParamIDs::delayPhaseLeft, 1.0f);
        const auto output = rig.run (impulse (0.2, 0.5f));
        CHECK_THAT (output[0][4800], WithinAbs (-0.5, 0.01));
        CHECK_THAT (output[1][4800], WithinAbs (0.5, 0.01));
    }

    SECTION ("FEEDBACK past 100% builds the repeats up, and holds them there")
    {
        EffectRig rig;
        delayRig (rig, 100.0f, 1.5f);
        const auto output = rig.runMono (impulse (3.0, 0.3f));
        REQUIRE (allFinite (output));
        const auto first = peakNear (output, 4800), later = peakNear (output, 4800 * 20);
        CHECK (later > 2.0f * first);   // louder than the first echo, not quieter
        auto peak = 0.0f;
        for (const auto x : output)
            peak = std::max (peak, std::abs (x));
        CHECK (peak < 1.5f);            // but held, not running away
    }

    SECTION ("LOW CUT and HIGH CUT filter the echoes")
    {
        const auto echoLevel = [&] (double hz, float lowCut, float highCut)
        {
            EffectRig rig;
            delayRig (rig, 50.0f);
            rig.set (ParamIDs::delayLowCut, lowCut);
            rig.set (ParamIDs::delayHighCut, highCut);
            return levelAt (echoesOf (rig, sine (hz, 0.2f, 0.6)), hz, 0.3);
        };
        const auto off = effects::Delay::cutOffLowHz, top = effects::Delay::cutOffHighHz;
        // 12 dB per octave: over 3 octaves from where they're set, about 36 dB down.
        CHECK (dB (echoLevel (100.0, 1000.0f, top) / echoLevel (100.0, off, top)) < -30.0f);
        CHECK (dB (echoLevel (8000.0, off, 1000.0f) / echoLevel (8000.0, off, top)) < -30.0f);
        // Between them, untouched.
        CHECK_THAT (dB (echoLevel (1000.0, 100.0f, 10000.0f) / echoLevel (1000.0, off, top)), WithinAbs (0.0, 0.5));
    }

    SECTION ("MOD sweeps the echo's time")
    {
        // Clicks every 0.25 s: with MOD at full, their echoes come at different times.
        const auto spread = [&] (float depth)
        {
            EffectRig rig;
            delayRig (rig, 50.0f);
            rig.set (ParamIDs::delayModDepth, depth);
            rig.set (ParamIDs::delayModRate, 0.7f);
            auto input = silence (2.6);
            for (int click = 0; click < 10; ++click)
                input[(size_t) ((0.1 + 0.25 * click) * sampleRate)] = 0.5f;
            const auto output = rig.runMono (input);
            double low = 1.0e9, high = -1.0e9;
            for (int click = 0; click < 10; ++click)
            {
                const auto at = (size_t) ((0.1 + 0.25 * click) * sampleRate);
                size_t loudest = at + 10;
                for (auto i = at + 10; i < at + (size_t) (0.1 * sampleRate); ++i)
                    if (std::abs (output[i]) > std::abs (output[loudest]))
                        loudest = i;
                const auto ms = (double) (loudest - at) / sampleRate * 1000.0;
                low = std::min (low, ms);
                high = std::max (high, ms);
            }
            return high - low;
        };
        CHECK (spread (0.0f) < 0.1);
        CHECK (spread (1.0f) > 4.0);    // up to 4 ms either way
        CHECK (spread (1.0f) < 8.5);
    }

    SECTION ("ANALOG adds warmth (harmonics) to the echoes; Off adds none")
    {
        const auto harmonics = [&] (float analog)
        {
            EffectRig rig;
            delayRig (rig, 50.0f);
            rig.set (ParamIDs::delayAnalog, analog);
            rig.set (ParamIDs::outputLimiter, effects::Limiter::offDb);   // so only ANALOG adds harmonics
            const auto output = rig.runMono (sine (440.0, 0.4f, 0.6));
            return dB ((levelAt (output, 880.0, 0.3) + levelAt (output, 1320.0, 0.3)) / levelAt (output, 440.0, 0.3));
        };
        CHECK (harmonics (0.0f) < -80.0f);
        CHECK (harmonics (1.0f) > -60.0f);
        CHECK (harmonics (4.0f) > harmonics (1.0f) + 10.0f);   // 4, Grit, much more than 1, Warm
    }

    SECTION ("LO-FI narrows the echoes' band")
    {
        const auto highs = [&] (bool loFi)
        {
            EffectRig rig;
            delayRig (rig, 50.0f);
            rig.set (ParamIDs::delayLoFi, loFi ? 1.0f : 0.0f);
            return levelAt (echoesOf (rig, sine (12000.0, 0.2f, 0.6)), 12000.0, 0.3);
        };
        CHECK (dB (highs (true) / highs (false)) < -10.0f);   // an early digital delay's narrow band
    }

    SECTION ("Up to 3.5 seconds")
    {
        EffectRig rig;
        delayRig (rig, 3400.0f);
        CHECK (loudestAt (rig.runMono (impulse (3.6, 0.5f))) == (size_t) (3.4 * sampleRate));
    }
}

TEST_CASE ("Switching delay off lets the echoes ring out but stops new ones", "[effects][delay]")
{
    EffectRig rig;
    rig.set (ParamIDs::delayOn, 1.0f);
    rig.set (ParamIDs::delayMix, 1.0f);
    rig.set (ParamIDs::delayFeedback, 0.0f);
    rig.set (ParamIDs::delaySync, 0.0f);
    rig.set (ParamIDs::delayTime, 200.0f);

    // Play a click, then switch the delay off before its echo arrives.
    auto first = rig.runMono (impulse (0.1, 0.5f));
    rig.set (ParamIDs::delayOn, 0.0f);
    auto rest = rig.runMono (silence (0.2));
    first.insert (first.end(), rest.begin(), rest.end());
    CHECK (std::abs (first[9600]) > 0.4f);   // the echo still came, 200 ms later

    // A click played now, with the delay off, gets no echo.
    const auto later = rig.runMono (impulse (0.5, 0.5f));
    CHECK (std::abs (later[9600]) < 1.0e-4f);
}

TEST_CASE ("Delay types each do what they say", "[effects][delay]")
{
    using DelayType = effects::Delay::Type;
    const auto makeRig = [] (DelayType type, float timeMs, float feedback)
    {
        auto rig = std::make_unique<EffectRig>();
        rig->set (ParamIDs::delayOn, 1.0f);
        rig->set (ParamIDs::delayType, (float) type);
        rig->set (ParamIDs::delaySync, 0.0f);
        rig->set (ParamIDs::delayTime, timeMs);
        rig->set (ParamIDs::delayFeedback, feedback);
        rig->set (ParamIDs::delayMix, 1.0f);
        return rig;
    };
    const auto minus = [] (const std::vector<float>& a, const std::vector<float>& b)
    {
        std::vector<float> difference (a.size());
        for (size_t i = 0; i < a.size(); ++i)
            difference[i] = a[i] - b[i];
        return difference;
    };

    SECTION ("Every type stays stable with FEEDBACK all the way up, and its echoes stop when switched off")
    {
        for (int type = 0; type < effects::Delay::typeNames.size(); ++type)
        {
            INFO (effects::Delay::typeNames[type]);
            auto rig = makeRig ((DelayType) type, 300.0f, 0.95f);
            auto burst = sine (220.0, 0.5f, 0.25);   // a played phrase, then letting it ring
            burst.resize ((size_t) (4.0 * sampleRate), 0.0f);
            const auto output = rig->run (burst);
            REQUIRE (allFinite (output[0]));
            REQUIRE (allFinite (output[1]));
            float peak = 0.0f;
            for (auto sample : output[0])
                peak = std::max (peak, std::abs (sample));
            CHECK (peak < 2.0f);   // it can build, but it never runs away
        }
    }

    SECTION ("Analog's repeats are darker than digital's")
    {
        juce::Random random (3);
        std::vector<float> noise ((size_t) (0.05 * sampleRate));
        for (auto& sample : noise)
            sample = random.nextFloat() * 0.6f - 0.3f;
        noise.resize ((size_t) (0.6 * sampleRate), 0.0f);
        const auto highsInEcho = [&] (DelayType type)
        {
            auto output = makeRig (type, 250.0f, 0.0f)->runMono (noise);
            juce::dsp::IIR::Filter<float> highPass;
            highPass.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, 5000.0f);
            for (auto& sample : output)
                sample = highPass.processSample (sample);
            return rms (output, 0.25, 0.32);
        };
        CHECK (highsInEcho (DelayType::analog) < 0.3f * highsInEcho (DelayType::digital));
    }

    SECTION ("Tape's wow and flutter smear a steady note's pitch")
    {
        const auto input = sine (1000.0, 0.3f, 2.0);
        const auto echoAt1k = [&] (DelayType type)
        {
            auto rig = makeRig (type, 300.0f, 0.0f);
            const auto output = rig->runMono (input);
            return levelAt (minus (output, input), 1000.0, 0.4);
        };
        CHECK (echoAt1k (DelayType::tape) < 0.8f * echoAt1k (DelayType::digital));
    }

    SECTION ("Mod's chorused repeats differ left and right")
    {
        const auto input = sine (440.0, 0.3f, 1.5);
        const auto output = makeRig (DelayType::mod, 300.0f, 0.3f)->run (input);
        CHECK (rms (minus (output[0], output[1]), 0.4) > 0.1f * rms (minus (output[0], input), 0.4));
    }

    SECTION ("Multi-tap repeats at a quarter, half, three quarters and the full time")
    {
        const auto output = makeRig (DelayType::multiTap, 400.0f, 0.0f)->runMono (impulse (0.6, 0.5f));
        for (const auto ms : { 100, 200, 300, 400 })
        {
            INFO (ms << " ms");
            float peak = 0.0f;
            for (int i = -8; i <= 8; ++i)
                peak = std::max (peak, std::abs (output[(size_t) (ms * 48 + i)]));
            CHECK (peak > 0.1f);
        }
        CHECK (rms (output, 0.12, 0.18) < 1.0e-4f);   // and nothing in between
    }

    SECTION ("Reverse plays each repeat backwards: a note's echo swells up instead of dying away")
    {
        // A plucked note: sharp attack, then a fade.
        std::vector<float> note ((size_t) (1.5 * sampleRate));
        for (size_t i = 0; i < (size_t) (0.4 * sampleRate); ++i)
            note[i] = 0.4f * (float) (std::sin (juce::MathConstants<double>::twoPi * 330.0 * (double) i / sampleRate)
                                      * std::exp (-(double) i / (0.08 * sampleRate)));
        const auto reversed = minus (makeRig (DelayType::reverse, 400.0f, 0.0f)->runMono (note), note);
        const auto forwards = minus (makeRig (DelayType::digital, 400.0f, 0.0f)->runMono (note), note);
        // In the second chunk (0.4-0.8 s): digital's echo is loud at the
        // start and fades; reverse's is quiet at the start and loudest late.
        CHECK (rms (forwards, 0.40, 0.48) > 3.0f * rms (forwards, 0.62, 0.7));
        CHECK (rms (reversed, 0.62, 0.78) > 3.0f * rms (reversed, 0.40, 0.48));
    }

    SECTION ("Shimmer's repeats climb an octave")
    {
        const auto input = [&]
        {
            auto burst = sine (330.0, 0.4f, 0.2);
            burst.resize ((size_t) (2.0 * sampleRate), 0.0f);
            return burst;
        }();
        const auto octaveUp = [&] (DelayType type)
        {
            const auto output = makeRig (type, 250.0f, 0.6f)->runMono (input);
            return levelAt (output, 660.0, 0.5) / (levelAt (output, 330.0, 0.5) + 1.0e-9f);
        };
        CHECK (octaveUp (DelayType::shimmer) > 0.3f);
        CHECK (octaveUp (DelayType::digital) < 0.05f);
    }

    SECTION ("Ducking keeps the repeats down while you play and lets them back in when you stop")
    {
        auto input = sine (220.0, 0.3f, 1.0);
        input.resize ((size_t) (2.0 * sampleRate), 0.0f);
        const auto ducked = minus (makeRig (DelayType::ducking, 250.0f, 0.5f)->runMono (input), input);
        const auto plain = minus (makeRig (DelayType::digital, 250.0f, 0.5f)->runMono (input), input);
        CHECK (rms (ducked, 0.5, 0.95) < 0.25f * rms (plain, 0.5, 0.95));    // at least 12 dB down while playing
        CHECK (rms (ducked, 1.5, 1.8) > 0.5f * rms (plain, 1.5, 1.8));      // and back once you stop
    }
}

TEST_CASE ("Reverb adds a stereo tail that fades out", "[effects][reverb]")
{
    EffectRig rig;
    rig.set (ParamIDs::reverbOn, 1.0f);
    rig.set (ParamIDs::reverbMix, 0.5f);
    const auto output = rig.run (impulse (3.0, 0.5f));

    REQUIRE (allFinite (output[0]));
    const auto early = rms (output[0], 0.05, 0.3);
    const auto late = rms (output[0], 2.5, 3.0);
    CHECK (early > 1.0e-3f);   // there is a tail
    CHECK (late < early * 0.1f);   // and it decays

    std::vector<float> difference (output[0].size());
    for (size_t i = 0; i < difference.size(); ++i)
        difference[i] = output[0][i] - output[1][i];
    CHECK (rms (difference, 0.05, 0.3) > 0.1f * early);   // left and right differ: it's wide
}

TEST_CASE ("Presence boosts the top end and leaves the lows alone", "[effects][presence]")
{
    EffectRig rig;
    rig.set (ParamIDs::presence, 10.0f);
    const auto highs = sine (9000.0, 0.1f, 0.5);
    const auto lows = sine (150.0, 0.1f, 0.5);
    CHECK (dB (rms (rig.runMono (highs), 0.2) / rms (highs, 0.2)) > 8.0f);
    CHECK_THAT (dB (rms (rig.runMono (lows), 0.2) / rms (lows, 0.2)), WithinAbs (0.0, 0.5));
}

TEST_CASE ("Turning effects on and off doesn't click, and off really is off", "[effects]")
{
    for (const auto* id : { ParamIDs::compOn, ParamIDs::driveOn, ParamIDs::modOn, ParamIDs::delayOn, ParamIDs::reverbOn })
    {
        DYNAMIC_SECTION (id)
        {
            EffectRig rig;
            const auto input = sine (220.0, 0.3f, 0.3);
            rig.set (id, 1.0f);
            rig.runMono (input);

            // Switching off mid-note: no sample may jump further than the
            // biggest step the effect makes on its own.
            const auto on = rig.runMono (input);
            rig.set (id, 0.0f);
            const auto switching = rig.runMono (sine (220.0, 0.3f, 0.05));
            float normalStep = 0.0f, switchStep = 0.0f;
            for (size_t i = 1; i < on.size(); ++i)
                normalStep = std::max (normalStep, std::abs (on[i] - on[i - 1]));
            for (size_t i = 1; i < switching.size(); ++i)
                switchStep = std::max (switchStep, std::abs (switching[i] - switching[i - 1]));
            CHECK (switchStep < std::max (normalStep, 0.03f) * 1.5f);

            // Once the fade (and any tail) is over, the output is the clean
            // input. A delay with feedback takes several seconds to die away.
            rig.runMono (silence (12.0));
            const auto after = rig.runMono (input);
            CHECK (maxAbsDifference (input, after) < 1.0e-4f);
        }
    }
}

TEST_CASE ("Every effect copes with a mono track and with oversized blocks", "[effects]")
{
    const auto turnEverythingOn = [] (EffectRig& rig)
    {
        for (const auto* id : { ParamIDs::compOn, ParamIDs::driveOn, ParamIDs::modOn, ParamIDs::delayOn, ParamIDs::reverbOn })
            rig.set (id, 1.0f);
        rig.set (ParamIDs::delayPingPong, 1.0f);
        rig.set (ParamIDs::presence, 5.0f);
    };

    SECTION ("Mono output")
    {
        EffectRig rig (1);
        turnEverythingOn (rig);
        const auto output = rig.runMono (sine (220.0, 0.3f, 0.5));
        REQUIRE (allFinite (output));
        CHECK (rms (output, 0.2) > 0.01f);
    }

    SECTION ("A host sending 3x the promised block size")
    {
        EffectRig rig;
        turnEverythingOn (rig);
        juce::AudioBuffer<float> big (2, blockSize * 3);
        juce::MidiBuffer midi;
        for (int repeat = 0; repeat < 20; ++repeat)
        {
            big.clear();
            const auto input = sine (220.0, 0.3f, (double) big.getNumSamples() / sampleRate);
            std::copy (input.begin(), input.end(), big.getWritePointer (0));
            rig.processor.processBlock (big, midi);
        }
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < big.getNumSamples(); ++i)
                REQUIRE (std::isfinite (big.getSample (ch, i)));
    }
}

TEST_CASE ("Full chain runs comfortably faster than real time", "[effects][performance]")
{
    EffectRig rig;
    rig.set (ParamIDs::inputLowCut, 80.0f);
    for (const auto* id : { ParamIDs::compOn, ParamIDs::fpOn, ParamIDs::driveOn, ParamIDs::eqOn, ParamIDs::doublerOn, ParamIDs::modOn,
                            ParamIDs::delayOn, ParamIDs::reverbOn })
        rig.set (id, 1.0f);
    // The costliest settings: the pitch shifter two octaves up (it jumps most often) and the rotary.
    rig.set (ParamIDs::fpType, (float) effects::FilterPitch::Type::pitchShifter);
    rig.set (ParamIDs::fpInterval, 24.0f);
    rig.set (ParamIDs::modType, (float) effects::Modulation::Type::rotary);
    // HARMONY with all eight voices, the highest two octaves up (each laid
    // down from the most overlapping slices).
    rig.set (ParamIDs::harmonyOn, 1.0f);
    const float intervals[] = { 4, 7, 12, -12, 16, 19, 24, -5 };
    for (int voice = 0; voice < effects::Harmony::numVoices; ++voice)
    {
        rig.set (ParamIDs::harmonyVoice (voice, "Active").toRawUTF8(), 1.0f);
        rig.set (ParamIDs::harmonyVoice (voice, "Pitch").toRawUTF8(), intervals[voice]);
    }

    const auto input = sine (220.0, 0.3f, 5.0);
    const auto startMs = juce::Time::getMillisecondCounterHiRes();
    rig.runMono (input);
    const auto elapsedSeconds = (juce::Time::getMillisecondCounterHiRes() - startMs) / 1000.0;
    const auto realtimeFraction = elapsedSeconds / 5.0;

    WARN ("Full chain uses " << juce::String (realtimeFraction * 100.0, 1) << "% of one core in real time");
    CHECK (realtimeFraction < 0.5);
}

TEST_CASE ("MARS at its heaviest still runs comfortably faster than real time", "[effects][performance][mars]")
{
    // Four bands of the costliest styles, with DYNAMICS and FEEDBACK on, at each QUALITY.
    for (const auto quality : { effects::Mars::Quality::off, effects::Mars::Quality::good, effects::Mars::Quality::superb })
    {
        MarsRig rig;
        rig.set (ParamIDs::marsBands, 3.0f);
        rig.set (ParamIDs::marsQuality, (float) quality);
        const effects::Mars::Style styles[] = { effects::Mars::Style::breakdown, effects::Mars::Style::smudge,
                                                effects::Mars::Style::screamingAmp, effects::Mars::Style::extremeTape };
        for (int b = 0; b < 4; ++b)
        {
            rig.band (b, "Style", (float) styles[b]);
            rig.band (b, "Drive", 0.8f);
            rig.band (b, "Dynamics", 0.5f);
            rig.band (b, "Feedback", 0.3f);
            rig.band (b, "Bass", 3.0f);
        }
        const auto input = sine (220.0, 0.3f, 4.0);
        const auto startMs = juce::Time::getMillisecondCounterHiRes();
        rig.runMono (input);
        const auto fraction = (juce::Time::getMillisecondCounterHiRes() - startMs) / 1000.0 / 4.0;
        WARN ("MARS, 4 bands, " << effects::Mars::qualityNames[(int) quality] << ": " << juce::String (fraction * 100.0, 1) << "% of one core");
        CHECK (fraction < (quality == effects::Mars::Quality::superb ? 0.4 : 0.25));
    }
}

TEST_CASE ("Graphic EQ: flat is transparent, each band moves only its octave", "[effects][eq]")
{
    // The peak level of a steady tone after the plugin, with the EQ set by `setUp`.
    auto levelOf = [] (float frequency, const std::function<void (EffectRig&)>& setUp)
    {
        EffectRig rig;
        setUp (rig);
        std::vector<float> tone ((size_t) (sampleRate * 0.5));
        for (size_t i = 0; i < tone.size(); ++i)
            tone[i] = 0.1f * (float) std::sin (juce::MathConstants<double>::twoPi * frequency * (double) i / sampleRate);
        const auto out = rig.run (tone)[0];
        return *std::max_element (out.end() - 4800, out.end());
    };
    const auto off = [] (EffectRig&) {};
    const auto toDb = [] (float ratio) { return juce::Decibels::gainToDecibels (ratio); };

    for (auto frequency : { 63.0f, 500.0f, 1000.0f, 4000.0f })
    {
        // Switched on but flat: no change at all.
        const auto flat = levelOf (frequency, [] (EffectRig& rig) { rig.set (ParamIDs::eqOn, 1.0f); });
        CHECK_THAT (toDb (flat / levelOf (frequency, off)), WithinAbs (0.0, 0.05));
    }

    // +12 dB at 1 kHz: a 1 kHz tone comes up by about 12 dB; far-off octaves barely
    // move (a band's skirt spills a little, as on a hardware graphic EQ: under
    // 1 dB three octaves away, under 0.5 dB four away).
    const auto boost1k = [] (EffectRig& rig) { rig.set (ParamIDs::eqOn, 1.0f); rig.set ("eq1k", 12.0f); };
    CHECK_THAT (toDb (levelOf (1000.0f, boost1k) / levelOf (1000.0f, off)), WithinAbs (12.0, 0.5));
    CHECK (std::abs (toDb (levelOf (63.0f, boost1k) / levelOf (63.0f, off))) < 0.5f);
    CHECK (std::abs (toDb (levelOf (8000.0f, boost1k) / levelOf (8000.0f, off))) < 1.0f);

    // Cuts work the same way, and LEVEL moves everything.
    const auto cut125 = [] (EffectRig& rig) { rig.set (ParamIDs::eqOn, 1.0f); rig.set ("eq125", -12.0f); };
    CHECK_THAT (toDb (levelOf (125.0f, cut125) / levelOf (125.0f, off)), WithinAbs (-12.0, 0.5));
    const auto louder = [] (EffectRig& rig) { rig.set (ParamIDs::eqOn, 1.0f); rig.set (ParamIDs::eqLevel, 6.0f); };
    CHECK_THAT (toDb (levelOf (500.0f, louder) / levelOf (500.0f, off)), WithinAbs (6.0, 0.1));

    // Off means off, whatever the faders say.
    const auto bypassed = [] (EffectRig& rig) { rig.set ("eq1k", 12.0f); };
    CHECK_THAT (toDb (levelOf (1000.0f, bypassed) / levelOf (1000.0f, off)), WithinAbs (0.0, 0.01));
}

TEST_CASE ("Graphic EQ's drawn curve matches what it does", "[effects][eq]")
{
    std::array<float, effects::GraphicEq::numBands> gains {};
    CHECK_THAT (effects::GraphicEq::responseDb (gains, 1000.0f), WithinAbs (0.0, 1.0e-6));
    gains[4] = 12.0f;   // 1 kHz
    CHECK_THAT (effects::GraphicEq::responseDb (gains, 1000.0f), WithinAbs (12.0, 0.05));
    CHECK (std::abs (effects::GraphicEq::responseDb (gains, 63.0f)) < 0.5f);
    // A band above half a low sample rate is pulled down rather than breaking.
    CHECK (effects::GraphicEq::bandFrequency (8, 22050.0) < 11025.0f);
}

TEST_CASE ("EQ presets: in range, named once each, and loudness-matched by LEVEL", "[effects][eq]")
{
    std::set<std::string> names;
    for (const auto& preset : EqPresets::all)
    {
        INFO (preset.name);
        CHECK (names.insert (preset.name).second);
        for (const auto gain : preset.gainsDb)
            CHECK (std::abs (gain) <= effects::GraphicEq::maxGainDb);
        CHECK (std::abs (preset.levelDb) <= 12.0f);

        // The curve's average power change over its range (a voice's, 100 Hz
        // to 8 kHz; a guitar's, 80 Hz to 5 kHz; sampled every 1/24 octave),
        // plus LEVEL, is close to nothing: switching presets changes the tone, not the volume.
        auto power = 0.0;
        auto count = 0;
        const auto lowest = preset.vocal ? 100.0f : 80.0f, highest = preset.vocal ? 8000.0f : 5000.0f;
        for (auto frequency = lowest; frequency <= highest; frequency *= std::pow (2.0f, 1.0f / 24.0f), ++count)
            power += std::pow (10.0, effects::GraphicEq::responseDb (preset.gainsDb, frequency) / 10.0);
        const auto changeDb = 10.0 * std::log10 (power / count) + preset.levelDb;
        CHECK (std::abs (changeDb) <= 0.5);
    }
}

TEST_CASE ("Reverb types: each is a stable stereo space of its own", "[effects][reverb]")
{
    using ReverbType = effects::Reverb::Type;
    const auto play = [] (ReverbType type, const std::vector<float>& input, float preDelayMs = 0.0f)
    {
        EffectRig rig;
        rig.set (ParamIDs::reverbOn, 1.0f);
        rig.set (ParamIDs::reverbType, (float) type);
        rig.set (ParamIDs::reverbMix, 1.0f);
        rig.set (ParamIDs::reverbPreDelay, preDelayMs);
        return rig.run (input);
    };

    SECTION ("Every type makes a stereo tail that fades away")
    {
        for (int type = 0; type < effects::Reverb::typeNames.size(); ++type)
        {
            INFO (effects::Reverb::typeNames[type]);
            const auto output = play ((ReverbType) type, impulse (14.0, 0.5f));
            REQUIRE (allFinite (output[0]));
            REQUIRE (allFinite (output[1]));
            CHECK (rms (output[0], 0.05, 1.0) > 1.0e-3f);    // there's a tail
            CHECK (rms (output[0], 13.0, 14.0) < 1.0e-4f);   // and it ends, even the longest cathedral

            std::vector<float> difference (output[0].size());
            for (size_t i = 0; i < difference.size(); ++i)
                difference[i] = output[0][i] - output[1][i];
            CHECK (rms (difference, 0.05, 1.0) > 0.1f * rms (output[0], 0.05, 1.0));   // and it's wide
        }
    }

    SECTION ("Bigger spaces ring longer: room, then hall, then cathedral")
    {
        const auto tailAt = [&] (ReverbType type)
        {
            return rms (play (type, impulse (2.0, 0.5f))[0], 1.4, 1.6);
        };
        const auto room = tailAt (ReverbType::room), hall = tailAt (ReverbType::hall), cathedral = tailAt (ReverbType::cathedral);
        CHECK (hall > 10.0f * room);
        CHECK (cathedral > 3.0f * hall);
    }

    SECTION ("PRE-DELAY: nothing comes back until it's over")
    {
        const auto output = play (ReverbType::hall, impulse (0.5, 0.5f), 100.0f);
        CHECK (rms (output[0], 0.005, 0.095) < 1.0e-5f);   // the click itself is at 0; then silence
        CHECK (rms (output[0], 0.1, 0.3) > 1.0e-3f);       // then the reverb
    }

    SECTION ("Reverse swells up rather than dying away")
    {
        const auto output = play (ReverbType::reverse, impulse (1.5, 0.5f));
        const auto start = rms (output[0], 0.005, 0.15);
        const auto swell = rms (output[0], 0.35, 0.8);
        CHECK (swell > 3.0f * start);
    }

    SECTION ("Spring is band-limited like a real tank: little deep bass or fizz")
    {
        const auto output = play (ReverbType::spring, impulse (2.0, 0.5f));
        // The tail's energy in an octave round a frequency.
        const auto band = [&] (float hz)
        {
            juce::dsp::IIR::Filter<float> filter;
            filter.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (sampleRate, hz, 1.4f);
            std::vector<float> filtered (output[0]);
            for (auto& sample : filtered)
                sample = filter.processSample (sample);
            return rms (filtered, 0.02, 1.5);
        };
        const auto mids = band (1200.0f);
        CHECK (mids > 4.0f * band (50.0f));
        CHECK (mids > 4.0f * band (12000.0f));
    }
}

TEST_CASE ("Reverb: the controls inspired by Lexicon LXP do what they say", "[effects][reverb]")
{
    using ReverbType = effects::Reverb::Type;
    const auto play = [] (ReverbType type, std::initializer_list<std::pair<const char*, float>> settings, const std::vector<float>& input)
    {
        EffectRig rig;
        rig.set (ParamIDs::reverbOn, 1.0f);
        rig.set (ParamIDs::reverbType, (float) type);
        rig.set (ParamIDs::reverbMix, 1.0f);
        rig.set (ParamIDs::reverbPreDelay, 0.0f);
        for (const auto& [id, value] : settings)
            rig.set (id, value);
        return rig.run (input)[0];
    };
    // An octave band of a signal (around `hz`).
    const auto band = [] (const std::vector<float>& samples, float hz)
    {
        juce::dsp::IIR::Filter<float> first, second;
        first.coefficients = second.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (sampleRate, hz, 1.4f);
        auto filtered = samples;
        for (auto& sample : filtered)
            sample = second.processSample (first.processSample (sample));
        return filtered;
    };
    // Echo density: how many moments in a stretch have real sound in them
    // (above a tenth of the stretch's loudest), as a share of the stretch.
    const auto density = [] (const std::vector<float>& samples, double from, double to)
    {
        const auto start = (size_t) (from * sampleRate), end = (size_t) (to * sampleRate);
        auto peak = 0.0f;
        for (auto i = start; i < end; ++i)
            peak = std::max (peak, std::abs (samples[i]));
        int busy = 0;
        for (auto i = start; i < end; ++i)
            busy += std::abs (samples[i]) > 0.1f * peak ? 1 : 0;
        return (double) busy / (double) (end - start);
    };
    // How long the tail takes to fall 60 dB, from how fast it falls between two moments.
    const auto rt60 = [] (const std::vector<float>& samples, double from, double to)
    {
        const auto early = dB (rms (samples, from - 0.05, from + 0.05)), late = dB (rms (samples, to - 0.05, to + 0.05));
        return 60.0 * (to - from) / (double) (early - late);
    };

    SECTION ("DECAY stretches the tail: 200% rings twice as long, and the stomp shows the seconds")
    {
        const auto normal = rt60 (play (ReverbType::hall, {}, impulse (4.0, 0.5f)), 0.3, 1.3);
        const auto longer = rt60 (play (ReverbType::hall, { { ParamIDs::reverbDecay, 2.0f } }, impulse (4.0, 0.5f)), 0.3, 1.3);
        CHECK_THAT (longer / normal, WithinAbs (2.0, 0.3));
        CHECK_THAT (effects::Reverb::decaySeconds (ReverbType::hall, 0.5f, 2.0f), WithinAbs (2.0 * effects::Reverb::decaySeconds (ReverbType::hall, 0.5f), 1.0e-4));
    }

    SECTION ("BASS RT: the lows ring longer than the rest at 2x, and the mids don't change")
    {
        // CROSSOVER well above the lows measured, as you'd set it for them.
        const auto hallWith = [&] (float bassRt) { return play (ReverbType::hall, { { ParamIDs::reverbBassRt, bassRt }, { ParamIDs::reverbCrossover, 800.0f } }, impulse (4.0, 0.5f)); };
        const auto lowRt = [&] (float bassRt) { return rt60 (band (hallWith (bassRt), 100.0f), 0.3, 1.3); };
        const auto midRt = [&] (float bassRt) { return rt60 (band (hallWith (bassRt), 3000.0f), 0.3, 1.3); };
        CHECK_THAT (lowRt (2.0f) / lowRt (1.0f), WithinAbs (2.0, 0.3));
        CHECK_THAT (midRt (2.0f) / midRt (1.0f), WithinAbs (1.0, 0.15));
    }

    SECTION ("ROLLOFF cuts the reverb's highs on the way out")
    {
        const auto at = [&] (float rolloff, float hz) { return rms (band (play (ReverbType::plate, { { ParamIDs::reverbRolloff, rolloff } }, impulse (1.0, 0.5f)), hz), 0.02, 0.6); };
        CHECK (dB (at (1500.0f, 8000.0f) / at (effects::Reverb::rolloffOffHz, 8000.0f)) < -20.0f);
        CHECK (dB (at (1500.0f, 300.0f) / at (effects::Reverb::rolloffOffHz, 300.0f)) > -3.0f);
    }

    SECTION ("EARLY and REVERB: the first reflections and the tail, each on its own")
    {
        // A room's reflections are its first 40 ms; its tail goes on long after.
        const auto room = [&] (float early, float tail) { return play (ReverbType::room, { { ParamIDs::reverbEarly, early }, { ParamIDs::reverbLevel, tail } }, impulse (1.0, 0.5f)); };
        const auto both = room (0.0f, 0.0f), tailOnly = room (effects::Reverb::levelOffDb, 0.0f), earlyOnly = room (0.0f, effects::Reverb::levelOffDb);
        CHECK (rms (tailOnly, 0.003, 0.03) < 0.7f * rms (both, 0.003, 0.03));   // without them, the start is quieter
        CHECK (rms (earlyOnly, 0.3, 0.6) < 1.0e-6f);                           // without the tail, nothing late
        CHECK (rms (earlyOnly, 0.003, 0.03) > 1.0e-3f);                        // but the reflections still there
    }

    SECTION ("SPREAD and SHAPE: fed in across the window, early or late in it")
    {
        // When the first 400 ms's energy comes, on average.
        const auto centre = [&] (float shape)
        {
            const auto output = play (ReverbType::hall, { { ParamIDs::reverbSpread, 250.0f }, { ParamIDs::reverbShape, shape },
                                                          { ParamIDs::reverbDecay, 0.3f } }, impulse (0.6, 0.5f));
            double weighted = 0.0, total = 0.0;
            for (size_t i = 10; i < (size_t) (0.4 * sampleRate); ++i)
            {
                const auto energy = (double) output[i] * output[i];
                weighted += energy * (double) i / sampleRate;
                total += energy;
            }
            return weighted / total;
        };
        CHECK (centre (1.0f) > centre (0.0f) + 0.06);   // SHAPE late: the reverb builds, then stops (a gated sound)
    }

    SECTION ("DIFFUSION: less of it leaves the first echoes apart, as distinct clicks")
    {
        const auto busy = [&] (float diffusion) { return density (play (ReverbType::plate, { { ParamIDs::reverbDiffusion, diffusion } }, impulse (0.3, 0.5f)), 0.002, 0.06); };
        CHECK (busy (1.25f) > 3.0 * busy (0.0f));
    }

    SECTION ("SPIN and WANDER move the tail; at 0 it holds still")
    {
        const auto still = play (ReverbType::digital, { { ParamIDs::reverbWander, 0.0f } }, impulse (1.5, 0.5f));
        const auto moving = play (ReverbType::digital, { { ParamIDs::reverbWander, 4.0f }, { ParamIDs::reverbSpin, 3.0f } }, impulse (1.5, 0.5f));
        REQUIRE (allFinite (moving));
        std::vector<float> difference (still.size());
        for (size_t i = 0; i < difference.size(); ++i)
            difference[i] = moving[i] - still[i];
        CHECK (rms (difference, 0.5, 1.4) > 0.3f * rms (still, 0.5, 1.4));
    }

    SECTION ("CHAMBER: dense at once, shorter than a hall")
    {
        const auto chamber = play (ReverbType::chamber, {}, impulse (2.0, 0.5f)), hall = play (ReverbType::hall, {}, impulse (2.0, 0.5f));
        CHECK (density (chamber, 0.002, 0.04) > 1.5 * density (hall, 0.002, 0.04));   // a hall starts sparse
        CHECK (rms (chamber, 1.4, 1.6) < rms (hall, 1.4, 1.6));
    }

    SECTION ("Classic and Spring don't take them")
    {
        for (const auto type : { ReverbType::classic, ReverbType::spring })
        {
            const auto plain = play (type, {}, impulse (0.8, 0.5f));
            const auto set = play (type, { { ParamIDs::reverbDecay, 3.0f }, { ParamIDs::reverbRolloff, 1000.0f }, { ParamIDs::reverbSpread, 200.0f } }, impulse (0.8, 0.5f));
            CHECK (maxAbsDifference (plain, set) < 1.0e-6f);
        }
    }
}

TEST_CASE ("DUCK tucks the delay and the reverb under the playing, on any type", "[effects][delay][reverb]")
{
    // Play for a second, then stop. With DUCK at 100% the wet signal is well
    // down while playing, and comes back once the playing stops.
    auto input = sine (220.0, 0.3f, 1.0);
    input.resize ((size_t) (2.5 * sampleRate), 0.0f);
    const auto wetOf = [&] (const char* onId, const char* duckId, const char* typeId, float type, float duck)
    {
        EffectRig rig;
        rig.set (onId, 1.0f);
        rig.set (typeId, type);
        rig.set (duckId, duck);
        if (std::string (onId) == ParamIDs::delayOn)
        {
            rig.set (ParamIDs::delaySync, 0.0f);
            rig.set (ParamIDs::delayTime, 250.0f);
            rig.set (ParamIDs::delayFeedback, 0.5f);
            rig.set (ParamIDs::delayMix, 1.0f);
        }
        else
        {
            rig.set (ParamIDs::reverbMix, 1.0f);
        }
        auto output = rig.runMono (input);
        for (size_t i = 0; i < output.size(); ++i)
            output[i] -= input[i];
        return output;
    };

    SECTION ("Delay: a ducked tape delay")
    {
        const auto plain = wetOf (ParamIDs::delayOn, ParamIDs::delayDuck, ParamIDs::delayType, (float) effects::Delay::Type::tape, 0.0f);
        const auto ducked = wetOf (ParamIDs::delayOn, ParamIDs::delayDuck, ParamIDs::delayType, (float) effects::Delay::Type::tape, 1.0f);
        CHECK (rms (ducked, 0.5, 0.95) < 0.1f * rms (plain, 0.5, 0.95));   // 20 dB down while playing
        CHECK (rms (ducked, 1.5, 1.8) > 0.5f * rms (plain, 1.5, 1.8));     // back after
    }

    SECTION ("Reverb: a ducked cathedral")
    {
        const auto plain = wetOf (ParamIDs::reverbOn, ParamIDs::reverbDuck, ParamIDs::reverbType, (float) effects::Reverb::Type::cathedral, 0.0f);
        const auto ducked = wetOf (ParamIDs::reverbOn, ParamIDs::reverbDuck, ParamIDs::reverbType, (float) effects::Reverb::Type::cathedral, 1.0f);
        CHECK (rms (ducked, 0.5, 0.95) < 0.1f * rms (plain, 0.5, 0.95));
        CHECK (rms (ducked, 1.5, 1.8) > 0.5f * rms (plain, 1.5, 1.8));
    }

    SECTION ("DUCK at 0% changes nothing")
    {
        EffectRig a, b;
        for (auto* rig : { &a, &b })
        {
            rig->set (ParamIDs::reverbOn, 1.0f);
            rig->set (ParamIDs::delayOn, 1.0f);
        }
        b.set (ParamIDs::reverbDuck, 0.0f);
        b.set (ParamIDs::delayDuck, 0.0f);
        CHECK (maxAbsDifference (a.runMono (input), b.runMono (input)) < 1.0e-6f);
    }
}

TEST_CASE ("Projects and rigs saved before reverb types keep the original reverb", "[effects][reverb]")
{
    EffectRig original;
    juce::MemoryBlock saved;
    original.processor.getStateInformation (saved);
    auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
    REQUIRE (xml != nullptr);
    for (auto* child : xml->getChildIterator())
        if (child->getStringAttribute ("id") == ParamIDs::reverbType)
        {
            xml->removeChildElement (child, true);
            break;
        }
    juce::MemoryBlock older;
    juce::AudioProcessor::copyXmlToBinary (*xml, older);

    EffectRig restored;
    restored.processor.setStateInformation (older.getData(), (int) older.getSize());
    CHECK (juce::roundToInt (restored.processor.getParameters().getRawParameterValue (ParamIDs::reverbType)->load())
           == (int) effects::Reverb::Type::classic);

    EffectRig fresh;   // new rigs: the Digital reverb and the Digital delay
    CHECK (juce::roundToInt (fresh.processor.getParameters().getRawParameterValue (ParamIDs::reverbType)->load())
           == (int) effects::Reverb::Type::digital);
    CHECK (juce::roundToInt (fresh.processor.getParameters().getRawParameterValue (ParamIDs::delayType)->load())
           == (int) effects::Delay::Type::digital);
}


TEST_CASE ("LOW CUT: takes out rumble below its frequency and leaves the voice alone", "[effects][lowcut]")
{
    // A 12 dB-per-octave (Butterworth) high-pass: 3 dB down at the knob's
    // frequency, about 12 dB an octave below, about 24 dB two octaves below.
    const auto levelThrough = [] (float lowCutHz, double hz)
    {
        EffectRig rig;
        rig.set (ParamIDs::inputLowCut, lowCutHz);
        const auto input = sine (hz, 0.3f, 1.0);
        return dB (levelAt (rig.runMono (input), hz, 0.3) / levelAt (input, hz, 0.3));
    };

    SECTION ("At 100 Hz")
    {
        CHECK_THAT (levelThrough (100.0f, 25.0), WithinAbs (-24.1, 1.0));   // mic-stand rumble
        CHECK_THAT (levelThrough (100.0f, 50.0), WithinAbs (-12.3, 0.5));   // mains hum, plosive thump
        CHECK_THAT (levelThrough (100.0f, 100.0), WithinAbs (-3.0, 0.3));
        CHECK_THAT (levelThrough (100.0f, 220.0), WithinAbs (-0.2, 0.2));   // a low sung note: barely touched
        CHECK_THAT (levelThrough (100.0f, 1000.0), WithinAbs (0.0, 0.05));  // the body of the voice: untouched
    }

    SECTION ("The knob moves the corner")
    {
        CHECK_THAT (levelThrough (200.0f, 200.0), WithinAbs (-3.0, 0.3));
        CHECK (levelThrough (200.0f, 100.0) < levelThrough (80.0f, 100.0) - 9.0f);
    }
}

TEST_CASE ("LOW CUT: OFF is exactly the input; the default is 80 Hz", "[effects][lowcut]")
{
    EffectRig rig;   // the rig starts with LOW CUT at OFF
    const auto rumble = sine (40.0, 0.3f, 0.5);
    CHECK (maxAbsDifference (rumble, rig.runMono (rumble)) < 1.0e-6f);

    auto* lowCut = rig.processor.getParameters().getParameter (ParamIDs::inputLowCut);
    CHECK (lowCut->getText (lowCut->convertTo0to1 (effects::LowCut::offHz), 16) == "Off");
    CHECK (lowCut->getText (lowCut->getDefaultValue(), 16) == "80 Hz");

    // A fresh plugin starts with it on, filtering from the very first note.
    ParagonX1AudioProcessor fresh;
    fresh.setPlayConfigDetails (1, 2, sampleRate, blockSize);
    fresh.prepareToPlay (sampleRate, blockSize);
    juce::AudioBuffer<float> buffer (2, blockSize);
    buffer.clear();
    for (int i = 0; i < blockSize; ++i)
        buffer.setSample (0, i, 0.3f);   // a step: what a plosive does to the mic
    juce::MidiBuffer midi;
    fresh.processBlock (buffer, midi);
    CHECK (std::abs (buffer.getSample (0, blockSize - 1)) < 0.3f * 0.95f);   // already draining away
}

TEST_CASE ("LOW CUT: turning the knob, or switching it off and on, doesn't click", "[effects][lowcut]")
{
    // A steady 1 kHz note while LOW CUT sweeps from OFF to the top and back.
    // A click would show as a sample far from the clean sine.
    EffectRig rig;
    const auto input = sine (1000.0, 0.3f, 1.2);
    std::vector<float> output;
    const float settings[] = { effects::LowCut::offHz, effects::LowCut::maxHz, 80.0f, effects::LowCut::offHz, 300.0f, 150.0f };
    const auto chunk = input.size() / std::size (settings);
    for (size_t step = 0; step < std::size (settings); ++step)
    {
        rig.set (ParamIDs::inputLowCut, settings[step]);
        const auto part = rig.runMono ({ input.begin() + (long) (step * chunk), input.begin() + (long) ((step + 1) * chunk) });
        output.insert (output.end(), part.begin(), part.end());
    }

    // A click is a sudden jump between neighbouring samples. A 0.3 sine at
    // 1 kHz never moves more than 0.3 x 2 pi x 1000 / 48000 = 0.039 per
    // sample, so the output must stay under that (plus a hair).
    CHECK (allFinite (output));
    auto biggestStep = 0.0f;
    for (size_t i = 1; i < output.size(); ++i)
        biggestStep = std::max (biggestStep, std::abs (output[i] - output[i - 1]));
    CHECK (biggestStep < 0.0415f);

    // And the note keeps its level throughout: even at 400 Hz, LOW CUT takes
    // under 0.3 dB off 1 kHz. (It shifts its timing slightly, which is why
    // this compares levels rather than samples.)
    for (size_t step = 0; step < std::size (settings); ++step)
    {
        const auto from = (double) (step * chunk) / sampleRate, to = (double) ((step + 1) * chunk) / sampleRate;
        CHECK_THAT (dB (rms (output, from, to) / rms (input, from, to)), WithinAbs (0.0, 0.35));
    }
}

TEST_CASE ("LIMITER: nothing leaves above the ceiling; normal levels are untouched", "[effects][limiter]")
{
    const auto peak = [] (const std::vector<float>& samples, double fromSeconds = 0.0)
    {
        auto result = 0.0f;
        for (auto i = (size_t) (fromSeconds * sampleRate); i < samples.size(); ++i)
            result = std::max (result, std::abs (samples[i]));
        return result;
    };

    SECTION ("A note 6 dB over full scale comes out at -1 dB, the default ceiling, on both sides")
    {
        EffectRig rig;
        const auto output = rig.run (sine (440.0, 2.0f, 0.5));
        const auto ceiling = juce::Decibels::decibelsToGain (-1.0f);
        for (const auto& channel : output)
        {
            CHECK (peak (channel) <= ceiling + 1.0e-6f);   // never over, not even the first sample
            CHECK (peak (channel, 0.1) > ceiling * 0.97f);  // and held up at the ceiling, not crushed
        }
        CHECK (rig.processor.getLimiterReductionDb() < -5.0f);   // the light is on
    }

    SECTION ("Under the ceiling it does nothing at all, and the light is off")
    {
        EffectRig rig;
        const auto input = sine (440.0, 0.3f, 0.5);
        CHECK (maxAbsDifference (input, rig.runMono (input)) < 1.0e-6f);
        CHECK (rig.processor.getLimiterReductionDb() == 0.0f);
    }

    SECTION ("The knob sets the ceiling (gliding there over 50 ms when turned, so it doesn't zip)")
    {
        EffectRig rig;
        rig.set (ParamIDs::outputLimiter, -6.0f);
        CHECK (peak (rig.runMono (sine (440.0, 0.9f, 0.5)), 0.06) <= juce::Decibels::decibelsToGain (-6.0f) + 1.0e-6f);
    }

    SECTION ("OFF lets everything through")
    {
        EffectRig rig;
        rig.set (ParamIDs::outputLimiter, effects::Limiter::offDb);
        auto* limiter = rig.processor.getParameters().getParameter (ParamIDs::outputLimiter);
        CHECK (limiter->getCurrentValueAsText() == "Off");
        const auto input = sine (440.0, 2.0f, 0.5);
        CHECK (maxAbsDifference (input, rig.runMono (input), (size_t) (0.1 * sampleRate)) < 1.0e-6f);
    }

    SECTION ("After a loud peak it lets go, so the next quiet phrase is back at full level")
    {
        EffectRig rig;
        auto input = sine (440.0, 2.0f, 0.3);                    // a shout...
        const auto quiet = sine (440.0, 0.3f, 0.7);              // ...then singing softly
        input.insert (input.end(), quiet.begin(), quiet.end());
        const auto output = rig.runMono (input);
        // 0.4 s after the shout, more than three times the 100 ms release:
        // the soft singing is untouched again.
        CHECK_THAT (dB (rms (output, 0.7, 1.0) / rms (input, 0.7, 1.0)), WithinAbs (0.0, 0.05));
        CHECK (rig.processor.getLimiterReductionDb() == 0.0f);
    }

    SECTION ("Switching it off and on during a loud note doesn't click")
    {
        // A note over full scale, with LIMITER switched OFF, back to -1 dB,
        // then to -9 dB. A click is a jump between neighbouring samples:
        // this note never moves more than 1.2 x 2 pi x 440 / 48000 = 0.069 per
        // sample, so the output must stay under that (plus a hair).
        EffectRig rig;
        const auto input = sine (440.0, 1.2f, 0.8);
        const float settings[] = { -1.0f, effects::Limiter::offDb, -1.0f, -9.0f };
        const auto chunk = input.size() / std::size (settings);
        std::vector<float> output;
        for (size_t step = 0; step < std::size (settings); ++step)
        {
            rig.set (ParamIDs::outputLimiter, settings[step]);
            const auto part = rig.runMono ({ input.begin() + (long) (step * chunk), input.begin() + (long) ((step + 1) * chunk) });
            output.insert (output.end(), part.begin(), part.end());
        }
        auto biggestStep = 0.0f;
        for (size_t i = 1; i < output.size(); ++i)
            biggestStep = std::max (biggestStep, std::abs (output[i] - output[i - 1]));
        CHECK (biggestStep < 0.072f);
    }

    SECTION ("Its default is -1 dB, and it doesn't change with rigs")
    {
        EffectRig rig;
        auto* limiter = rig.processor.getParameters().getParameter (ParamIDs::outputLimiter);
        CHECK (limiter->getText (limiter->getDefaultValue(), 16) == "-1.0 dB");
        CHECK (ParamIDs::notInRigs.contains (ParamIDs::outputLimiter));
        CHECK (rig.processor.getLatencySamples() == 0);   // no look-ahead: nothing delays the voice
    }
}

namespace
{
    // A sung "ah" at 220 Hz: the note and its overtones up to about 3 kHz,
    // each quieter than the last, as a voice's are. It fades in over 10 ms,
    // as a sung note does: starting instantly would be a click, which is
    // bright and sibilant enough for a de-esser to catch.
    std::vector<float> vowel (double seconds, float level = 0.3f)
    {
        std::vector<float> samples ((size_t) (seconds * sampleRate), 0.0f);
        for (int harmonic = 1; harmonic * 220.0 < 3000.0; ++harmonic)
            for (size_t i = 0; i < samples.size(); ++i)
                samples[i] += level / (float) harmonic
                            * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * harmonic * (double) i / sampleRate);
        const auto fade = (size_t) (0.01 * sampleRate);
        for (size_t i = 0; i < std::min (fade, samples.size()); ++i)
            samples[i] *= 0.5f - 0.5f * (float) std::cos (juce::MathConstants<double>::pi * (double) i / (double) fade);
        return samples;
    }

    // An "s": energy spread over the top, between about 6 and 10 kHz
    // (shifted down to `lowestHz` and up from there for other voices).
    std::vector<float> ess (double seconds, float level = 0.1f, double lowestHz = 6100.0)
    {
        std::vector<float> samples ((size_t) (seconds * sampleRate), 0.0f);
        for (const auto hz : { lowestHz, lowestHz * 1.2, lowestHz * 1.34, lowestHz * 1.57 })
            for (size_t i = 0; i < samples.size(); ++i)
                samples[i] += level * (float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) i / sampleRate + hz);
        return samples;
    }
}

TEST_CASE ("DE-ESSER: turns the esses down and leaves the vowels alone", "[effects][deesser]")
{
    const auto deEssed = [] (const std::vector<float>& input, float amountDb = 6.0f, float frequencyHz = 6000.0f, bool listen = false)
    {
        EffectRig rig;
        rig.set (ParamIDs::deessOn, 1.0f);
        rig.set (ParamIDs::deessAmount, amountDb);
        rig.set (ParamIDs::deessFreq, frequencyHz);
        rig.set (ParamIDs::deessListen, listen ? 1.0f : 0.0f);
        return rig.runMono (input);
    };

    SECTION ("A vowel comes through exactly as it went in")
    {
        const auto input = vowel (0.5);
        CHECK (maxAbsDifference (input, deEssed (input, 18.0f)) < 1.0e-6f);
    }

    SECTION ("An ess is turned down by AMOUNT")
    {
        const auto input = ess (0.5);
        for (const auto amount : { 6.0f, 12.0f })
        {
            INFO ("AMOUNT " << amount);
            CHECK_THAT (dB (rms (deEssed (input, amount), 0.1) / rms (input, 0.1)), WithinAbs (-amount, 1.5));
        }
        CHECK_THAT (dB (rms (deEssed (input, 0.0f), 0.1) / rms (input, 0.1)), WithinAbs (0.0, 0.01));   // AMOUNT 0: nothing
    }

    SECTION ("It follows the singing: the ess in a phrase is turned down, the vowels either side aren't")
    {
        auto input = vowel (0.4);
        const auto s = ess (0.2);
        const auto after = vowel (0.4);
        input.insert (input.end(), s.begin(), s.end());
        input.insert (input.end(), after.begin(), after.end());
        const auto output = deEssed (input, 10.0f);
        CHECK_THAT (dB (rms (output, 0.45, 0.6) / rms (input, 0.45, 0.6)), WithinAbs (-10.0, 2.0));   // the ess
        CHECK (maxAbsDifference (input, output) > 0.0f);
        CHECK_THAT (dB (rms (output, 0.1, 0.38) / rms (input, 0.1, 0.38)), WithinAbs (0.0, 0.01));    // the vowel before
        CHECK_THAT (dB (rms (output, 0.8, 1.0) / rms (input, 0.8, 1.0)), WithinAbs (0.0, 0.05));      // and after (once released)
    }

    SECTION ("It reacts the same to a quiet ess and a loud one")
    {
        CHECK_THAT (dB (rms (deEssed (ess (0.5, 0.01f)), 0.1) / rms (ess (0.5, 0.01f), 0.1)),
                    WithinAbs (dB (rms (deEssed (ess (0.5, 0.2f)), 0.1) / rms (ess (0.5, 0.2f), 0.1)), 0.5));
    }

    SECTION ("FREQUENCY: set above a low ess it misses it; set below, it catches it")
    {
        const auto lowEss = ess (0.5, 0.1f, 3200.0);   // a lower, darker "sh"
        CHECK (dB (rms (deEssed (lowEss, 12.0f, 11000.0f), 0.1) / rms (lowEss, 0.1)) > -3.0f);
        CHECK (dB (rms (deEssed (lowEss, 12.0f, 2500.0f), 0.1) / rms (lowEss, 0.1)) < -8.0f);
    }

    SECTION ("LISTEN plays only what's taken away: the esses, and silence for the vowels")
    {
        const auto vowelHeard = deEssed (vowel (0.5), 6.0f, 6000.0f, true);
        CHECK (rms (vowelHeard, 0.1) < 1.0e-5f);

        // At 6 dB, half the ess's level is taken away, so that's what LISTEN plays.
        const auto input = ess (0.5);
        const auto essHeard = deEssed (input, 6.0f, 6000.0f, true);
        CHECK_THAT (dB (rms (essHeard, 0.1) / rms (input, 0.1)), WithinAbs (-6.0, 2.0));
    }

    SECTION ("Off, it's exactly the input")
    {
        EffectRig rig;
        rig.set (ParamIDs::deessAmount, 18.0f);   // set up, but switched off
        const auto input = ess (0.3);
        CHECK (maxAbsDifference (input, rig.runMono (input)) < 1.0e-6f);
    }
}

namespace
{
    // A sung vowel: a note at `hz` with its overtones up to 4 kHz, shaped
    // by one vocal resonance (formant) at `formantHz`, as a voice's are.
    std::vector<float> sungVowel (double hz, double seconds, double formantHz = 800.0)
    {
        std::vector<float> samples ((size_t) (seconds * sampleRate), 0.0f);
        for (int harmonic = 1; harmonic * hz < 4000.0; ++harmonic)
        {
            const auto f = harmonic * hz;
            const auto amplitude = (float) (0.05 * (0.06 + 1.0 / (1.0 + std::pow ((f - formantHz) / 180.0, 2.0))));
            for (size_t i = 0; i < samples.size(); ++i)
                samples[i] += amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * f * (double) i / sampleRate + harmonic);
        }
        return samples;
    }

    // The pitch of the last part of a signal, in Hz (0 if there's no clear note).
    double pitchOf (const std::vector<float>& samples)
    {
        PitchDetector detector { 60.0, 1100.0 };
        detector.prepare (sampleRate);
        const auto length = detector.windowSizeAtHostRate();
        const auto result = detector.detect (samples.data() + (samples.size() - (size_t) length), length);
        return result.has_value() ? result->frequency : 0.0;
    }

    double centsBetween (double hz, double reference) { return 1200.0 * std::log2 (hz / reference); }

    // A rig with HARMONY on, only voice 1 sounding (in the middle, at 0 dB),
    // and the singer's own voice turned off, so the output is voice 1 alone.
    struct HarmonyRig : EffectRig
    {
        HarmonyRig (float semitones, int key = 0, Scales::Scale scale = Scales::Scale::major)
        {
            set (ParamIDs::harmonyOn, 1.0f);
            set (ParamIDs::harmonySource, effects::Harmony::silentDb);
            set (ParamIDs::harmonyKey, (float) key);
            set (ParamIDs::harmonyScale, (float) scale);
            setVoice ("Pitch", semitones);
            setVoice ("Pan", 0.0f);
            setVoice ("Level", 0.0f);
            set (ParamIDs::harmonyVoice (1, "Active").toRawUTF8(), 0.0f);
        }
        void setVoice (const char* part, float value, int voice = 0) { set (ParamIDs::harmonyVoice (voice, part).toRawUTF8(), value); }
    };
}

TEST_CASE ("HARMONY: voices sing the interval, corrected into the key", "[effects][harmony]")
{
    const auto harmonyOf = [] (HarmonyRig& rig, double sungHz) { return pitchOf (rig.run (sungVowel (sungHz, 1.0))[0]); };
    const auto A3 = 220.0, D3 = 146.832, E4 = 329.628, F3 = 174.614, Fsharp3 = 184.997;

    SECTION ("A fifth up (+7) on an A, in C major, is the E above")
    {
        HarmonyRig rig (7.0f);
        CHECK (std::abs (centsBetween (harmonyOf (rig, A3), E4)) < 5.0);
    }

    SECTION ("+4 on a D in C major: F#, the major third, isn't in the key, so it sings F, the minor third")
    {
        HarmonyRig rig (4.0f);
        CHECK (std::abs (centsBetween (harmonyOf (rig, D3), F3)) < 5.0);
    }

    SECTION ("The same +4 on a D in A major (F# is in the key) sings F#")
    {
        HarmonyRig rig (4.0f, 9 /* A */);
        CHECK (std::abs (centsBetween (harmonyOf (rig, D3), Fsharp3)) < 5.0);
    }

    SECTION ("CORRECT off: exactly +4, whatever the key")
    {
        HarmonyRig rig (4.0f);
        rig.setVoice ("Correct", 0.0f);
        CHECK (std::abs (centsBetween (harmonyOf (rig, D3), Fsharp3)) < 5.0);
    }

    SECTION ("A singer 20 cents flat gets a harmony in tune; uncorrected, it's as flat as they are")
    {
        const auto flatA = A3 * std::pow (2.0, -20.0 / 1200.0);
        HarmonyRig corrected (7.0f);
        CHECK (std::abs (centsBetween (harmonyOf (corrected, flatA), E4)) < 5.0);

        HarmonyRig free (7.0f);
        free.setVoice ("Correct", 0.0f);
        CHECK (std::abs (centsBetween (harmonyOf (free, flatA), E4) + 20.0) < 5.0);
    }

    SECTION ("FINE adds cents on top")
    {
        HarmonyRig rig (7.0f);
        rig.setVoice ("Fine", 30.0f);
        CHECK (std::abs (centsBetween (harmonyOf (rig, A3), E4) - 30.0) < 5.0);
    }

    SECTION ("Down an octave (-12)")
    {
        HarmonyRig rig (-12.0f);
        CHECK (std::abs (centsBetween (harmonyOf (rig, A3), A3 / 2.0)) < 5.0);
    }
}

TEST_CASE ("HARMONY: a raised voice keeps its vowel (formants), unless CORRECT FORMANT is turned down", "[effects][harmony]")
{
    // Where a voice's resonance (its formant: what makes an "ah" an "ah")
    // sits: the frequency of its loudest overtone. The test vowel's is
    // built at 800 Hz.
    const auto resonanceOf = [] (const std::vector<float>& samples, double hz)
    {
        auto loudest = 0.0f;
        auto where = 0.0;
        for (int harmonic = 1; harmonic * hz < 4000.0; ++harmonic)
            if (const auto level = levelAt (samples, harmonic * hz, 0.4); level > loudest)
            {
                loudest = level;
                where = harmonic * hz;
            }
        return where;
    };

    const auto sung = 150.0, raised = 150.0 * std::pow (2.0, 7.0 / 12.0);   // a fifth up: 224.7 Hz
    const auto input = sungVowel (sung, 1.0, 800.0);
    INFO ("The sung vowel's resonance: " << resonanceOf (input, sung) << " Hz");

    // CORRECT FORMANT at 100% (the default): the resonance stays near 800 Hz
    // (its nearest overtones a fifth up are 674 and 899 Hz): a person singing
    // a fifth higher.
    HarmonyRig natural (7.0f);
    natural.setVoice ("Correct", 0.0f);   // exactly +7, so the overtones are where we measure
    const auto kept = resonanceOf (natural.run (input)[0], raised);
    CHECK (kept > 650.0);
    CHECK (kept < 950.0);

    // At 0%: the resonance moves up with the pitch, to about 1200 Hz: a sped-up tape.
    HarmonyRig chipmunk (7.0f);
    chipmunk.setVoice ("Correct", 0.0f);
    chipmunk.set (ParamIDs::harmonyCorrectFormant, 0.0f);
    CHECK (resonanceOf (chipmunk.run (input)[0], raised) > 1050.0);

    // FORMANT down a fifth: the resonance moves down to about 530 Hz: a bigger singer, same note.
    HarmonyRig deeper (7.0f);
    deeper.setVoice ("Correct", 0.0f);
    deeper.setVoice ("Formant", -7.0f);
    CHECK (resonanceOf (deeper.run (input)[0], raised) < 600.0);
}

TEST_CASE ("HARMONY: levels, pan, source and switching off", "[effects][harmony]")
{
    const auto input = sungVowel (220.0, 1.0);

    SECTION ("Off, the voice comes through exactly as it went in")
    {
        EffectRig rig;
        rig.set (ParamIDs::harmonyLevel, 6.0f);   // set up, but switched off
        const auto output = rig.run (input);
        CHECK (maxAbsDifference (input, output[0]) < 1.0e-6f);
        CHECK (maxAbsDifference (input, output[1]) < 1.0e-6f);
    }

    SECTION ("On with every voice silent, SOURCE at 0 dB is exactly the voice")
    {
        EffectRig rig;
        rig.set (ParamIDs::harmonyOn, 1.0f);
        rig.set (ParamIDs::harmonyLevel, effects::Harmony::silentDb);
        CHECK (maxAbsDifference (input, rig.run (input)[0], (size_t) (0.05 * sampleRate)) < 1.0e-5f);
    }

    SECTION ("A voice panned hard left is only on the left")
    {
        HarmonyRig rig (7.0f);
        rig.setVoice ("Pan", -1.0f);
        const auto output = rig.run (input);
        CHECK (rms (output[0], 0.3) > 0.01f);
        CHECK (rms (output[1], 0.3) < 1.0e-4f);
    }

    SECTION ("A voice's LEVEL, and HARMONY's overall level, set its loudness")
    {
        const auto loudness = [&input] (float voiceDb, float harmonyDb)
        {
            HarmonyRig rig (7.0f);
            rig.setVoice ("Level", voiceDb);
            rig.set (ParamIDs::harmonyLevel, harmonyDb);
            return rms (rig.run (input)[0], 0.3);
        };
        const auto full = loudness (0.0f, 0.0f);
        CHECK_THAT (dB (loudness (-6.0f, 0.0f) / full), WithinAbs (-6.0, 0.5));
        CHECK_THAT (dB (loudness (0.0f, -10.0f) / full), WithinAbs (-10.0, 0.5));
    }

    SECTION ("A raised voice is about as loud as the singer")
    {
        HarmonyRig rig (7.0f);
        CHECK_THAT (dB (rms (rig.run (input)[0], 0.3) / rms (input, 0.3)), WithinAbs (0.0, 3.0));
    }

    SECTION ("Breath and consonants (no note) come through safely at the singer's pitch")
    {
        HarmonyRig rig (7.0f);
        juce::Random random (42);
        std::vector<float> breath ((size_t) sampleRate / 2);
        for (auto& sample : breath)
            sample = 0.1f * (random.nextFloat() * 2.0f - 1.0f);
        const auto output = rig.run (breath)[0];
        CHECK (allFinite (output));
        CHECK (rms (output, 0.2) < 2.0f * rms (breath, 0.2));
    }

    SECTION ("The singer's own voice still has no delay: the plugin reports none")
    {
        HarmonyRig rig (7.0f);
        CHECK (rig.processor.getLatencySamples() == 0);
    }
}

namespace
{
    // A rig with DOUBLER on and only voice 1 sounding (in the middle, at
    // 0 dB, no detune), the direct voice off: the output is voice 1 alone.
    struct DoublerRig : EffectRig
    {
        explicit DoublerRig (int outputChannels = 2) : EffectRig (outputChannels)
        {
            set (ParamIDs::doublerOn, 1.0f);
            set (ParamIDs::doublerDirectOn, 0.0f);
            for (int voice = 1; voice < effects::Doubler::numVoices; ++voice)
                setVoice ("On", 0.0f, voice);
            setVoice ("Gain", 0.0f);
            setVoice ("Pan", 0.0f);
            setVoice ("Detune", 0.0f);
            setVoice ("Drift", 0.0f);   // steady, so each test hears only its own setting
        }
        void setVoice (const char* part, float value, int voice = 0) { set (ParamIDs::doublerVoice (voice, part).toRawUTF8(), value); }
    };

    // A click 100 ms in: after DOUBLER's 20 ms fade in as it's switched on,
    // which a click right at the start would partly slip through.
    constexpr double clickAtMs = 100.0;
    std::vector<float> lateClick (double seconds)
    {
        auto samples = silence (seconds);
        samples[(size_t) (clickAtMs * sampleRate / 1000.0)] = 1.0f;
        return samples;
    }

    // When the click first comes out, in ms after it went in.
    double arrivalMs (const std::vector<float>& samples, float threshold = 0.05f)
    {
        for (size_t i = 0; i < samples.size(); ++i)
            if (std::abs (samples[i]) > threshold)
                return (double) i / sampleRate * 1000.0 - clickAtMs;
        return -1.0;
    }
}

TEST_CASE ("DOUBLER: each voice detuned, delayed and placed as set", "[effects][doubler]")
{
    const auto note = sungVowel (220.0, 1.0);

    SECTION ("DETUNE moves the voice's pitch by its cents, keeping its timing")
    {
        DoublerRig rig;
        rig.setVoice ("Detune", 50.0f);
        CHECK (std::abs (centsBetween (pitchOf (rig.run (note)[0]), 220.0) - 50.0) < 4.0);
    }

    SECTION ("OCTAVE puts it an octave down")
    {
        DoublerRig rig;
        rig.setVoice ("Octave", 1.0f);
        CHECK (std::abs (centsBetween (pitchOf (rig.run (note)[0]), 110.0)) < 4.0);
    }

    SECTION ("DELAY puts it that late; below the detuning's own ~13 ms it comes at about that")
    {
        DoublerRig rig;
        rig.setVoice ("Delay", 40.0f);
        CHECK_THAT (arrivalMs (rig.run (lateClick (0.3))[0]), WithinAbs (40.0, 0.5));

        DoublerRig shortest;
        shortest.setVoice ("Delay", 2.0f);
        // The detuning's own delay: 4 samples plus half of its 25 ms jump, 12.6 ms at 48 kHz.
        CHECK_THAT (arrivalMs (shortest.run (lateClick (0.3))[0]), WithinAbs (12.6, 1.0));
    }

    SECTION ("FEEDBACK repeats it, each repeat quieter by the feedback")
    {
        DoublerRig rig;
        rig.setVoice ("Delay", 40.0f);
        rig.setVoice ("Feedback", 0.5f);
        const auto output = rig.run (lateClick (0.4))[0];
        const auto peakNear = [&output] (double ms)
        {
            ms += clickAtMs;
            auto peak = 0.0f;
            for (auto i = (size_t) ((ms - 3.0) * sampleRate / 1000.0); i < (size_t) ((ms + 3.0) * sampleRate / 1000.0); ++i)
                peak = std::max (peak, std::abs (output[i]));
            return peak;
        };
        CHECK (peakNear (40.0) > 0.5f);
        CHECK_THAT (peakNear (80.0) / peakNear (40.0), WithinAbs (0.5, 0.1));
        CHECK_THAT (peakNear (120.0) / peakNear (80.0), WithinAbs (0.5, 0.1));
    }

    SECTION ("PAN: hard left is only on the left")
    {
        DoublerRig rig;
        rig.setVoice ("Pan", -1.0f);
        const auto output = rig.run (note);
        CHECK (rms (output[0], 0.2) > 0.01f);
        CHECK (rms (output[1], 0.2) < 1.0e-5f);
    }

    SECTION ("GAIN and OUTPUT set the level")
    {
        const auto loudness = [&note] (float gainDb, float outputDb)
        {
            DoublerRig rig;
            rig.setVoice ("Gain", gainDb);
            rig.set (ParamIDs::doublerOutput, outputDb);
            return rms (rig.run (note)[0], 0.2);
        };
        const auto full = loudness (0.0f, 0.0f);
        CHECK_THAT (dB (full / rms (note, 0.2)), WithinAbs (0.0, 1.0));   // a voice at 0 dB is as loud as you
        CHECK_THAT (dB (loudness (-12.0f, 0.0f) / full), WithinAbs (-12.0, 0.3));
        CHECK_THAT (dB (loudness (0.0f, -6.0f) / full), WithinAbs (-6.0, 0.3));
    }

    SECTION ("DEPTH wobbles its pitch around DETUNE, at RATE")
    {
        DoublerRig rig;
        rig.setVoice ("Depth", 60.0f);
        rig.setVoice ("Rate", 1.0f);
        const auto output = rig.run (sungVowel (220.0, 1.6))[0];
        // Its pitch, a quarter of a cycle apart: up 60 cents, back, down 60.
        std::vector<double> pitches;
        for (const auto seconds : { 0.55, 0.8, 1.05, 1.3, 1.55 })
            pitches.push_back (centsBetween (pitchOf ({ output.begin(), output.begin() + (long) (seconds * sampleRate) }), 220.0));
        const auto [low, high] = std::minmax_element (pitches.begin(), pitches.end());
        CHECK (*high - *low > 60.0);   // it moves, by roughly the 120 cents from top to bottom
        CHECK (*high < 75.0);
        CHECK (*low > -75.0);
    }
}

TEST_CASE ("DOUBLER: DRIFT wanders each take's timing and tuning, like a second take", "[effects][doubler]")
{
    // Clicks every 0.3 s for 6 s: when each one comes out of the voice.
    const auto arrivals = [] (float drift)
    {
        DoublerRig rig;
        rig.setVoice ("Delay", 40.0f);
        rig.setVoice ("Drift", drift);
        auto input = silence (6.2);
        std::vector<size_t> clicks;
        for (auto seconds = 0.3; seconds < 6.0; seconds += 0.3)
            clicks.push_back ((size_t) (seconds * sampleRate));
        for (const auto click : clicks)
            input[click] = 1.0f;
        const auto output = rig.run (input)[0];
        std::vector<double> times;
        for (const auto click : clicks)
            for (auto i = click; i < click + (size_t) (0.08 * sampleRate); ++i)
                if (std::abs (output[i]) > 0.05f)
                {
                    times.push_back ((double) (i - click) / sampleRate * 1000.0);
                    break;
                }
        return times;
    };
    const auto spreadOf = [] (const std::vector<double>& times)
    {
        const auto [low, high] = std::minmax_element (times.begin(), times.end());
        return *high - *low;
    };

    SECTION ("Its timing moves by several milliseconds, within DRIFT's 6 ms either way")
    {
        const auto steady = arrivals (0.0f), loose = arrivals (1.0f);
        REQUIRE (steady.size() == 20);
        REQUIRE (loose.size() == 20);
        CHECK (spreadOf (steady) < 0.5);
        CHECK (spreadOf (loose) > 4.0);
        for (const auto ms : loose)
            CHECK_THAT (ms, WithinAbs (40.0, 7.0));
    }

    SECTION ("Its tuning wanders a few cents, never a warble")
    {
        DoublerRig rig;
        rig.setVoice ("Drift", 1.0f);
        rig.setVoice ("Delay", 40.0f);
        const auto output = rig.run (sungVowel (220.0, 6.0))[0];
        std::vector<double> pitches;
        for (auto seconds = 0.5; seconds <= 6.0; seconds += 0.25)
            pitches.push_back (centsBetween (pitchOf ({ output.begin(), output.begin() + (long) (seconds * sampleRate) }), 220.0));
        const auto [low, high] = std::minmax_element (pitches.begin(), pitches.end());
        CHECK (*high - *low > 3.0);    // it wanders...
        CHECK (*high < 15.0);          // ...by a few cents: DRIFT's 6, and a little more while
        CHECK (*low > -15.0);          // the timing glides (a singer's take drifts this much)
    }

    SECTION ("Each voice wanders its own way")
    {
        DoublerRig rig;
        for (int voice = 0; voice < 2; ++voice)
        {
            rig.setVoice ("On", 1.0f, voice);
            rig.setVoice ("Gain", 0.0f, voice);
            rig.setVoice ("Detune", 0.0f, voice);
            rig.setVoice ("Delay", 40.0f, voice);
            rig.setVoice ("Drift", 1.0f, voice);
            rig.setVoice ("Pan", voice == 0 ? -1.0f : 1.0f, voice);
        }
        const auto output = rig.run (sungVowel (220.0, 3.0));
        std::vector<float> difference (output[0].size());
        for (size_t i = 0; i < difference.size(); ++i)
            difference[i] = output[0][i] - output[1][i];
        CHECK (rms (difference, 1.0) > 0.3f * rms (output[0], 1.0));   // same settings, different takes
    }
}

TEST_CASE ("Projects saved with 0.7.0's DOUBLER load without DRIFT, as they sounded", "[effects][doubler]")
{
    // As 0.7.0 or 0.8.0 saved it: parameterVersion 30, no Drift parameters.
    EffectRig original;
    original.set (ParamIDs::doublerVoice (0, "Gain").toRawUTF8(), -6.0f);
    juce::MemoryBlock saved;
    original.processor.getStateInformation (saved);
    auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
    REQUIRE (xml != nullptr);
    xml->setAttribute ("parameterVersion", 30);
    for (int voice = 0; voice < effects::Doubler::numVoices; ++voice)
        xml->removeChildElement (xml->getChildByAttribute ("id", ParamIDs::doublerVoice (voice, "Drift")), true);
    juce::MemoryBlock older;
    juce::AudioProcessor::copyXmlToBinary (*xml, older);

    EffectRig rig;
    rig.processor.setStateInformation (older.getData(), (int) older.getSize());
    const auto voice = [&rig] (int v, const char* part)
    {
        return rig.processor.getParameters().getRawParameterValue (ParamIDs::doublerVoice (v, part))->load();
    };
    for (int v = 0; v < effects::Doubler::numVoices; ++v)
        CHECK (voice (v, "Drift") == 0.0f);
    CHECK_THAT (voice (0, "Gain"), WithinAbs (-6.0, 0.01));   // as saved
}

TEST_CASE ("DOUBLER on a mono track: the takes at their full level", "[effects][doubler]")
{
    // Panned hard left, on a mono output: as loud as its GAIN says, not half.
    const auto note = sungVowel (220.0, 1.0);
    DoublerRig rig (1);
    rig.setVoice ("Pan", -1.0f);
    const auto output = rig.run (note);
    REQUIRE (output.size() == 1);
    CHECK_THAT (dB (rms (output[0], 0.3) / rms (note, 0.3)), WithinAbs (0.0, 1.0));

    // And your own voice too, panned or not.
    DoublerRig direct (1);
    direct.setVoice ("On", 0.0f);
    direct.set (ParamIDs::doublerDirectOn, 1.0f);
    direct.set (ParamIDs::doublerDirectPan, 1.0f);
    CHECK (maxAbsDifference (note, direct.run (note)[0], (size_t) (0.1 * sampleRate)) < 1.0e-6f);
}

TEST_CASE ("DOUBLER: the direct voice, and the shelves on the doubles", "[effects][doubler]")
{
    const auto note = sungVowel (220.0, 1.0);

    SECTION ("With every voice off, the direct voice comes through exactly")
    {
        DoublerRig rig;
        rig.set (ParamIDs::doublerDirectOn, 1.0f);
        rig.setVoice ("On", 0.0f);
        const auto output = rig.run (note);
        CHECK (maxAbsDifference (note, output[0], (size_t) (0.1 * sampleRate)) < 1.0e-6f);
        CHECK (maxAbsDifference (note, output[1], (size_t) (0.1 * sampleRate)) < 1.0e-6f);
    }

    SECTION ("DIRECT's PAN and GAIN")
    {
        DoublerRig rig;
        rig.set (ParamIDs::doublerDirectOn, 1.0f);
        rig.set (ParamIDs::doublerDirectPan, 1.0f);
        rig.set (ParamIDs::doublerDirectGain, -6.0f);
        rig.setVoice ("On", 0.0f);
        const auto output = rig.run (note);
        CHECK (rms (output[0], 0.2) < 1.0e-5f);
        CHECK_THAT (dB (rms (output[1], 0.2) / rms (note, 0.2)), WithinAbs (-6.0, 0.2));
    }

    SECTION ("The HIGH shelf cuts the doubles' highs, not your own voice")
    {
        const auto highsOf = [] (bool voices, float highShelfDb)
        {
            DoublerRig rig;
            rig.set (ParamIDs::doublerDirectOn, voices ? 0.0f : 1.0f);
            rig.setVoice ("On", voices ? 1.0f : 0.0f);
            rig.set (ParamIDs::doublerHighGain, highShelfDb);
            return levelAt (rig.run (sine (5000.0, 0.2f, 0.6))[0], 5000.0, 0.2);
        };
        CHECK_THAT (dB (highsOf (true, -12.0f) / highsOf (true, 0.0f)), WithinAbs (-12.0, 1.0));
        CHECK_THAT (dB (highsOf (false, -12.0f) / highsOf (false, 0.0f)), WithinAbs (0.0, 0.01));
    }

    SECTION ("Off, it's exactly the input")
    {
        EffectRig rig;
        rig.set (ParamIDs::doublerOutput, -12.0f);   // set up, but off
        CHECK (maxAbsDifference (note, rig.run (note)[0]) < 1.0e-6f);
    }
}

//==============================================================================
// MARS stage 2: the modulation, heard through the whole plugin.
namespace
{
    using Mod = effects::MarsModulation;

    struct ModulatedMarsRig : MarsRig
    {
        ModulatedMarsRig()
        {
            // A clean band, so what changes is the modulation, not the saturation.
            band (0, "Style", (float) effects::Mars::Style::cleanTube);
            band (0, "Drive", 0.0f);
        }
        void slot (int index, int source, int target, float amount)
        {
            set (ParamIDs::marsSlot (index, "Source").toRawUTF8(), (float) source);
            set (ParamIDs::marsSlot (index, "Target").toRawUTF8(), (float) target);
            set (ParamIDs::marsSlot (index, "Amount").toRawUTF8(), amount);
        }
        void lfo (int index, const char* part, float value) { set (ParamIDs::marsLfo (index, part).toRawUTF8(), value); }
    };

    // The level every 10 ms, in dB, from `fromSeconds` on.
    std::vector<float> envelopeDb (const std::vector<float>& samples, double fromSeconds)
    {
        std::vector<float> levels;
        const auto window = (size_t) (0.01 * sampleRate);
        for (auto start = (size_t) (fromSeconds * sampleRate); start + window <= samples.size(); start += window)
        {
            double sum = 0.0;
            for (size_t i = start; i < start + window; ++i)
                sum += samples[i] * samples[i];
            levels.push_back (dB ((float) std::sqrt (sum / (double) window) + 1.0e-9f));
        }
        return levels;
    }

    // How many times a level wobbles up through its middle.
    int wobbles (const std::vector<float>& levels)
    {
        const auto [low, high] = std::minmax_element (levels.begin(), levels.end());
        const auto middle = 0.5f * (*low + *high);
        int count = 0;
        for (size_t i = 1; i < levels.size(); ++i)
            count += levels[i - 1] < middle && levels[i] >= middle;
        return count;
    }
}

TEST_CASE ("MARS modulation: an XLFO on a band's LEVEL makes it pulse at RATE", "[effects][mars][modulation]")
{
    ModulatedMarsRig rig;
    rig.lfo (0, "Rate", 4.0f);
    rig.slot (0, Mod::lfo1, Mod::bandTarget (0, Mod::level), 0.5f);
    const auto output = rig.runMono (sine (440.0, 0.1f, 2.3));
    REQUIRE (allFinite (output));
    const auto levels = envelopeDb (output, 0.3);
    const auto [low, high] = std::minmax_element (levels.begin(), levels.end());
    CAPTURE (*low, *high);
    // LEVEL's knob runs -36 to +12 dB; at 0 dB, +-50% swings it from -24 dB up to +12.
    CHECK_THAT (*high - *low, WithinAbs (36.0, 3.0));
    CHECK (wobbles (levels) == 8);   // 4 a second for 2 seconds
}

TEST_CASE ("MARS modulation: a synced XLFO follows the DAW's tempo", "[effects][mars][modulation]")
{
    const auto wobblesAt = [] (double bpm)
    {
        ModulatedMarsRig rig;
        rig.playHead.bpm = bpm;
        rig.lfo (0, "Sync", (float) Mod::syncNames.indexOf ("1/4"));
        rig.slot (0, Mod::lfo1, Mod::bandTarget (0, Mod::level), 0.5f);
        return wobbles (envelopeDb (rig.runMono (sine (440.0, 0.1f, 4.3)), 0.3));
    };
    CHECK (wobblesAt (120.0) == 8);   // a cycle a beat: 2 a second
    CHECK (wobblesAt (90.0) == 6);
}

TEST_CASE ("MARS modulation: empty slots change nothing", "[effects][mars][modulation]")
{
    const auto input = sine (440.0, 0.3f, 0.5);
    ModulatedMarsRig plain;
    const auto reference = plain.runMono (input);
    ModulatedMarsRig noSource, noAmount;
    noSource.slot (0, Mod::none, Mod::bandTarget (0, Mod::level), 1.0f);
    noAmount.slot (0, Mod::lfo1, Mod::bandTarget (0, Mod::level), 0.0f);
    CHECK (maxAbsDifference (reference, noSource.runMono (input)) == 0.0f);
    CHECK (maxAbsDifference (reference, noAmount.runMono (input)) == 0.0f);
}

TEST_CASE ("MARS modulation: busy from the first moment, the knobs start where they're set", "[effects][mars][modulation]")
{
    // A slot that moves LEVEL by at most half a dB: the start must sound like
    // the unmodulated band, not glide up from every knob at 0 (as 0.15.0 did
    // at first, which also made a hair-off-0% slot audible on macOS).
    const auto input = sine (440.0, 0.3f, 0.1);
    ModulatedMarsRig plain;
    plain.band (0, "Mix", 0.8f);
    const auto reference = plain.runMono (input);
    ModulatedMarsRig busy;
    busy.band (0, "Mix", 0.8f);
    busy.slot (0, Mod::lfo1, Mod::bandTarget (0, Mod::level), 0.01f);
    CHECK (maxAbsDifference (reference, busy.runMono (input)) < 0.02f);
}

TEST_CASE ("MARS modulation: an envelope follower on DRIVE saturates the loud parts harder", "[effects][mars][modulation]")
{
    // The third harmonic against the note, in dB.
    const auto grit = [] (bool followed, float amplitude)
    {
        ModulatedMarsRig rig;
        rig.band (0, "Style", (float) effects::Mars::Style::saturatedTube);
        if (followed)
            rig.slot (0, Mod::ef1, Mod::bandTarget (0, Mod::drive), 1.0f);
        const auto output = rig.runMono (sine (330.0, amplitude, 0.8));
        return dB (levelAt (output, 990.0, 0.4) / levelAt (output, 330.0, 0.4));
    };
    CHECK (grit (true, 0.5f) > grit (false, 0.5f) + 10.0f);
    CHECK (grit (true, 0.5f) > grit (true, 0.02f) + 6.0f);
}

TEST_CASE ("MARS modulation: the expression pedal as a source", "[effects][mars][modulation]")
{
    const auto levelWithPedal = [] (float position)
    {
        ModulatedMarsRig rig;
        rig.set (ParamIDs::expression, position);
        rig.slot (0, Mod::pedal, Mod::globalTarget (Mod::output), 0.5f);
        return rms (rig.runMono (sine (440.0, 0.01f, 0.5)), 0.3);
    };
    // OUTPUT's knob runs -24 to +24 dB: half of that range is 24 dB.
    CHECK_THAT (dB (levelWithPedal (1.0f) / levelWithPedal (0.0f)), WithinAbs (24.0, 1.0));
}

TEST_CASE ("MARS modulation: MIDI from the DAW (the mod wheel) turns a band down", "[effects][mars][modulation]")
{
    ModulatedMarsRig rig;
    rig.slot (0, Mod::midi1, Mod::bandTarget (0, Mod::level), -1.0f);   // MIDI 1 starts on the mod wheel
    const auto input = sine (440.0, 0.1f, 1.0);
    std::vector<float> output;
    juce::AudioBuffer<float> buffer (2, blockSize);
    for (size_t start = 0; start + blockSize <= input.size(); start += blockSize)
    {
        juce::MidiBuffer midi;
        if (start == (size_t) (0.5 * sampleRate) / blockSize * blockSize)
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, 127), 0);
        buffer.clear();
        std::copy (input.begin() + (long) start, input.begin() + (long) start + blockSize, buffer.getWritePointer (0));
        rig.processor.processBlock (buffer, midi);
        output.insert (output.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
    }
    CHECK (rms (output, 0.3, 0.49) > 0.05f);
    CHECK (rms (output, 0.6) < 1.0e-4f);   // the wheel all the way up: the band all the way down (off)
}

TEST_CASE ("MARS modulation: a swept crossover moves where the bands split", "[effects][mars][modulation]")
{
    // Two bands, the top one off: MARS passes only what's below the
    // crossover. Sweeping the crossover makes a 1 kHz tone come and go.
    ModulatedMarsRig rig;
    rig.set (ParamIDs::marsBands, 1.0f);
    rig.band (1, "Level", effects::Mars::minLevelDb);
    rig.set (ParamIDs::marsCross1, 1000.0f);
    rig.lfo (0, "Rate", 2.0f);
    rig.slot (0, Mod::lfo1, Mod::globalTarget (Mod::crossover1), 0.3f);
    const auto output = rig.runMono (sine (1000.0, 0.1f, 2.3));
    REQUIRE (allFinite (output));
    const auto levels = envelopeDb (output, 0.3);
    const auto [low, high] = std::minmax_element (levels.begin(), levels.end());
    CHECK (*high > -24.0f);   // the tone all through (a 0.1 sine is -23 dB)
    CHECK (*low < *high - 20.0f);
    CHECK (wobbles (levels) == 4);
}

TEST_CASE ("MARS with all 16 slots busy still runs comfortably faster than real time", "[effects][performance][mars][modulation]")
{
    ModulatedMarsRig rig;
    rig.set (ParamIDs::marsBands, 3.0f);
    for (int b = 0; b < 4; ++b)
    {
        rig.band (b, "Style", (float) effects::Mars::Style::screamingAmp);
        rig.band (b, "Drive", 0.6f);
    }
    // Every slot moving something that costs: drive, tone, crossovers.
    const int targets[] = { Mod::bandTarget (0, Mod::drive), Mod::bandTarget (1, Mod::bass), Mod::bandTarget (2, Mod::treble),
                            Mod::bandTarget (3, Mod::presence), Mod::globalTarget (Mod::crossover1), Mod::globalTarget (Mod::crossover2),
                            Mod::globalTarget (Mod::crossover3), Mod::bandTarget (0, Mod::feedbackFreq) };
    for (int s = 0; s < Mod::numSlots; ++s)
        rig.slot (s, s % 2 == 0 ? Mod::lfo1 : Mod::ef1, targets[s % 8], 0.2f);
    const auto input = sine (220.0, 0.3f, 4.0);
    const auto startMs = juce::Time::getMillisecondCounterHiRes();
    const auto output = rig.runMono (input);
    const auto fraction = (juce::Time::getMillisecondCounterHiRes() - startMs) / 1000.0 / 4.0;
    WARN ("MARS, 4 bands, 16 slots: " << juce::String (fraction * 100.0, 1) << "% of one core");
    CHECK (allFinite (output));
    CHECK (fraction < 0.3);
}


//==============================================================================
// HARMONY stage 2: CORRECT, pitch correction of the singer's own voice.
namespace
{
    struct CorrectRig : EffectRig
    {
        explicit CorrectRig (effects::Harmony::Correct mode, int key = 0, Scales::Scale scale = Scales::Scale::major)
        {
            set (ParamIDs::harmonyCorrect, (float) mode);
            set (ParamIDs::harmonyKey, (float) key);
            set (ParamIDs::harmonyScale, (float) scale);
            processor.updateLatency();   // the timer's job in a DAW
        }
    };

    double cents (double hz, double reference) { return 1200.0 * std::log2 (hz / reference); }

    // When a signal first gets loud (seconds), or -1.
    double onsetOf (const std::vector<float>& samples, float threshold = 0.01f)
    {
        for (size_t i = 0; i < samples.size(); ++i)
            if (std::abs (samples[i]) > threshold)
                return (double) i / sampleRate;
        return -1.0;
    }
}

TEST_CASE ("HARMONY CORRECT: your voice tuned to the nearest note in the key", "[effects][harmony][correct]")
{
    using Correct = effects::Harmony::Correct;
    const auto A3 = 220.0, D4 = 293.665;
    const auto flatA = A3 * std::pow (2.0, -30.0 / 1200.0);

    SECTION ("A singer 30 cents flat on an A comes out on the A, NATURAL or HARD; OFF leaves them flat")
    {
        for (const auto mode : { Correct::natural, Correct::hard })
        {
            CorrectRig rig (mode);
            const auto out = pitchOf (rig.runMono (sungVowel (flatA, 1.0)));
            CAPTURE ((int) mode, out);
            CHECK (std::abs (cents (out, A3)) < 5.0);
        }
        CorrectRig off (Correct::off);
        CHECK (std::abs (cents (pitchOf (off.runMono (sungVowel (flatA, 1.0))), flatA)) < 3.0);
    }

    SECTION ("A note outside the key goes to the nearest one in it: C# (a little sharp) in C major becomes D")
    {
        CorrectRig rig (Correct::hard);
        const auto out = pitchOf (rig.runMono (sungVowel (277.183 * std::pow (2.0, 10.0 / 1200.0), 1.0)));
        CAPTURE (out);
        CHECK (std::abs (cents (out, D4)) < 5.0);
    }

    SECTION ("The KEY decides: the same C# in A major is in the key, so it's only pulled to C# itself")
    {
        CorrectRig rig (Correct::hard, 9 /* A */);
        const auto out = pitchOf (rig.runMono (sungVowel (277.183 * std::pow (2.0, 25.0 / 1200.0), 1.0)));
        CAPTURE (out);
        CHECK (std::abs (cents (out, 277.183)) < 5.0);
    }

    SECTION ("SCALE Chromatic (no key needed): every note counts, so C# is only pulled onto C# itself")
    {
        CorrectRig rig (Correct::hard, 0, Scales::Scale::chromatic);
        const auto out = pitchOf (rig.runMono (sungVowel (277.183 * std::pow (2.0, 25.0 / 1200.0), 1.0)));
        CHECK (std::abs (cents (out, 277.183)) < 5.0);
    }

    SECTION ("The voice keeps its vowel: the resonance stays where it was")
    {
        // A strong resonance at 800 Hz: after a 40-cent correction, the
        // overtones near it are still the loudest (as they would be if the
        // formants moved too, but 40 cents moves them only 2%: so check the
        // level around 800 Hz holds rather than dropping).
        CorrectRig rig (Correct::hard);
        const auto out = rig.runMono (sungVowel (flatA, 1.0, 800.0));
        const auto in = sungVowel (flatA, 1.0, 800.0);
        const auto bandLevel = [] (const std::vector<float>& s, double fundamental)
        {
            auto sum = 0.0f;
            for (int h = 3; h <= 4; ++h)   // the overtones either side of 800 Hz
                sum += levelAt (s, h * fundamental, 0.5);
            return sum;
        };
        CHECK_THAT (dB (bandLevel (out, A3) / bandLevel (in, flatA)), WithinAbs (0.0, 2.0));
    }
}

TEST_CASE ("HARMONY CORRECT: NATURAL glides to the note, HARD jumps", "[effects][harmony][correct]")
{
    using Correct = effects::Harmony::Correct;
    // How long the correction takes from first moving to 90% of the way (seconds).
    const auto settleTime = [] (Correct mode)
    {
        effects::Harmony harmony;
        harmony.prepare (sampleRate, blockSize);
        effects::Harmony::Settings settings;
        settings.correct = mode;
        settings.latency = (int) (effects::Harmony::correctLatencySeconds * sampleRate);
        const auto voice = sungVowel (220.0 * std::pow (2.0, -40.0 / 1200.0), 1.0);
        juce::AudioBuffer<float> buffer (2, blockSize);
        std::vector<float> trace;
        for (size_t start = 0; start + blockSize <= voice.size(); start += blockSize)
        {
            for (int ch = 0; ch < 2; ++ch)
                buffer.copyFrom (ch, 0, voice.data() + start, blockSize);
            juce::dsp::AudioBlock<float> block (buffer);
            harmony.process (block, voice.data() + start, settings);
            trace.push_back (harmony.getCorrection());
        }
        const auto final = trace.back();
        int first = -1, reached = -1;
        for (int i = 0; i < (int) trace.size(); ++i)
        {
            if (first < 0 && std::abs (trace[(size_t) i]) > 0.01f)
                first = i;
            if (reached < 0 && std::abs (trace[(size_t) i]) >= 0.9f * std::abs (final))
                reached = i;
        }
        CAPTURE (final, first, reached);
        CHECK_THAT (final, WithinAbs (0.4, 0.03));   // 40 cents up, onto the A
        return (reached - first) * blockSize / sampleRate;
    };
    const auto hard = settleTime (Correct::hard), natural = settleTime (Correct::natural);
    CAPTURE (hard, natural);
    CHECK (hard < 0.02);
    CHECK (natural > 0.08);
    CHECK (natural < 0.3);
}

TEST_CASE ("HARMONY CORRECT: the plugin runs 40 ms late only while it's on, and says so", "[effects][harmony][correct]")
{
    using Correct = effects::Harmony::Correct;
    std::vector<float> input ((size_t) (0.2 * sampleRate), 0.0f);
    const auto note = sungVowel (220.0, 0.5);
    input.insert (input.end(), note.begin(), note.end());

    CorrectRig off (Correct::off);
    CHECK (off.processor.getLatencySamples() == 0);
    CHECK_THAT (onsetOf (off.runMono (input)), WithinAbs (0.2, 0.002));

    CorrectRig on (Correct::natural);
    CHECK (on.processor.getLatencySamples() == juce::roundToInt (0.04 * sampleRate));
    // Within one vocal pulse (4.5 ms at 220 Hz) of the 40 ms it reports.
    CHECK_THAT (onsetOf (on.runMono (input)), WithinAbs (0.24, 0.006));

    // Switched back off: no delay again.
    on.set (ParamIDs::harmonyCorrect, (float) Correct::off);
    on.processor.updateLatency();
    CHECK (on.processor.getLatencySamples() == 0);
}

TEST_CASE ("HARMONY CORRECT: works with HARMONY off, and harmonies stay in tune with it", "[effects][harmony][correct]")
{
    using Correct = effects::Harmony::Correct;
    const auto flatA = 220.0 * std::pow (2.0, -30.0 / 1200.0);

    SECTION ("HARMONY off: only your voice, tuned, at full level")
    {
        CorrectRig rig (Correct::natural);
        rig.set (ParamIDs::harmonyOn, 0.0f);
        const auto input = sungVowel (flatA, 1.0);
        const auto out = rig.runMono (input);
        CHECK (std::abs (cents (pitchOf (out), 220.0)) < 5.0);
        CHECK_THAT (dB (rms (out, 0.5) / rms (input, 0.5)), WithinAbs (0.0, 1.0));
    }

    SECTION ("HARMONY on: a fifth above the tuned voice, in tune, in time with it")
    {
        HarmonyRig rig (7.0f);
        rig.set (ParamIDs::harmonyCorrect, (float) Correct::natural);
        rig.processor.updateLatency();
        const auto out = rig.runMono (sungVowel (flatA, 1.0));
        CHECK (std::abs (cents (pitchOf (out), 329.628)) < 5.0);
    }
}

//==============================================================================
// HARMONY stage 3: GENERATE NOTES and CHORD maps.
namespace
{
    // HARMONY on, its fixed voices and the singer's own voice out of the
    // way, GENERATE NOTES on with `chord`: the output is the note voices alone.
    struct NotesRig : EffectRig
    {
        explicit NotesRig (const char* chord, int key = 0, Scales::Scale scale = Scales::Scale::major)
        {
            set (ParamIDs::harmonyOn, 1.0f);
            set (ParamIDs::harmonySource, effects::Harmony::silentDb);
            set (ParamIDs::harmonyKey, (float) key);
            set (ParamIDs::harmonyScale, (float) scale);
            for (int v = 0; v < effects::Harmony::numVoices; ++v)
                set (ParamIDs::harmonyVoice (v, "Active").toRawUTF8(), 0.0f);
            set (ParamIDs::harmonyGenerate, 1.0f);
            set (ParamIDs::harmonyChord, (float) effects::Harmony::chordMapNames().indexOf (chord));
            set (ParamIDs::harmonyNoteLevel, 0.0f);
            set (ParamIDs::harmonyNoteWidth, 0.0f);
        }
    };

    // A voice singing `hz` for each stretch in turn (seconds).
    std::vector<float> sungLine (std::initializer_list<std::pair<double, double>> stretches)
    {
        std::vector<float> line;
        for (const auto& [hz, seconds] : stretches)
        {
            const auto part = hz > 0.0 ? sungVowel (hz, seconds) : silence (seconds);
            line.insert (line.end(), part.begin(), part.end());
        }
        return line;
    }

    double semitonesUp (double hz, double semitones) { return hz * std::pow (2.0, semitones / 12.0); }
}

TEST_CASE ("HARMONY GENERATE NOTES: each sung note becomes the CHORD map's notes, in the key", "[effects][harmony][notes]")
{
    const auto A3 = 220.0, C4 = 261.626, E4 = 329.628, Csharp4 = 277.183, F4 = 349.228;

    SECTION ("3rd + 5th Above on an A in C major: C and E (the minor third, in the key), nothing between")
    {
        NotesRig rig ("3rd + 5th Above");
        const auto out = rig.runMono (sungVowel (A3, 1.0));
        // Each note voice is about as loud as the sung note's own fundamental.
        const auto sung = levelAt (sungVowel (A3, 1.0), A3, 0.5);
        CAPTURE (sung, levelAt (out, C4, 0.5), levelAt (out, E4, 0.5), levelAt (out, Csharp4, 0.5));
        CHECK (levelAt (out, C4, 0.5) > 0.5f * sung);
        CHECK (levelAt (out, E4, 0.5) > 0.5f * sung);
        CHECK (levelAt (out, Csharp4, 0.5) < 0.2f * levelAt (out, C4, 0.5));   // not the major third: out of the key
        CHECK (levelAt (out, F4, 0.5) < 0.2f * levelAt (out, E4, 0.5));
    }

    SECTION ("The same in A major: C#, the major third")
    {
        NotesRig rig ("3rd + 5th Above", 9 /* A */);
        const auto out = rig.runMono (sungVowel (A3, 1.0));
        CHECK (levelAt (out, Csharp4, 0.5) > 0.5f * levelAt (sungVowel (A3, 1.0), A3, 0.5));
        CHECK (levelAt (out, C4, 0.5) < 0.2f * levelAt (out, Csharp4, 0.5));
    }

    SECTION ("Note voices play their notes exactly, though the singer is 30 cents flat")
    {
        NotesRig rig ("5th Above");
        const auto out = rig.runMono (sungVowel (semitonesUp (A3, -0.3), 1.0));
        CAPTURE (pitchOf (out));
        CHECK (std::abs (cents (pitchOf (out), E4)) < 5.0);
    }

    SECTION ("GENERATE NOTES off, or HARMONY off: no note voices")
    {
        NotesRig off ("5th Above");
        off.set (ParamIDs::harmonyGenerate, 0.0f);
        CHECK (rms (off.runMono (sungVowel (A3, 0.6)), 0.3) < 1.0e-4f);
        NotesRig harmonyOff ("5th Above");
        harmonyOff.set (ParamIDs::harmonyOn, 0.0f);
        harmonyOff.set (ParamIDs::harmonySource, 0.0f);
        const auto input = sungVowel (A3, 0.6);
        CHECK (maxAbsDifference (input, harmonyOff.runMono (input), (size_t) (0.1 * sampleRate)) < 1.0e-5f);
    }

    SECTION ("Below MIN NOTE: no notes")
    {
        NotesRig rig ("5th Above");
        rig.set (ParamIDs::harmonyMinNote, 48.0f);   // C3
        CHECK (rms (rig.runMono (sungVowel (110.0 /* A2 */, 0.6)), 0.3) < 1.0e-4f);
    }

    SECTION ("ATTACK fades the note voices in; a silence ends them")
    {
        const auto risingAt = [A3] (float attackMs)
        {
            NotesRig rig ("5th Above");
            rig.set (ParamIDs::harmonyNoteAttack, attackMs);
            const auto out = rig.runMono (sungLine ({ { A3, 1.0 }, { 0.0, 0.5 } }));
            CHECK (rms (out, 1.2, 1.5) < 1.0e-4f);   // the singer stopped: so did they
            return dB (rms (out, 0.1, 0.15) / rms (out, 0.8, 0.9));
        };
        CHECK (risingAt (5.0f) > -2.0f);       // in at once
        CHECK (risingAt (500.0f) < -6.0f);     // still fading in
    }
}

TEST_CASE ("HARMONY GENERATE NOTES: TOLERANCE decides when a new note starts", "[effects][harmony][notes]")
{
    // The notes generated as a line is sung (chromatic, so every semitone counts).
    const auto notesOf = [] (const std::vector<float>& line, float toleranceCents, float toleranceMs)
    {
        effects::Harmony harmony;
        harmony.prepare (sampleRate, blockSize);
        effects::Harmony::Settings settings;
        settings.on = true;
        settings.scale = Scales::Scale::chromatic;
        settings.notes.generate = true;
        settings.notes.toleranceCents = toleranceCents;
        settings.notes.toleranceMs = toleranceMs;
        juce::AudioBuffer<float> buffer (2, blockSize);
        std::vector<int> notes;
        for (size_t start = 0; start + blockSize <= line.size(); start += blockSize)
        {
            for (int ch = 0; ch < 2; ++ch)
                buffer.copyFrom (ch, 0, line.data() + start, blockSize);
            juce::dsp::AudioBlock<float> block (buffer);
            harmony.process (block, line.data() + start, settings);
            const auto note = harmony.getGeneratedNote();
            if (note >= 0 && (notes.empty() || notes.back() != note))
                notes.push_back (note);
        }
        return notes;
    };
    const auto A3 = 220.0;

    SECTION ("A wobble inside 50 cents keeps the note")
    {
        CHECK (notesOf (sungLine ({ { A3, 0.4 }, { semitonesUp (A3, 0.35), 0.4 }, { A3, 0.4 } }), 50.0f, 100.0f) == std::vector<int> { 57 });
    }

    SECTION ("A move past 50 cents for longer than TIME is a new note; a quick one isn't")
    {
        CHECK (notesOf (sungLine ({ { A3, 0.4 }, { semitonesUp (A3, 1.0), 0.4 } }), 50.0f, 100.0f) == std::vector<int> { 57, 58 });
        CHECK (notesOf (sungLine ({ { A3, 0.4 }, { semitonesUp (A3, 1.0), 0.06 }, { A3, 0.4 } }), 50.0f, 150.0f) == std::vector<int> { 57 });
    }

    SECTION ("TOLERANCE Off: only a silence starts a new note")
    {
        CHECK (notesOf (sungLine ({ { A3, 0.4 }, { semitonesUp (A3, 2.0), 0.4 } }), effects::Harmony::toleranceOff, 100.0f) == std::vector<int> { 57 });
        CHECK (notesOf (sungLine ({ { A3, 0.4 }, { 0.0, 0.3 }, { semitonesUp (A3, 2.0), 0.4 } }), effects::Harmony::toleranceOff, 100.0f)
               == std::vector<int> { 57, 59 });
    }
}

TEST_CASE ("HARMONY CORRECT NOTES: the voice tuned hard to the generated note", "[effects][harmony][notes][correct]")
{
    CorrectRig rig (effects::Harmony::Correct::notes);
    const auto out = rig.runMono (sungVowel (semitonesUp (220.0, 0.3), 1.0));   // 30 cents sharp of A
    CAPTURE (pitchOf (out));
    CHECK (std::abs (cents (pitchOf (out), 220.0)) < 5.0);
    CHECK (rig.processor.getLatencySamples() > 0);   // like NATURAL and HARD, it runs 40 ms late
}
TEST_CASE ("AEON: the flanger follows the BF-2's circuit and specifications", "[effects][modulation]")
{
    using effects::Aeon;
    const auto makeRig = [] (float manual, float depth, float res, float rateHz = 0.5f)
    {
        auto rig = std::make_unique<EffectRig>();
        rig->set (ParamIDs::modOn, 1.0f);
        rig->set (ParamIDs::modType, (float) effects::Modulation::Type::flanger);
        rig->set (ParamIDs::modManual, manual);
        rig->set (ParamIDs::modDepth, depth);
        rig->set (ParamIDs::modRes, res);
        rig->set (ParamIDs::modRate, rateHz);
        return rig;
    };

    // With DEPTH at 0 the delay holds still: the response to a small click
    // (after a second, so everything has settled), as gain in dB per band.
    struct Band { double lowest, highest; };
    const auto impulseResponse = [&] (float manual, float res)
    {
        auto rig = makeRig (manual, 0.0f, res);
        rig->runMono (silence (1.0));
        return rig->runMono (impulse (1.0, 0.01f));
    };
    const auto spectrum = [] (const std::vector<float>& ir)
    {
        constexpr int order = 17;
        juce::dsp::FFT fft (order);
        std::vector<float> data ((size_t) (2 << order), 0.0f);
        for (size_t i = 0; i < std::min (ir.size(), (size_t) (1 << order)); ++i)
            data[i] = ir[i] / 0.01f;
        fft.performFrequencyOnlyForwardTransform (data.data());
        return data;   // bin i is i * sampleRate / 2^17 Hz
    };
    const auto bandExtremes = [] (const std::vector<float>& magnitudes, Band band)
    {
        const auto binHz = sampleRate / (double) (1 << 17);
        float highest = -1000.0f, lowest = 1000.0f;
        for (auto i = (size_t) (band.lowest / binHz); i < (size_t) (band.highest / binHz); ++i)
        {
            highest = std::max (highest, dB (magnitudes[i] + 1.0e-9f));
            lowest = std::min (lowest, dB (magnitudes[i] + 1.0e-9f));
        }
        return std::make_pair (lowest, highest);
    };

    SECTION ("MANUAL sets the delay across the pedal's specified 13 ms to 1 ms (DEPTH at 0)")
    {
        CHECK_THAT (Aeon::delayMsForControl (Aeon::manualVolts (0.0f), 0.0), WithinAbs (13.0, 0.01));
        CHECK_THAT (Aeon::delayMsForControl (Aeon::manualVolts (10.0f), 0.0), WithinAbs (1.0, 0.01));

        // The echo of a click lands there, plus about 0.15 ms through the circuit's filters.
        const auto echoMs = [&] (float manual)
        {
            const auto ir = impulseResponse (manual, 0.0f);
            size_t peak = 20;
            for (size_t i = 20; i < ir.size(); ++i)
                if (std::abs (ir[i]) > std::abs (ir[peak]))
                    peak = i;
            return 1000.0 * (double) peak / sampleRate;
        };
        CHECK_THAT (echoMs (0.0f), WithinAbs (13.15, 0.2));
        CHECK_THAT (echoMs (5.0f), WithinAbs (3.6, 0.2));
        CHECK_THAT (echoMs (10.0f), WithinAbs (1.15, 0.2));
    }

    SECTION ("Dry and wet mix in phase: deep notches between the peaks")
    {
        const auto magnitudes = spectrum (impulseResponse (5.0f, 0.0f));
        const auto [notch, peak] = bandExtremes (magnitudes, { 2000.0, 6000.0 });
        CHECK (notch < -30.0f);
        CHECK_THAT (peak, WithinAbs (6.0, 1.5));   // dry and wet adding up: about twice the level
    }

    SECTION ("The low end keeps more of its level: the wet path lets little through below about 40 Hz")
    {
        const auto magnitudes = spectrum (impulseResponse (5.0f, 0.0f));
        CHECK (bandExtremes (magnitudes, { 40.0, 150.0 }).first > -10.0f);
    }

    SECTION ("RES rings the peaks, most in the treble, and never howls")
    {
        const auto still = spectrum (impulseResponse (5.0f, 0.0f));
        const auto ringing = spectrum (impulseResponse (5.0f, 10.0f));
        CHECK (bandExtremes (ringing, { 600.0, 2000.0 }).second > bandExtremes (still, { 600.0, 2000.0 }).second + 3.0f);
        CHECK (bandExtremes (ringing, { 6000.0, 12000.0 }).second > bandExtremes (still, { 6000.0, 12000.0 }).second + 15.0f);

        // A click at full RES dies away rather than building up...
        const auto ir = impulseResponse (5.0f, 10.0f);
        CHECK (rms (ir, 0.9, 1.0) < 1.0e-4f * rms (ir, 0.0, 0.1));
        // ...and loud playing stays loud playing: IC2b's diodes hold the BBD's input down.
        auto rig = makeRig (10.0f, 1.0f, 10.0f, 3.0f);
        auto loud = sine (110.0, 1.0f, 3.0);
        for (size_t i = 0; i < loud.size(); ++i)
            loud[i] += 0.8f * (float) std::sin (0.37 * (double) i);
        const auto output = rig->runMono (loud);
        REQUIRE (allFinite (output));
        CHECK (*std::max_element (output.begin(), output.end()) < 3.0f);
    }

    SECTION ("Full DEPTH sweeps by the LFO alone: MANUAL does nothing, as the manual says")
    {
        const auto input = sine (700.0, 0.05f, 1.5);
        const auto atZero = makeRig (0.0f, 1.0f, 3.0f)->runMono (input);
        const auto atTen = makeRig (10.0f, 1.0f, 3.0f)->runMono (input);
        CHECK (maxAbsDifference (atZero, atTen) < 1.0e-5f);
        CHECK_THAT (Aeon::delayMsForControl (Aeon::lfoVolts (-1.0), 1.0), WithinAbs (9.1, 0.3));
        CHECK_THAT (Aeon::delayMsForControl (Aeon::lfoVolts (1.0), 1.0), WithinAbs (1.2, 0.1));
    }

    // A steady 1 kHz tone through a full-depth sweep: its loudness rises
    // and falls as the notches pass. Its envelope, in 10 ms steps.
    const auto envelope = [&] (float rateHz, double seconds)
    {
        auto rig = makeRig (5.0f, 1.0f, 0.0f, rateHz);
        const auto output = rig->runMono (sine (1000.0, 0.05f, seconds));
        std::vector<float> levels;
        for (double t = 1.0; t + 0.01 < seconds; t += 0.01)
            levels.push_back (rms (output, t, t + 0.01));
        return levels;
    };

    SECTION ("RATE: one sweep per cycle")
    {
        const auto levels = envelope (1.0f, 6.0);
        float mean = 0.0f;
        for (auto l : levels)
            mean += l / (float) levels.size();
        auto bestLag = 0;
        double best = -1.0e9;
        for (int lag = 70; lag <= 130; ++lag)   // 0.7 to 1.3 s
        {
            double sum = 0.0;
            for (size_t i = 0; i + (size_t) lag < levels.size(); ++i)
                sum += (levels[i] - mean) * (levels[i + (size_t) lag] - mean);
            if (sum > best)
            {
                best = sum;
                bestLag = lag;
            }
        }
        CHECK (std::abs (bestLag - 100) <= 3);
    }

    SECTION ("Fast sweeps are narrower: C32 (22 uF) smooths the sweep")
    {
        const auto swing = [] (const std::vector<float>& levels)
        {
            const auto [lowest, highest] = std::minmax_element (levels.begin(), levels.end());
            return dB (*highest / *lowest);
        };
        CHECK (swing (envelope (0.3f, 5.0)) > swing (envelope (8.0f, 5.0)) + 2.0f);
    }
}

//==============================================================================
// HARMONY stage 4: MIDI note input.
namespace
{
    // Runs `input` through `rig`, sending each MIDI message at its time (seconds).
    std::vector<float> runWithMidi (EffectRig& rig, const std::vector<float>& input,
                                    std::vector<std::pair<double, juce::MidiMessage>> events)
    {
        std::vector<float> output;
        juce::AudioBuffer<float> buffer (2, blockSize);
        for (size_t start = 0; start + blockSize <= input.size(); start += blockSize)
        {
            juce::MidiBuffer midi;
            for (const auto& [seconds, message] : events)
            {
                const auto at = (size_t) (seconds * sampleRate);
                if (at >= start && at < start + blockSize)
                    midi.addEvent (message, (int) (at - start));
            }
            buffer.clear();
            std::copy (input.begin() + (long) start, input.begin() + (long) start + blockSize, buffer.getWritePointer (0));
            rig.processor.processBlock (buffer, midi);
            output.insert (output.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
        }
        return output;
    }

    juce::MidiMessage keyDown (int note, float velocity = 1.0f) { return juce::MidiMessage::noteOn (1, note, velocity); }
    juce::MidiMessage keyUp (int note) { return juce::MidiMessage::noteOff (1, note); }

    // HARMONY on, only note voices heard, GENERATE off: what MIDI plays.
    struct MidiRig : NotesRig
    {
        explicit MidiRig (const char* chord = "3rd + 5th Above") : NotesRig (chord)
        {
            set (ParamIDs::harmonyGenerate, 0.0f);
        }
    };
}

TEST_CASE ("HARMONY MIDI: played keys become note voices", "[effects][harmony][midi]")
{
    const auto A3 = 220.0, C4 = 261.626, E4 = 329.628, G4 = 391.995;
    const auto sung = sungVowel (A3, 1.0);
    const auto sungLevel = levelAt (sung, A3, 0.5);

    SECTION ("AS PLAYED: C4 and E4 held while singing an A: exactly those two notes")
    {
        MidiRig rig;
        const auto out = runWithMidi (rig, sung, { { 0.0, keyDown (60) }, { 0.0, keyDown (64) } });
        CAPTURE (levelAt (out, C4, 0.5), levelAt (out, E4, 0.5), levelAt (out, G4, 0.5));
        CHECK (levelAt (out, C4, 0.5) > 0.5f * sungLevel);
        CHECK (levelAt (out, E4, 0.5) > 0.5f * sungLevel);
        CHECK (levelAt (out, G4, 0.5) < 0.2f * sungLevel);
    }

    SECTION ("AS PLAYED keeps an out-of-key key as played (F# in C major)")
    {
        MidiRig rig;
        const auto out = runWithMidi (rig, sung, { { 0.0, keyDown (66) } });
        CHECK (std::abs (cents (pitchOf (out), 369.994)) < 5.0);
    }

    SECTION ("THROUGH CHORD: an A key with 3rd + 5th Above plays C and E, in the key")
    {
        MidiRig rig;
        rig.set (ParamIDs::harmonyMidiMap, 1.0f);
        const auto out = runWithMidi (rig, sung, { { 0.0, keyDown (57) } });
        CHECK (levelAt (out, C4, 0.5) > 0.5f * sungLevel);
        CHECK (levelAt (out, E4, 0.5) > 0.5f * sungLevel);
    }

    SECTION ("No keys, or HARMONY off: no note voices")
    {
        MidiRig none;
        CHECK (rms (runWithMidi (none, sung, {}), 0.3) < 1.0e-4f);
        MidiRig off;
        off.set (ParamIDs::harmonyOn, 0.0f);
        off.set (ParamIDs::harmonySource, effects::Harmony::silentDb);
        const auto out = runWithMidi (off, sung, { { 0.0, keyDown (60) } });
        CHECK (maxAbsDifference (sung, out, (size_t) (0.1 * sampleRate)) < 1.0e-5f);   // HARMONY off: the voice untouched
    }

    SECTION ("Letting go fades the note out; the sustain pedal holds it")
    {
        MidiRig released;
        const auto out = runWithMidi (released, sung, { { 0.0, keyDown (60) }, { 0.5, keyUp (60) } });
        // RELEASE (150 ms to start) fades it: well down 0.3 s after letting go.
        const auto playing = rms (out, 0.3, 0.5);
        CHECK (playing > 0.002f);
        CHECK (dB (rms (out, 0.85, 1.0) / playing) < -15.0f);

        MidiRig pedalled;
        const auto held = runWithMidi (pedalled, sung, { { 0.0, juce::MidiMessage::controllerEvent (1, 64, 127) },
                                                         { 0.0, keyDown (60) }, { 0.5, keyUp (60) } });
        CHECK_THAT (dB (rms (held, 0.85, 1.0) / rms (held, 0.3, 0.5)), WithinAbs (0.0, 1.5));
    }

    SECTION ("Velocity: a soft key is quieter than a hard one")
    {
        const auto levelAtVelocity = [&] (float velocity)
        {
            MidiRig rig;
            return rms (runWithMidi (rig, sung, { { 0.0, keyDown (60, velocity) } }), 0.5);
        };
        CHECK (dB (levelAtVelocity (0.15f) / levelAtVelocity (1.0f)) < -6.0f);
    }
}

TEST_CASE ("HARMONY MIDI: TUNE VOICE's Notes follows the keyboard", "[effects][harmony][midi][correct]")
{
    // Singing an A with C4 held: the voice is pulled up to C4, the nearest held key.
    CorrectRig rig (effects::Harmony::Correct::notes);
    const auto out = runWithMidi (rig, sungVowel (220.0, 1.0), { { 0.0, keyDown (60) } });
    CAPTURE (pitchOf (out));
    CHECK (std::abs (cents (pitchOf (out), 261.626)) < 5.0);
}

//==============================================================================
// HARMONY stage 5: the Note Map Editor (CHORD Custom, SCALE Custom).
namespace
{
    // GENERATE NOTES through CHORD Custom: only the note voices heard.
    struct CustomMapRig : NotesRig
    {
        CustomMapRig() : NotesRig ("Custom") {}

        // What `noteClass` plays, in semitones from the sung note (as the
        // editor stores it: bit offset + 23, sixteen bits per parameter).
        void map (int noteClass, std::initializer_list<int> offsets)
        {
            std::uint64_t bits = 0;
            for (auto offset : offsets)
                bits |= std::uint64_t { 1 } << (offset + effects::Harmony::mapSpan);
            for (int part = 0; part < 3; ++part)
                set (ParamIDs::harmonyMap (noteClass, part).toRawUTF8(), (float) ((bits >> (16 * part)) & 0xffffu));
        }
    };
}

TEST_CASE ("HARMONY Note Map: CHORD Custom plays the notes you map", "[effects][harmony][notemap]")
{
    const auto A2 = 110.0, A3 = 220.0, Bb3 = 233.082, B3 = 246.942, C3 = 130.813, C4 = 261.626, D4 = 293.665,
               E4 = 329.628;
    const auto sung = sungVowel (A3, 1.0);
    const auto sungLevel = levelAt (sung, A3, 0.5);

    SECTION ("A mapped to C and E above: exactly those notes")
    {
        CustomMapRig rig;
        rig.map (9 /* A */, { 3, 7 });
        const auto out = rig.runMono (sung);
        CAPTURE (levelAt (out, C4, 0.5), levelAt (out, E4, 0.5), levelAt (out, D4, 0.5));
        CHECK (levelAt (out, C4, 0.5) > 0.5f * sungLevel);
        CHECK (levelAt (out, E4, 0.5) > 0.5f * sungLevel);
        CHECK (levelAt (out, D4, 0.5) < 0.2f * sungLevel);
    }

    SECTION ("Only the sung note's own row counts: an empty A plays nothing")
    {
        CustomMapRig rig;
        rig.map (0 /* C */, { 4, 7 });
        CHECK (rms (rig.runMono (sung), 0.3) < 1.0e-4f);
    }

    SECTION ("SPREAD follows the octave sung; FOLD stays where it was set")
    {
        // A plays 9 below. SPREAD: from the A sung. FOLD: from A4 (the
        // editor's octave), so C4 whether A3 or A2 is sung.
        const auto heard = [] (double hz, bool fold)
        {
            CustomMapRig rig;
            rig.map (9, { -9 });
            rig.set (ParamIDs::harmonyMapFold, fold ? 1.0f : 0.0f);
            return pitchOf (rig.runMono (sungVowel (hz, 1.0)));
        };
        const auto C2 = 65.406;
        CHECK (std::abs (cents (heard (A3, false), C3)) < 20.0);
        CHECK (std::abs (cents (heard (A2, false), C2)) < 20.0);
        CHECK (std::abs (cents (heard (A3, true), C4)) < 20.0);
        CHECK (std::abs (cents (heard (A2, true), C4)) < 20.0);
    }

    SECTION ("FOLD never asks a voice for more than two octaves: an octave nearer instead")
    {
        // A plays C above, set as C5 (A4 + 3); sung two octaves lower (A2)
        // that's 27 semitones: it plays C4 instead (15).
        CustomMapRig rig;
        rig.map (9, { 3 });
        rig.set (ParamIDs::harmonyMapFold, 1.0f);
        const auto pitch = pitchOf (rig.runMono (sungVowel (A2, 1.0)));
        CAPTURE (pitch);
        CHECK (std::abs (cents (pitch, C4)) < 20.0);
        std::array<int, effects::Harmony::mapBits> notes {};
        effects::Harmony::NoteMap map {};
        map[9] = (std::uint64_t { 1 } << (effects::Harmony::mapSpan + 23)) | (std::uint64_t { 1 } << (effects::Harmony::mapSpan - 23));
        for (int sung = 24; sung < 96; ++sung)
        {
            const auto count = effects::Harmony::customNotes (map, true, sung - (sung % 12) + 9, notes);
            for (int i = 0; i < count; ++i)
                CHECK (std::abs (notes[(size_t) i] - (sung - (sung % 12) + 9)) <= effects::Harmony::maxShift);
        }
    }

    SECTION ("SCALE Custom: a note set to Up or Down moves; In stays")
    {
        const auto playing = [&] (int box)
        {
            CustomMapRig rig;
            rig.set (ParamIDs::harmonyScale, (float) Scales::Scale::custom);
            rig.map (9, { 1 });   // A plays A#
            rig.set (ParamIDs::harmonyScaleNote (10 /* A# above C */).toRawUTF8(), (float) box);
            return rig.runMono (sung);
        };
        const auto in = playing (0), up = playing (1);
        CAPTURE (levelAt (in, Bb3, 0.5), levelAt (in, B3, 0.5), levelAt (up, Bb3, 0.5), levelAt (up, B3, 0.5));
        CHECK (levelAt (in, Bb3, 0.5) > 0.5f * sungLevel);
        CHECK (levelAt (in, B3, 0.5) < 0.2f * sungLevel);
        CHECK (levelAt (up, B3, 0.5) > 0.5f * sungLevel);
        CHECK (levelAt (up, Bb3, 0.5) < 0.2f * sungLevel);
    }

    SECTION ("An out-of-key sung note has its own row (G# in C major)")
    {
        const auto Gs3 = 207.652;
        CustomMapRig rig;
        rig.map (8 /* G# */, { 4 });     // G# plays C
        rig.map (9 /* A */, { 7 });      // A plays E
        const auto out = rig.runMono (sungVowel (Gs3, 1.0));
        CAPTURE (levelAt (out, C4, 0.5), levelAt (out, E4, 0.5));
        CHECK (levelAt (out, C4, 0.5) > 0.3f * levelAt (sungVowel (Gs3, 1.0), Gs3, 0.5));
        CHECK (levelAt (out, E4, 0.5) < 0.1f * levelAt (sungVowel (Gs3, 1.0), Gs3, 0.5));
    }

    SECTION ("FROM CHORD copies a built-in map: it sounds the same")
    {
        NotesRig builtIn ("3rd + 5th Above");
        CustomMapRig copied;
        NoteMapEditor editor (copied.processor.getParameters(), {});
        editor.fillFromChord (effects::Harmony::chordMapNames().indexOf ("3rd + 5th Above"));
        for (auto hz : { A3, 196.0 /* G3 */, 174.614 /* F3 */ })
        {
            const auto voice = sungVowel (hz, 1.0);
            const auto a = builtIn.runMono (voice), b = copied.runMono (voice);
            CAPTURE (hz, pitchOf (a), pitchOf (b));
            for (auto note : { C4, D4, E4, 349.228 /* F4 */, 391.995 /* G4 */, 440.0 /* A4 */ })
            {
                CAPTURE (note, levelAt (a, note, 0.5), levelAt (b, note, 0.5));
                CHECK ((levelAt (a, note, 0.5) > 0.3f * sungLevel) == (levelAt (b, note, 0.5) > 0.3f * sungLevel));
            }
        }
    }
}

TEST_CASE ("HARMONY Note Map: FROM CHORD matches every built-in map, note for note", "[effects][harmony][notemap]")
{
    // For every built-in map, key and scale, and every sung note: the copied
    // Custom map, put into the scale, plays exactly the notes the built-in does.
    ParagonX1AudioProcessor processor;
    NoteMapEditor editor (processor.getParameters(), {});
    auto& parameters = processor.getParameters();
    const auto setPlain = [&parameters] (const char* id, float plain)
    {
        auto* parameter = parameters.getParameter (id);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plain));
    };
    for (auto scale : { Scales::Scale::major, Scales::Scale::chromatic, (Scales::Scale) 1 })
        for (auto key : { 0, 7, 10 })
        {
            setPlain (ParamIDs::harmonyKey, (float) key);
            setPlain (ParamIDs::harmonyScale, (float) scale);
            for (int chord = 0; chord < effects::Harmony::customChord; ++chord)
            {
                editor.fillFromChord (chord);
                effects::Harmony::NoteMap map {};
                for (int noteClass = 0; noteClass < 12; ++noteClass)
                    map[(size_t) noteClass] = editor.mapOf (noteClass);
                const auto& builtIn = effects::Harmony::chordMaps[(size_t) chord];
                for (int incoming = 48; incoming < 72; ++incoming)
                {
                    std::set<int> expected, got;
                    const auto root = Scales::nearest (incoming, key, scale);
                    for (int i = 0; i < builtIn.count; ++i)
                        expected.insert (Scales::harmonyNote (root, builtIn.intervals[(size_t) i], key, scale));
                    std::array<int, effects::Harmony::mapBits> notes {};
                    const auto count = effects::Harmony::customNotes (map, false, incoming, notes);
                    for (int i = 0; i < count; ++i)
                        got.insert (Scales::nearest (notes[(size_t) i], key, scale));
                    CAPTURE ((int) scale, key, builtIn.name, incoming);
                    CHECK (got == expected);
                }
            }
        }
}

//==============================================================================
// HARMONY stage 6: per-voice DELAY, FEEDBACK, FILTER and FLATTEN.
namespace
{
    // A sung note after 0.1 s of silence: by then the singer's own voice
    // has faded out to SOURCE's silence, so the first sound out is the
    // harmony voice's.
    std::vector<float> afterSilence (std::vector<float> note)
    {
        auto samples = silence (0.1);
        samples.insert (samples.end(), note.begin(), note.end());
        return samples;
    }
    double voiceOnset (const std::vector<float>& samples) { return onsetOf (samples); }
}

TEST_CASE ("HARMONY voice DELAY and FEEDBACK: the voice comes later, and repeats", "[effects][harmony][voicefx]")
{
    const auto A3 = 220.0;
    const auto sung = afterSilence (sungVowel (A3, 1.0));

    SECTION ("DELAY 300 ms: the voice starts 300 ms later; synced, a note length at the tempo")
    {
        HarmonyRig plain (7.0f);
        const auto start = voiceOnset (plain.runMono (sung));
        HarmonyRig delayed (7.0f);
        delayed.setVoice ("Delay", 300.0f);
        const auto later = voiceOnset (delayed.runMono (sung));
        CAPTURE (start, later);
        CHECK_THAT (later - start, Catch::Matchers::WithinAbs (0.3, 0.01));

        for (auto bpm : { 120.0, 90.0 })
        {
            HarmonyRig synced (7.0f);
            synced.playHead.bpm = bpm;
            synced.setVoice ("Delay", 300.0f);   // ignored while synced
            synced.setVoice ("Sync", 3.0f);      // 1/4
            const auto onset = voiceOnset (synced.runMono (sung));
            CAPTURE (bpm, onset);
            CHECK_THAT (onset - start, Catch::Matchers::WithinAbs (60.0 / bpm, 0.01));
        }
    }

    SECTION ("FEEDBACK 50%: after the singer stops, each repeat about 6 dB down; 0%: one echo only")
    {
        std::vector<float> phrase = sungVowel (A3, 0.4);
        phrase.resize ((size_t) (2.0 * sampleRate), 0.0f);
        const auto run = [&phrase] (float feedback)
        {
            HarmonyRig rig (7.0f);
            rig.setVoice ("Delay", 300.0f);
            rig.setVoice ("Feedback", feedback);
            return rig.runMono (phrase);
        };
        // Sung 0 to 0.4 s: echoes at 0.3-0.7, 0.6-1.0, 0.9-1.3 s (plus the voice's own few ms).
        const auto repeats = run (0.5f);
        const auto second = rms (repeats, 0.76, 0.94), third = rms (repeats, 1.06, 1.24);
        CAPTURE (second, third, dB (third / second));
        CHECK (second > 1.0e-3f);
        CHECK_THAT (dB (third / second), Catch::Matchers::WithinAbs (20.0 * std::log10 (0.5 * effects::Harmony::feedbackAtFull), 1.5));
        const auto once = run (0.0f);
        CHECK (rms (once, 0.3, 0.6) > 1.0e-3f);     // the one echo
        CHECK (rms (once, 0.76, 1.5) < 1.0e-5f);    // and nothing after it
    }
}

TEST_CASE ("HARMONY voice FILTER: low-pass, high-pass and band-pass shape the voice", "[effects][harmony][voicefx]")
{
    // A fifth above a sung A3: E4, 329.6 Hz, and its harmonics.
    const auto E4 = 329.628;
    const auto sung = sungVowel (220.0, 1.0);
    HarmonyRig plain (7.0f);
    const auto open = plain.runMono (sung);
    const auto filtered = [&sung] (int type, float hz)
    {
        HarmonyRig rig (7.0f);
        rig.setVoice ("Filter", (float) type);
        rig.setVoice ("Cutoff", hz);
        return rig.runMono (sung);
    };
    const auto change = [&open] (const std::vector<float>& out, double hz) { return dB (levelAt (out, hz, 0.5) / levelAt (open, hz, 0.5)); };

    const auto lowPass = filtered (0, 500.0f);
    CAPTURE (change (lowPass, E4), change (lowPass, 5 * E4));
    CHECK (change (lowPass, E4) > -3.0f);           // the note stays
    CHECK (change (lowPass, 5 * E4) < -12.0f);      // 1.6 kHz well down

    const auto highPass = filtered (1, 2000.0f);
    CAPTURE (change (highPass, E4), change (highPass, 9 * E4));
    CHECK (change (highPass, E4) < -20.0f);         // the fundamental gone
    CHECK (change (highPass, 9 * E4) > -4.0f);      // 3 kHz through

    const auto bandPass = filtered (2, 1000.0f);
    CAPTURE (change (bandPass, E4), change (bandPass, 3 * E4), change (bandPass, 9 * E4));
    CHECK (std::abs (change (bandPass, 3 * E4)) < 2.0f);   // ~990 Hz: at the centre
    CHECK (change (bandPass, E4) < -6.0f);
    CHECK (change (bandPass, 9 * E4) < -6.0f);

    // LOW-PASS at 20 kHz (the start) does nothing at all.
    CHECK (maxAbsDifference (open, filtered (0, effects::Harmony::filterHighestHz), 0) < 1.0e-6f);
}

TEST_CASE ("HARMONY FLATTEN: the voice holds its NOTE whatever is sung", "[effects][harmony][voicefx]")
{
    const auto C3 = 130.813, Fs3 = 184.997, F3 = 174.614, G3 = 195.998;
    const auto flattened = [] (float note, bool correct, double sungHz)
    {
        HarmonyRig rig (7.0f);
        rig.setVoice ("Flatten", 1.0f);
        rig.setVoice ("Note", note);
        rig.setVoice ("Correct", correct ? 1.0f : 0.0f);
        return pitchOf (rig.runMono (sungVowel (sungHz, 1.0)));
    };
    for (auto sungHz : { 220.0 /* A3 */, 293.665 /* D4 */ })
    {
        const auto heard = flattened (48.0f, true, sungHz);
        CAPTURE (sungHz, heard);
        CHECK (std::abs (cents (heard, C3)) < 20.0);
    }
    // F#3 in C major: as set with CORRECT off, into the key (F3 or G3) with it on.
    CHECK (std::abs (cents (flattened (54.0f, false, 220.0), Fs3)) < 20.0);
    const auto corrected = flattened (54.0f, true, 220.0);
    CAPTURE (corrected);
    CHECK (std::min (std::abs (cents (corrected, F3)), std::abs (cents (corrected, G3))) < 20.0);
}

TEST_CASE ("HARMONY NOTES DELAY and FEEDBACK: the note voices' echo outlasts the key", "[effects][harmony][voicefx][midi]")
{
    const auto sung = afterSilence (sungVowel (220.0, 2.0));
    MidiRig plain;
    const auto start = voiceOnset (runWithMidi (plain, sung, { { 0.0, keyDown (60) } }));
    MidiRig delayed;
    delayed.set (ParamIDs::harmonyNoteDelay, 300.0f);
    CHECK_THAT (voiceOnset (runWithMidi (delayed, sung, { { 0.0, keyDown (60) } })) - start, Catch::Matchers::WithinAbs (0.3, 0.01));

    // Key let go at 0.5 s (RELEASE 150 ms): with FEEDBACK the repeats carry on.
    const auto afterRelease = [&sung] (float feedback)
    {
        MidiRig rig;
        rig.set (ParamIDs::harmonyNoteDelay, 300.0f);
        rig.set (ParamIDs::harmonyNoteFeedback, feedback);
        return rms (runWithMidi (rig, sung, { { 0.0, keyDown (60) }, { 0.5, keyUp (60) } }), 1.4, 1.9);
    };
    CAPTURE (afterRelease (0.6f), afterRelease (0.0f));
    CHECK (afterRelease (0.6f) > 1.0e-3f);
    CHECK (afterRelease (0.6f) > 10.0f * afterRelease (0.0f));   // without: only RELEASE's last trace
}

//==============================================================================
// HARMONY stage 7: GLOBAL GLIDE, PITCH and FORMANT.
TEST_CASE ("HARMONY GLOBAL PITCH: every harmony voice moves, the singer doesn't", "[effects][harmony][global]")
{
    const auto A3 = 220.0, E4 = 329.628, E5 = 659.255, C5 = 523.251;
    HarmonyRig fixed (7.0f);
    fixed.set (ParamIDs::harmonyGlobalPitch, 12.0f);
    const auto heard = pitchOf (fixed.runMono (sungVowel (A3, 1.0)));
    CAPTURE (heard);
    CHECK (std::abs (cents (heard, E5)) < 20.0);   // a fifth above, an octave up
    HarmonyRig plain (7.0f);
    CHECK (std::abs (cents (pitchOf (plain.runMono (sungVowel (A3, 1.0))), E4)) < 20.0);

    // The singer's own voice (SOURCE up, voices off) isn't moved.
    HarmonyRig singer (7.0f);
    singer.set (ParamIDs::harmonySource, 0.0f);
    singer.setVoice ("Active", 0.0f);
    singer.set (ParamIDs::harmonyGlobalPitch, 12.0f);
    const auto sung = sungVowel (A3, 1.0);
    CHECK (maxAbsDifference (sung, singer.runMono (sung), (size_t) (0.1 * sampleRate)) < 1.0e-5f);

    // And note voices too: a held C4 comes out as C5.
    MidiRig midi;
    midi.set (ParamIDs::harmonyGlobalPitch, 12.0f);
    const auto out = runWithMidi (midi, sungVowel (A3, 1.0), { { 0.0, keyDown (60) } });
    CAPTURE (pitchOf (out));
    CHECK (std::abs (cents (pitchOf (out), C5)) < 20.0);
}

TEST_CASE ("HARMONY GLOBAL FORMANT: the same as each voice's own FORMANT", "[effects][harmony][global]")
{
    const auto sung = sungVowel (220.0, 1.0);
    HarmonyRig global (7.0f);
    global.set (ParamIDs::harmonyGlobalFormant, 5.0f);
    HarmonyRig own (7.0f);
    own.setVoice ("Formant", 5.0f);
    HarmonyRig plain (7.0f);
    const auto a = global.runMono (sung), b = own.runMono (sung), c = plain.runMono (sung);
    CHECK (maxAbsDifference (a, b, 0) < 1.0e-6f);
    CHECK (maxAbsDifference (a, c, (size_t) (0.2 * sampleRate)) > 1.0e-3f);   // and it does change the voice
}

TEST_CASE ("HARMONY GLIDE: the voices slide to a new note instead of jumping", "[effects][harmony][global]")
{
    // Sung A3 then C4 (from 0.6 s): the fifth above goes from E4 to G4. A
    // voice gliding there is, for a moment, less than a fifth above the
    // singer; one that jumps is a fifth above straight away. (A pitch
    // can't be read off a slide, so this reads the voice's own shift.)
    const auto line = sungLine ({ { 220.0, 0.6 }, { 261.626, 0.9 } });
    const auto shiftAt = [&line] (std::function<void (HarmonyRig&)> setUp, double seconds)
    {
        HarmonyRig rig (7.0f);
        setUp (rig);
        rig.runMono (std::vector<float> (line.begin(), line.begin() + (long) (seconds * sampleRate)));
        return 12.0 * std::log2 ((double) rig.processor.getHarmony().getRatio (0));
    };
    const auto noGlide = [] (HarmonyRig&) {};
    const auto glide300 = [] (HarmonyRig& rig) { rig.set (ParamIDs::harmonyGlide, 300.0f); };
    const auto synced = [] (HarmonyRig& rig) { rig.set (ParamIDs::harmonyGlideSync, 3.0f); };   // 1/4 at 120: 500 ms
    const auto soon = 0.6 + 0.12;   // once the new note's been heard
    CAPTURE (shiftAt (noGlide, soon), shiftAt (glide300, soon), shiftAt (synced, soon), shiftAt (glide300, 0.6 + 0.45));
    CHECK (std::abs (shiftAt (noGlide, soon) - 7.0) < 0.2);    // straight to the fifth
    CHECK (shiftAt (glide300, soon) < 6.0);                      // still sliding up from E4
    CHECK (shiftAt (synced, soon) < shiftAt (glide300, soon));   // the longer glide, further behind
    CHECK (std::abs (shiftAt (glide300, 0.6 + 0.45) - 7.0) < 0.3);   // there within GLIDE (plus the note's detection)
}

//==============================================================================
// HARMONY stage 8: SNAPSHOTS.
TEST_CASE ("HARMONY SNAPSHOTS: store, recall (from the panel or the SNAPSHOT parameter), save", "[effects][harmony][snapshots]")
{
    const auto A3 = 220.0, E4 = 329.628;
    const auto sung = sungVowel (A3, 1.0);
    const auto pitchOfVoice = [&sung] (HarmonyRig& rig) { return pitchOf (rig.runMono (sung)); };
    const auto pitchId = ParamIDs::harmonyVoice (0, "Pitch");

    SECTION ("Recall brings back what was stored, and the voice sounds it")
    {
        HarmonyRig rig (7.0f);   // a fifth: E4
        rig.processor.storeHarmonySnapshot (0);
        rig.setVoice ("Pitch", 4.0f);   // a third up (C4 in C major): not E4
        CHECK (std::abs (cents (pitchOfVoice (rig), E4)) > 100.0);
        rig.processor.recallHarmonySnapshot (0);
        CHECK (rig.processor.getParameters().getRawParameterValue (pitchId)->load() == 7.0f);
        CHECK (std::abs (cents (pitchOfVoice (rig), E4)) < 20.0);
        CHECK (juce::roundToInt (rig.processor.getParameters().getRawParameterValue (ParamIDs::harmonySnapshot)->load()) == 1);
    }

    SECTION ("Moving the SNAPSHOT parameter (automation, MIDI) recalls; an empty slot does nothing")
    {
        HarmonyRig rig (7.0f);
        rig.processor.storeHarmonySnapshot (2);
        rig.setVoice ("Pitch", 4.0f);
        rig.set (ParamIDs::harmonySnapshot, 5.0f);   // slot 5: empty
        rig.processor.followSnapshotParameter();
        CHECK (rig.processor.getParameters().getRawParameterValue (pitchId)->load() == 4.0f);
        rig.set (ParamIDs::harmonySnapshot, 3.0f);   // slot 3: stored
        rig.processor.followSnapshotParameter();
        CHECK (rig.processor.getParameters().getRawParameterValue (pitchId)->load() == 7.0f);
    }

    SECTION ("Only the harmony voices: KEY, GLOBAL and the mix are left alone")
    {
        HarmonyRig rig (7.0f);
        rig.processor.storeHarmonySnapshot (0);
        rig.set (ParamIDs::harmonyKey, 2.0f);
        rig.set (ParamIDs::harmonyGlobalPitch, 5.0f);
        rig.set (ParamIDs::harmonyLevel, -6.0f);
        rig.processor.recallHarmonySnapshot (0);
        auto& state = rig.processor.getParameters();
        CHECK (state.getRawParameterValue (ParamIDs::harmonyKey)->load() == 2.0f);
        CHECK (state.getRawParameterValue (ParamIDs::harmonyGlobalPitch)->load() == 5.0f);
        CHECK_THAT (state.getRawParameterValue (ParamIDs::harmonyLevel)->load(), WithinAbs (-6.0, 0.001));   // a smooth range: -5.999999 on macOS
    }

    SECTION ("Saved with the project and in rig files; loading isn't a recall")
    {
        HarmonyRig rig (7.0f);
        rig.processor.storeHarmonySnapshot (4);
        rig.setVoice ("Pitch", 4.0f);   // changed since, and saved like that
        juce::MemoryBlock project;
        rig.processor.getStateInformation (project);
        HarmonyRig other (0.0f);
        other.processor.setStateInformation (project.getData(), (int) project.getSize());
        other.processor.followSnapshotParameter();   // SNAPSHOT is 5 in the project: not recalled on load
        CHECK (other.processor.getParameters().getRawParameterValue (pitchId)->load() == 4.0f);
        REQUIRE (other.processor.getHarmonySnapshot (4).has_value());
        CHECK (other.processor.getHarmonySnapshot (4)->at (pitchId) == 7.0f);
        CHECK_FALSE (other.processor.getHarmonySnapshot (0).has_value());

        const auto xml = rig.processor.captureRig().toXml();
        const auto reloaded = Rig::fromXml (*xml);
        REQUIRE (reloaded.has_value());
        CHECK (reloaded->harmonySnapshots == rig.processor.captureRig().harmonySnapshots);
    }

    SECTION ("Undo takes back a stored snapshot")
    {
        HarmonyRig rig (7.0f);
        rig.processor.flushUndoStep();
        rig.processor.storeHarmonySnapshot (0);
        rig.processor.flushUndoStep();
        REQUIRE (rig.processor.getHarmonySnapshot (0).has_value());
        rig.processor.undo();
        CHECK_FALSE (rig.processor.getHarmonySnapshot (0).has_value());
    }
}

//==============================================================================
// HARMONY stage 9: the modulators (M1-M4, AM, PT) and their slots.
namespace
{
    using HarmonyMod = effects::HarmonyModulation;

    // Runs a modulator engine for `seconds` in 32-sample steps, as the
    // processor does; returns M1's (or `source`'s) value at each step.
    std::vector<float> runModulator (HarmonyMod& engine, const HarmonyMod::Settings& settings, double seconds,
                                     int source = HarmonyMod::m1, HarmonyMod::Context context = {}, const float* voice = nullptr)
    {
        std::vector<float> values;
        const auto steps = (int) (seconds * sampleRate / 32.0);
        for (int i = 0; i < steps; ++i)
        {
            engine.advance (settings, context, voice != nullptr ? voice + i * 32 : nullptr, 32);
            context.newNote = false;
            if (context.songBeats >= 0.0)
                context.songBeats += 32.0 / sampleRate * context.bpm / 60.0;
            values.push_back (engine.getValue (source));
        }
        return values;
    }

    // A voice's shift from the singer right now, in semitones.
    double voiceShift (HarmonyRig& rig, int voice = 0) { return 12.0 * std::log2 ((double) rig.processor.getHarmony().getRatio (voice)); }

    // Puts a modulator on a control, in its first slot.
    void modulate (HarmonyRig& rig, const juce::String& targetId, int source, float depth, int slot = 0)
    {
        const auto target = ParagonX1AudioProcessor::harmonyModTarget (targetId);
        REQUIRE (target >= 0);
        rig.processor.setHarmonyModSlot (target, slot, { source, depth });
    }
}

TEST_CASE ("HARMONY modulators: LFO shapes, the sequencer, PLAY and TRIGGER do what they say", "[effects][harmony][modulators]")
{
    HarmonyMod engine;
    engine.prepare (sampleRate);
    HarmonyMod::Settings settings;
    auto& m1 = settings.mods[0];

    SECTION ("A sine at 2 Hz swings fully both ways, twice a second")
    {
        m1.rateHz = 2.0f;
        const auto values = runModulator (engine, settings, 1.2);   // it starts at 0 going up, so crosses up at 0.5 s and 1 s
        auto rising = 0;
        for (size_t i = 1; i < values.size(); ++i)
            rising += values[i - 1] < 0.0f && values[i] >= 0.0f ? 1 : 0;
        CHECK (*std::max_element (values.begin(), values.end()) > 0.99f);
        CHECK (*std::min_element (values.begin(), values.end()) < -0.99f);
        CHECK (rising == 2);
    }

    SECTION ("LEVEL scales it; WARP keeps the cycle's length but moves its middle")
    {
        m1.rateHz = 1.0f;
        m1.level = 0.5f;
        const auto half = runModulator (engine, settings, 1.0);
        CHECK_THAT (*std::max_element (half.begin(), half.end()), WithinAbs (0.5, 0.01));
        // Saw up, warped above 1: fast at the start, so a quarter through it's well past a quarter of the way.
        CHECK_THAT (HarmonyMod::modValue (HarmonyMod::Mod { 0, 2 }, 0.25f, 0, 1), WithinAbs (-0.5, 0.001));
        HarmonyMod::Mod warped { 0, 2 };
        warped.warp = 4.0f;
        CHECK (HarmonyMod::modValue (warped, 0.25f, 0, 1) > 0.3f);
        CHECK_THAT (HarmonyMod::modValue (warped, 0.999f, 0, 1), WithinAbs (1.0, 0.01));   // still ends where it ends
    }

    SECTION ("SEQ: each step's value (in semitones out of 24), at RATE steps a second")
    {
        m1.type = 1;
        m1.steps = 2;
        m1.stepValues[0] = 12.0f;
        m1.stepValues[1] = -6.0f;
        m1.rateHz = 4.0f;   // a step every 0.25 s
        const auto values = runModulator (engine, settings, 1.0);
        const auto at = [&values] (double seconds) { return values[(size_t) (seconds * sampleRate / 32.0)]; };
        CHECK_THAT (at (0.1), WithinAbs (0.5, 0.01));
        CHECK_THAT (at (0.35), WithinAbs (-0.25, 0.01));
        CHECK_THAT (at (0.6), WithinAbs (0.5, 0.01));
        CHECK_THAT (at (0.85), WithinAbs (-0.25, 0.01));
    }

    SECTION ("PLAY: One Shot stops at the end; Hold stays at PHASE; Seesaw comes back")
    {
        m1.shape = 2;   // Saw Up
        m1.rateHz = 2.0f;
        m1.play = HarmonyMod::oneShot;
        const auto once = runModulator (engine, settings, 1.5);
        CHECK (once.back() > 0.99f);   // stopped at the top

        HarmonyMod held;
        held.prepare (sampleRate);
        m1.play = HarmonyMod::hold;
        m1.phase = 0.75f;
        const auto still = runModulator (held, settings, 1.0);
        CHECK_THAT (still.front(), WithinAbs (0.5, 0.01));
        CHECK_THAT (still.back(), WithinAbs (0.5, 0.01));

        HarmonyMod seesaw;
        seesaw.prepare (sampleRate);
        m1.play = HarmonyMod::seesaw;
        m1.phase = 0.0f;
        const auto backAndForth = runModulator (seesaw, settings, 1.0);   // up for 0.5 s, then back down
        CHECK (backAndForth[(size_t) (0.45 * sampleRate / 32.0)] > 0.7f);
        CHECK (backAndForth[(size_t) (0.95 * sampleRate / 32.0)] < -0.7f);
    }

    SECTION ("TRIGGER Sync locks it to the song; Retrigger restarts it on a new note")
    {
        m1.shape = 2;   // Saw Up
        m1.sync = HarmonyMod::syncNames.indexOf ("1 Bar");
        m1.trigger = HarmonyMod::sync;
        HarmonyMod::Context context;
        context.bpm = 120.0;
        context.songBeats = 2.0;   // half way through a bar
        engine.advance (settings, context, nullptr, 32);
        CHECK_THAT (engine.getValue (HarmonyMod::m1), WithinAbs (0.0, 0.01));

        HarmonyMod again;
        again.prepare (sampleRate);
        m1.sync = 0;
        m1.rateHz = 1.0f;
        m1.trigger = HarmonyMod::retrigger;
        runModulator (again, settings, 0.6);   // well up the ramp
        CHECK (again.getValue (HarmonyMod::m1) > 0.1f);
        HarmonyMod::Context note;
        note.newNote = true;
        again.advance (settings, note, nullptr, 32);
        runModulator (again, settings, 0.004);   // SMOOTH (1 ms) settles
        CHECK_THAT (again.getValue (HarmonyMod::m1), WithinAbs (-1.0, 0.03));   // back to the start of the ramp
    }

    SECTION ("SMOOTH rounds off a square's jumps")
    {
        m1.shape = 4;   // Square
        m1.rateHz = 1.0f;
        m1.smoothMs = 100.0f;
        const auto values = runModulator (engine, settings, 1.0);
        // Just after the drop at 0.5 s it's still on its way down.
        CHECK (values[(size_t) (0.52 * sampleRate / 32.0)] > -0.5f);
        CHECK (values[(size_t) (0.95 * sampleRate / 32.0)] < -0.95f);
    }

    SECTION ("AM follows how loud the voice is; PT the note sung")
    {
        // 0 at -60 dB to 1 at full scale: a voice 20 dB quieter reads a third lower.
        const auto loud = sungVowel (220.0, 0.5);
        auto quiet = loud;
        for (auto& sample : quiet)
            sample *= 0.1f;
        const auto loudValue = runModulator (engine, settings, 0.45, HarmonyMod::am, {}, loud.data()).back();
        HarmonyMod other;
        other.prepare (sampleRate);
        const auto quietValue = runModulator (other, settings, 0.45, HarmonyMod::am, {}, quiet.data()).back();
        CHECK_THAT (loudValue - quietValue, WithinAbs (1.0 / 3.0, 0.03));
        const std::vector<float> silence ((size_t) sampleRate, 0.0f);
        const auto after = runModulator (engine, settings, 1.0, HarmonyMod::am, {}, silence.data());
        CHECK (after.back() < 0.05f);

        HarmonyMod::Context sung;
        sung.sungNote = 72.0f;   // C5, an octave above middle C
        const auto pitch = runModulator (engine, settings, 0.5, HarmonyMod::pt, sung);
        CHECK_THAT (pitch.back(), WithinAbs (0.5, 0.01));
    }
}

TEST_CASE ("HARMONY modulators move the voices: vibrato, a sequenced line, AM and PT", "[effects][harmony][modulators]")
{
    const auto sung = sungVowel (220.0, 2.0);   // A3
    // The voice's shift over the second half (once the note is read), at each block.
    const auto shifts = [&sung] (HarmonyRig& rig)
    {
        std::vector<double> found;
        const auto half = sung.size() / 2;
        rig.runMono (std::vector<float> (sung.begin(), sung.begin() + (long) half));
        for (size_t start = half; start + blockSize <= sung.size(); start += blockSize)
        {
            rig.runMono (std::vector<float> (sung.begin() + (long) start, sung.begin() + (long) (start + blockSize)));
            found.push_back (voiceShift (rig));
        }
        return found;
    };

    SECTION ("An LFO on PITCH at 1/24 depth: a vibrato a semitone either way of the fifth")
    {
        HarmonyRig rig (7.0f);
        rig.setVoice ("Correct", 0.0f);   // exactly where the modulation puts it
        rig.set (ParamIDs::harmonyMod (0, "Rate").toRawUTF8(), 3.0f);
        modulate (rig, ParamIDs::harmonyVoice (0, "Pitch"), HarmonyMod::m1, 1.0f / 24.0f);
        const auto found = shifts (rig);
        CAPTURE (*std::min_element (found.begin(), found.end()), *std::max_element (found.begin(), found.end()));
        CHECK_THAT (*std::max_element (found.begin(), found.end()), WithinAbs (8.0, 0.2));
        CHECK_THAT (*std::min_element (found.begin(), found.end()), WithinAbs (6.0, 0.2));

        HarmonyRig plain (7.0f);
        plain.setVoice ("Correct", 0.0f);
        const auto still = shifts (plain);
        CHECK (*std::max_element (still.begin(), still.end()) - *std::min_element (still.begin(), still.end()) < 0.05);
    }

    SECTION ("A two-step SEQ at 100%: the voice alternates between the fifth and the octave")
    {
        HarmonyRig rig (7.0f);
        rig.setVoice ("Correct", 0.0f);
        rig.set (ParamIDs::harmonyMod (0, "Type").toRawUTF8(), 1.0f);
        rig.set (ParamIDs::harmonyMod (0, "Steps").toRawUTF8(), 2.0f);
        rig.set (ParamIDs::harmonyModStep (0, 1).toRawUTF8(), 5.0f);   // step 2: +5, a fourth more
        rig.set (ParamIDs::harmonyMod (0, "Rate").toRawUTF8(), 4.0f);
        modulate (rig, ParamIDs::harmonyVoice (0, "Pitch"), HarmonyMod::m1, 1.0f);
        const auto found = shifts (rig);
        const auto near = [&found] (double semitones)
        {
            return std::count_if (found.begin(), found.end(), [semitones] (double s) { return std::abs (s - semitones) < 0.15; });
        };
        CAPTURE (near (7.0), near (12.0), found.size());
        CHECK (near (7.0) > (long) found.size() / 4);
        CHECK (near (12.0) > (long) found.size() / 4);
    }

    SECTION ("PT on PITCH: an A3 (3 semitones under middle C) at 100% takes the voice 3 lower")
    {
        HarmonyRig rig (7.0f);
        rig.setVoice ("Correct", 0.0f);
        modulate (rig, ParamIDs::harmonyVoice (0, "Pitch"), HarmonyMod::pt, 1.0f);
        const auto found = shifts (rig);
        CHECK_THAT (found.back(), WithinAbs (4.0, 0.2));
    }

    SECTION ("AM on HARMONY's level, turned all the way down: singing loudly ducks the harmony")
    {
        const auto level = [&sung] (bool ducked)
        {
            HarmonyRig rig (7.0f);
            if (ducked)
                modulate (rig, ParamIDs::harmonyLevel, HarmonyMod::am, -1.0f);
            return rms (rig.runMono (sung), 1.0, 2.0);
        };
        const auto plain = level (false), ducked = level (true);
        CAPTURE (plain, ducked);
        CHECK (ducked < plain * 0.3f);
    }

    SECTION ("Nothing assigned: the voice is exactly as before, whatever the modulators do")
    {
        HarmonyRig plain (7.0f), busy (7.0f);
        busy.set (ParamIDs::harmonyMod (0, "Rate").toRawUTF8(), 10.0f);
        busy.set (ParamIDs::harmonyMod (1, "Type").toRawUTF8(), 1.0f);
        modulate (busy, ParamIDs::harmonyVoice (0, "Pitch"), HarmonyMod::m1, 0.0f);   // a slot at no depth
        CHECK (maxAbsDifference (plain.runMono (sung), busy.runMono (sung), 0) < 1.0e-6f);
    }
}

TEST_CASE ("HARMONY modulation slots are saved with the project and the rig, and undone", "[effects][harmony][modulators]")
{
    HarmonyRig rig (7.0f);
    const auto pitch = ParagonX1AudioProcessor::harmonyModTarget (ParamIDs::harmonyVoice (0, "Pitch"));
    const auto spread = ParagonX1AudioProcessor::harmonyModTarget (ParamIDs::harmonySpread);
    REQUIRE ((pitch >= 0 && spread >= 0));
    CHECK (ParagonX1AudioProcessor::harmonyModTarget (ParamIDs::harmonyVoice (0, "Fine")) < 0);   // FINE can't be, as in the guide
    rig.processor.setHarmonyModSlot (pitch, 0, { HarmonyMod::m2, 0.25f });
    rig.processor.setHarmonyModSlot (pitch, 3, { HarmonyMod::pt, -0.5f });
    rig.processor.setHarmonyModSlot (spread, 1, { HarmonyMod::am, 1.0f });

    SECTION ("The project")
    {
        juce::MemoryBlock state;
        rig.processor.getStateInformation (state);
        HarmonyRig other (0.0f);
        other.processor.setStateInformation (state.getData(), (int) state.getSize());
        CHECK (other.processor.getHarmonyModSlot (pitch, 0) == Rig::HarmonyModSlot { HarmonyMod::m2, 0.25f });
        CHECK (other.processor.getHarmonyModSlot (pitch, 3) == Rig::HarmonyModSlot { HarmonyMod::pt, -0.5f });
        CHECK (other.processor.getHarmonyModSlot (spread, 1) == Rig::HarmonyModSlot { HarmonyMod::am, 1.0f });
        CHECK (other.processor.getHarmonyModSlot (pitch, 1).source == 0);
    }

    SECTION ("A rig file, and loading a rig without any takes them off")
    {
        const auto xml = rig.processor.captureRig().toXml();
        const auto loaded = Rig::fromXml (*xml);
        REQUIRE (loaded.has_value());
        CHECK (loaded->harmonyModSlots == rig.processor.captureRig().harmonyModSlots);
        HarmonyRig other (0.0f);
        other.processor.loadRig ({ *loaded, {}, false });
        CHECK (other.processor.getHarmonyModSlot (pitch, 0) == Rig::HarmonyModSlot { HarmonyMod::m2, 0.25f });
        Rig empty;
        other.processor.loadRig ({ empty, {}, false });
        CHECK (other.processor.getHarmonyModSlot (pitch, 0).source == 0);
    }

    SECTION ("Undo takes a change back")
    {
        rig.processor.flushUndoStep();   // the edits so far, as one step
        rig.processor.setHarmonyModSlot (pitch, 0, { HarmonyMod::m4, 1.0f });
        rig.processor.flushUndoStep();
        rig.processor.undo();
        CHECK (rig.processor.getHarmonyModSlot (pitch, 0) == Rig::HarmonyModSlot { HarmonyMod::m2, 0.25f });
    }
}

//==============================================================================
// HARMONY stage 10: A1-A2, the Spreader, MIDI sources, Poly, drawn shapes,
// and modulator slots in snapshots.
namespace
{
    // Runs the engine for `seconds` with `notes` (one per note voice, -1 for
    // none) and returns `source`'s value for `voice` at the end.
    float runVoices (HarmonyMod& engine, const HarmonyMod::Settings& settings, double seconds,
                     std::initializer_list<int> notes, int source, int voice)
    {
        HarmonyMod::Context context;
        auto v = 0;
        for (const auto note : notes)
            context.noteVoices[(size_t) v++] = { note, 1.0f };
        const auto steps = juce::jmax (1, (int) std::lround (seconds * sampleRate / 32.0));
        for (int i = 0; i < steps; ++i)
            engine.advance (settings, context, nullptr, 32);
        return engine.getVoiceValue (source, voice);
    }

    // The level of `hz` between two times (seconds).
    float levelBetween (const std::vector<float>& samples, double hz, double from, double to)
    {
        const std::vector<float> part (samples.begin() + (long) (from * sampleRate),
                                       samples.begin() + (long) std::min (samples.size(), (size_t) (to * sampleRate)));
        return levelAt (part, hz, 0.0);
    }
}

TEST_CASE ("HARMONY stage 10 sources: envelopes, the Spreader, Poly, drawn shapes, MIDI", "[effects][harmony][modulators]")
{
    HarmonyMod engine;
    engine.prepare (sampleRate);
    HarmonyMod::Settings settings;

    SECTION ("An envelope rises over ATTACK, falls to SUSTAIN over DECAY, and dies over RELEASE")
    {
        HarmonyMod::Envelope envelope;   // 10 ms, 200 ms, 0.7, 300 ms
        CHECK_THAT (HarmonyMod::envelopeHeld (envelope, 0.005), WithinAbs (0.5, 0.001));
        CHECK_THAT (HarmonyMod::envelopeHeld (envelope, 0.010 + 0.1), WithinAbs (0.85, 0.001));
        CHECK_THAT (HarmonyMod::envelopeHeld (envelope, 1.0), WithinAbs (0.7, 0.001));
        CHECK_THAT (HarmonyMod::envelopeReleased (envelope, 0.7f, 0.15), WithinAbs (0.35, 0.001));
        CHECK (HarmonyMod::envelopeReleased (envelope, 0.7f, 0.31) == 0.0f);
        // CURVE above 1: fast then slow, so half way through the attack it's well past half.
        envelope.attackCurve = 4.0f;
        CHECK (HarmonyMod::envelopeHeld (envelope, 0.005) > 0.8f);
        envelope.attackCurve = 0.25f;
        CHECK (HarmonyMod::envelopeHeld (envelope, 0.005) < 0.1f);
    }

    SECTION ("A1 follows each note voice on its own; with no notes it stays at 0")
    {
        CHECK (runVoices (engine, settings, 0.5, {}, HarmonyMod::a1, 0) == 0.0f);
        CHECK (engine.getValue (HarmonyMod::a1) == 0.0f);
        // Voice 1 held half a second (sustain), then voice 2 joins: it starts its own attack.
        runVoices (engine, settings, 0.5, { 60 }, HarmonyMod::a1, 0);
        CHECK_THAT (engine.getVoiceValue (HarmonyMod::a1, 0), WithinAbs (0.7, 0.01));
        runVoices (engine, settings, 0.005, { 60, 64 }, HarmonyMod::a1, 0);
        CHECK_THAT (engine.getVoiceValue (HarmonyMod::a1, 0), WithinAbs (0.7, 0.01));
        CHECK (engine.getVoiceValue (HarmonyMod::a1, 1) < 0.7f);
        CHECK (engine.getVoiceValue (HarmonyMod::a1, 1) > 0.2f);
        // Both let go: released, half way down after 150 ms (straight release from 0.7).
        runVoices (engine, settings, 0.3, { 60, 64 }, HarmonyMod::a1, 0);
        CHECK_THAT (runVoices (engine, settings, 0.15, { -1, -1 }, HarmonyMod::a1, 0), WithinAbs (0.35, 0.02));
        CHECK (runVoices (engine, settings, 0.3, { -1, -1 }, HarmonyMod::a1, 0) == 0.0f);
    }

    SECTION ("SP Outward: lowest note -SPREAD, highest +SPREAD, between in step; Inward the other way")
    {
        settings.spreader.spread = 0.8f;
        runVoices (engine, settings, 0.01, { 67, 60, 64 }, HarmonyMod::sp, 0);
        CHECK_THAT (engine.getVoiceValue (HarmonyMod::sp, 0), WithinAbs (0.8, 1.0e-5));
        CHECK_THAT (engine.getVoiceValue (HarmonyMod::sp, 1), WithinAbs (-0.8, 1.0e-5));
        CHECK_THAT (engine.getVoiceValue (HarmonyMod::sp, 2), WithinAbs (0.0, 1.0e-5));

        HarmonyMod inward;
        inward.prepare (sampleRate);
        settings.spreader.mode = 1;
        runVoices (inward, settings, 0.01, { 67, 60, 64 }, HarmonyMod::sp, 0);
        CHECK_THAT (inward.getVoiceValue (HarmonyMod::sp, 0), WithinAbs (-0.8, 1.0e-5));
        CHECK_THAT (inward.getVoiceValue (HarmonyMod::sp, 1), WithinAbs (0.8, 1.0e-5));
    }

    SECTION ("SP Retrigger works every note out again when one starts; Poly keeps each note's value")
    {
        const auto afterThird = [&settings] (int trigger)
        {
            HarmonyMod spreader;
            spreader.prepare (sampleRate);
            settings.spreader.trigger = trigger;
            runVoices (spreader, settings, 0.01, { 60, 64 }, HarmonyMod::sp, 0);   // -1, +1
            runVoices (spreader, settings, 0.01, { 60, 64, 67 }, HarmonyMod::sp, 0);   // a G above both
            return std::array<float, 3> { spreader.getVoiceValue (HarmonyMod::sp, 0), spreader.getVoiceValue (HarmonyMod::sp, 1),
                                          spreader.getVoiceValue (HarmonyMod::sp, 2) };
        };
        const auto retrigger = afterThird (0), poly = afterThird (1);
        CHECK_THAT (retrigger[1], WithinAbs (0.0, 1.0e-5));   // the E is in the middle now
        CHECK_THAT (retrigger[2], WithinAbs (1.0, 1.0e-5));
        CHECK_THAT (poly[0], WithinAbs (-1.0, 1.0e-5));
        CHECK_THAT (poly[1], WithinAbs (1.0, 1.0e-5));         // the E keeps the top value it began with
        CHECK_THAT (poly[2], WithinAbs (1.0, 1.0e-5));
    }

    SECTION ("TRIGGER Poly: each note voice its own cycle, from when its note began")
    {
        auto& m1 = settings.mods[0];
        m1.shape = 2;   // Saw Up, 1 Hz
        m1.trigger = HarmonyMod::poly;
        runVoices (engine, settings, 0.5, { 60 }, HarmonyMod::m1, 0);
        runVoices (engine, settings, 0.25, { 60, 64 }, HarmonyMod::m1, 0);
        CHECK_THAT (engine.getVoiceValue (HarmonyMod::m1, 0), WithinAbs (0.5, 0.01));    // 0.75 s through
        CHECK_THAT (engine.getVoiceValue (HarmonyMod::m1, 1), WithinAbs (-0.5, 0.01));   // 0.25 s through
        CHECK (engine.isPerNote (HarmonyMod::m1, settings));
        CHECK_FALSE (engine.isPerNote (HarmonyMod::m2, settings));
        CHECK_FALSE (engine.isPerNote (HarmonyMod::am, settings));
    }

    SECTION ("SHAPE Drawn: the points joined by straight lines")
    {
        HarmonyMod::Mod drawn;
        drawn.shape = HarmonyMod::drawnShape;
        drawn.drawn[8] = 1.0f;
        drawn.drawn[9] = -1.0f;
        CHECK_THAT (HarmonyMod::modValue (drawn, 8.0f / 32.0f, 0, 1), WithinAbs (1.0, 1.0e-5));
        CHECK_THAT (HarmonyMod::modValue (drawn, 8.25f / 32.0f, 0, 1), WithinAbs (0.5, 1.0e-5));
        CHECK_THAT (HarmonyMod::modValue (drawn, 8.5f / 32.0f, 0, 1), WithinAbs (0.0, 1.0e-5));
        CHECK_THAT (HarmonyMod::modValue (drawn, 20.0f / 32.0f, 0, 1), WithinAbs (0.0, 1.0e-5));
    }

    SECTION ("MIDI sources pass through; Velocity and Keyboard go note by note")
    {
        HarmonyMod::Context context;
        context.midi = { 0.8f, 0.5f, -0.25f, 0.6f, 0.3f };
        context.noteVoices[0] = { 72, 0.4f };
        context.noteVoices[1] = { 48, 1.0f };
        engine.advance (settings, context, nullptr, 32);
        CHECK (engine.getValue (HarmonyMod::velocity) == 0.8f);
        CHECK (engine.getValue (HarmonyMod::pitchWheel) == -0.25f);
        CHECK (engine.getValue (HarmonyMod::modWheel) == 0.6f);
        CHECK (engine.getValue (HarmonyMod::aftertouch) == 0.3f);
        CHECK (engine.getVoiceValue (HarmonyMod::velocity, 0) == 0.4f);
        CHECK_THAT (engine.getVoiceValue (HarmonyMod::keyboard, 0), WithinAbs (0.5, 1.0e-5));
        CHECK_THAT (engine.getVoiceValue (HarmonyMod::keyboard, 1), WithinAbs (-0.5, 1.0e-5));
        CHECK (engine.getVoiceValue (HarmonyMod::modWheel, 1) == 0.6f);   // not note by note: the same for all
    }
}

TEST_CASE ("HARMONY stage 10 moves each note voice on its own", "[effects][harmony][modulators][midi]")
{
    const auto A3 = 220.0;
    const auto hz = [] (double note) { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); };
    const auto targetOf = [] (const char* id) { return ParagonX1AudioProcessor::harmonyModTarget (id); };

    SECTION ("SP on GLOBAL PITCH at 1/24: C4 and G4 held, the low one a semitone down, the high one up")
    {
        const auto sung = sungVowel (A3, 1.0);
        MidiRig rig;
        rig.processor.setHarmonyModSlot (targetOf (ParamIDs::harmonyGlobalPitch), 0, { HarmonyMod::sp, 1.0f / 24.0f });
        const auto out = runWithMidi (rig, sung, { { 0.0, keyDown (60) }, { 0.0, keyDown (67) } });
        const auto sungLevel = levelAt (sung, A3, 0.5);
        CAPTURE (levelAt (out, hz (59), 0.5), levelAt (out, hz (68), 0.5), levelAt (out, hz (60), 0.5), levelAt (out, hz (67), 0.5));
        CHECK (levelAt (out, hz (59), 0.5) > 0.5f * sungLevel);
        CHECK (levelAt (out, hz (68), 0.5) > 0.5f * sungLevel);
        CHECK (levelAt (out, hz (60), 0.5) < 0.2f * sungLevel);
        CHECK (levelAt (out, hz (67), 0.5) < 0.2f * sungLevel);
    }

    SECTION ("A1 on NOTES LEVEL turned down: each key ducks from its own start, then comes back")
    {
        // SUSTAIN 0, so a key is quiet as it starts and back at full once the
        // envelope has decayed: a second key, half a second later, is quiet
        // while the first is already back.
        const auto sung = sungVowel (A3, 1.4);
        MidiRig rig;
        rig.set (ParamIDs::harmonyEnvelope (0, "Attack").toRawUTF8(), 1.0f);
        rig.set (ParamIDs::harmonyEnvelope (0, "Decay").toRawUTF8(), 300.0f);
        rig.set (ParamIDs::harmonyEnvelope (0, "Sustain").toRawUTF8(), 0.0f);
        rig.processor.setHarmonyModSlot (targetOf (ParamIDs::harmonyNoteLevel), 0, { HarmonyMod::a1, -1.0f });
        const auto out = runWithMidi (rig, sung, { { 0.1, keyDown (60) }, { 0.7, keyDown (67) } });
        const auto c4Settled = levelBetween (out, hz (60), 0.75, 0.85);
        const auto g4Starting = levelBetween (out, hz (67), 0.71, 0.81);
        const auto g4Settled = levelBetween (out, hz (67), 1.2, 1.35);
        CAPTURE (c4Settled, g4Starting, g4Settled);
        CHECK (c4Settled > 0.3f * levelAt (sung, A3, 0.5));
        CHECK (g4Starting < 0.5f * g4Settled);
    }

    SECTION ("Velocity on NOTES WIDTH: a soft key sits nearer the middle than a hard one")
    {
        const auto sung = sungVowel (A3, 1.0);
        MidiRig rig;
        rig.set (ParamIDs::harmonyNoteWidth, 0.0f);
        rig.processor.setHarmonyModSlot (targetOf (ParamIDs::harmonyNoteWidth), 0, { HarmonyMod::velocity, 1.0f });
        runWithMidi (rig, sung, { { 0.0, keyDown (60, 0.2f) }, { 0.0, keyDown (67, 1.0f) } });
        auto& harmony = rig.processor.getHarmony();
        std::map<int, float> pans;
        for (int v = 0; v < effects::Harmony::numNoteVoices; ++v)
            if (harmony.getNoteVoiceNote (v) >= 0)
                pans[harmony.getNoteVoiceNote (v)] = harmony.getNoteVoicePan (v);
        CAPTURE (pans[60], pans[67]);
        REQUIRE (pans.size() == 2);
        CHECK_THAT (pans[60], WithinAbs (-0.2, 0.03));
        CHECK_THAT (pans[67], WithinAbs (1.0, 0.03));
    }
}

TEST_CASE ("HARMONY snapshots keep the modulators on their controls; drawn shapes are saved", "[effects][harmony][modulators]")
{
    HarmonyRig rig (7.0f);
    const auto pitch = ParagonX1AudioProcessor::harmonyModTarget (ParamIDs::harmonyVoice (0, "Pitch"));
    const auto spread = ParagonX1AudioProcessor::harmonyModTarget (ParamIDs::harmonySpread);   // not in a snapshot (the mix)

    SECTION ("Store, change, recall: the slots on a voice come back; others stay")
    {
        rig.processor.setHarmonyModSlot (pitch, 0, { HarmonyMod::m1, 0.5f });
        rig.processor.setHarmonyModSlot (spread, 0, { HarmonyMod::am, 1.0f });
        rig.processor.storeHarmonySnapshot (0);
        rig.processor.setHarmonyModSlot (pitch, 0, { HarmonyMod::m2, -0.25f });
        rig.processor.setHarmonyModSlot (pitch, 1, { HarmonyMod::a1, 1.0f });
        rig.processor.setHarmonyModSlot (spread, 0, { HarmonyMod::pt, 0.5f });
        rig.processor.recallHarmonySnapshot (0);
        CHECK (rig.processor.getHarmonyModSlot (pitch, 0) == Rig::HarmonyModSlot { HarmonyMod::m1, 0.5f });
        CHECK (rig.processor.getHarmonyModSlot (pitch, 1).source == 0);
        CHECK (rig.processor.getHarmonyModSlot (spread, 0) == Rig::HarmonyModSlot { HarmonyMod::pt, 0.5f });

        // Through a rig file too.
        const auto loaded = Rig::fromXml (*rig.processor.captureRig().toXml());
        REQUIRE (loaded.has_value());
        CHECK (loaded->harmonySnapshots[0] == rig.processor.getHarmonySnapshot (0));
    }

    SECTION ("A snapshot from before stage 10 (no slots in it) leaves the slots alone")
    {
        rig.processor.storeHarmonySnapshot (0);
        auto old = *rig.processor.getHarmonySnapshot (0);
        for (auto it = old.begin(); it != old.end();)
            it = it->first.startsWith ("mod:") ? old.erase (it) : std::next (it);
        rig.processor.setHarmonySnapshot (0, old);
        rig.processor.setHarmonyModSlot (pitch, 0, { HarmonyMod::m3, 0.5f });
        rig.processor.recallHarmonySnapshot (0);
        CHECK (rig.processor.getHarmonyModSlot (pitch, 0) == Rig::HarmonyModSlot { HarmonyMod::m3, 0.5f });
    }

    SECTION ("A drawn shape goes with the project, the rig, and undo")
    {
        std::array<float, 32> shape {};
        for (size_t i = 0; i < shape.size(); ++i)
            shape[i] = (float) i / 31.0f * 2.0f - 1.0f;
        rig.processor.setHarmonyModShape (2, shape);
        CHECK_THAT (rig.processor.getHarmonyModShape (2)[1], WithinAbs (shape[1], 1.0e-4));   // kept to 1/10000
        shape = rig.processor.getHarmonyModShape (2);
        juce::MemoryBlock state;
        rig.processor.getStateInformation (state);
        HarmonyRig other (0.0f);
        other.processor.setStateInformation (state.getData(), (int) state.getSize());
        CHECK (other.processor.getHarmonyModShape (2) == shape);
        CHECK (other.processor.getHarmonyModShape (0) == std::array<float, 32> {});

        const auto loaded = Rig::fromXml (*rig.processor.captureRig().toXml());
        REQUIRE (loaded.has_value());
        CHECK (loaded->harmonyModShapes[2] == shape);

        rig.processor.flushUndoStep();
        rig.processor.setHarmonyModShape (2, {});
        rig.processor.flushUndoStep();
        rig.processor.undo();
        CHECK (rig.processor.getHarmonyModShape (2) == shape);
    }

    SECTION ("A drawn M1 on a voice's PITCH moves it as drawn")
    {
        std::array<float, 32> shape;
        shape.fill (0.5f);   // a flat line at half: +12 st at 100%
        rig.processor.setHarmonyModShape (0, shape);
        rig.set (ParamIDs::harmonyMod (0, "Shape").toRawUTF8(), (float) HarmonyMod::drawnShape);
        rig.setVoice ("Correct", 0.0f);
        modulate (rig, ParamIDs::harmonyVoice (0, "Pitch"), HarmonyMod::m1, 1.0f / 6.0f);   // +2 st
        rig.runMono (sungVowel (220.0, 1.0));
        CHECK_THAT (voiceShift (rig), WithinAbs (9.0, 0.2));
    }
}

//==============================================================================
// The VOCODER (inspired by iZotope VocalSynth 2's).
namespace
{
    using Voc = effects::Vocoder;

    struct VocoderRun
    {
        std::vector<float> left, right;
    };

    // Runs a vocoder over `voice` (the modulator, and the clean voice for
    // Auto), with `keys` (MIDI notes, velocity 1) held from the start.
    VocoderRun runVocoder (const Voc::Settings& settings, const std::vector<float>& voice, std::initializer_list<int> keys = {},
                           const std::vector<float>* input = nullptr)
    {
        Voc vocoder;
        vocoder.prepare (sampleRate, blockSize);
        juce::MidiBuffer midi;
        for (const auto key : keys)
            midi.addEvent (juce::MidiMessage::noteOn (1, key, 1.0f), 0);
        vocoder.handleMidi (midi);
        VocoderRun out;
        juce::AudioBuffer<float> buffer (2, blockSize);
        for (size_t start = 0; start + blockSize <= voice.size(); start += blockSize)
        {
            buffer.clear();
            if (input != nullptr)
                for (int ch = 0; ch < 2; ++ch)
                    buffer.copyFrom (ch, 0, input->data() + start, blockSize);
            juce::dsp::AudioBlock<float> block (buffer);
            vocoder.process (block, voice.data() + start, voice.data() + start, settings);
            out.left.insert (out.left.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
            out.right.insert (out.right.end(), buffer.getReadPointer (1), buffer.getReadPointer (1) + blockSize);
        }
        return out;
    }

    std::vector<float> sine (double hz, double seconds, float amplitude = 0.1f)
    {
        std::vector<float> samples ((size_t) (seconds * sampleRate));
        for (size_t i = 0; i < samples.size(); ++i)
            samples[i] = amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) i / sampleRate);
        return samples;
    }

    Voc::Settings vocoderOn (Voc::Carrier carrier = Voc::Carrier::midi)
    {
        Voc::Settings settings;
        settings.on = true;
        settings.carrier = carrier;
        return settings;
    }
}

TEST_CASE ("VOCODER: the synth takes the voice's shape", "[effects][vocoder]")
{
    const auto settings = vocoderOn();

    SECTION ("A voice low in pitch brings out the synth's low harmonics; a high one, its high ones")
    {
        // The synth: a saw on A2 (110 Hz). Its 4th harmonic is 440 Hz, its 23rd 2530 Hz.
        const auto low = runVocoder (settings, sine (440.0, 1.0), { 45 }).left;
        const auto high = runVocoder (settings, sine (2530.0, 1.0), { 45 }).left;
        CAPTURE (levelAt (low, 440.0, 0.5), levelAt (low, 2530.0, 0.5), levelAt (high, 440.0, 0.5), levelAt (high, 2530.0, 0.5));
        CHECK (levelAt (low, 440.0, 0.5) > 8.0f * levelAt (low, 2530.0, 0.5));
        CHECK (levelAt (high, 2530.0, 0.5) > 8.0f * levelAt (high, 440.0, 0.5));
    }

    SECTION ("MIDI: the synth's pitch is the key's, not the voice's")
    {
        const auto out = runVocoder (settings, sungVowel (220.0, 1.0), { 45 }).left;   // sing A3, play A2
        CHECK (std::abs (cents (pitchOf (out), 110.0)) < 10.0);
    }

    SECTION ("POLY: two keys, both notes")
    {
        auto poly = settings;
        poly.voicing = Voc::Voicing::poly;
        const auto out = runVocoder (poly, sungVowel (220.0, 1.0, 600.0), { 48, 55 }).left;   // C3 and G3
        const auto c3 = 130.81, g3 = 196.0;
        CAPTURE (levelAt (out, c3 * 4, 0.5), levelAt (out, g3 * 3, 0.5), levelAt (out, 150.0, 0.5));
        CHECK (levelAt (out, c3 * 4, 0.5) > 4.0f * levelAt (out, 450.0, 0.5));   // a harmonic of each, against a frequency in neither
        CHECK (levelAt (out, g3 * 3, 0.5) > 4.0f * levelAt (out, 450.0, 0.5));
    }

    SECTION ("No voice, no sound (the synth never leaks), even with a key held; no key, no sound")
    {
        CHECK (rms (runVocoder (settings, std::vector<float> ((size_t) sampleRate, 0.0f), { 45 }).left) < 1.0e-5f);
        CHECK (rms (runVocoder (settings, sungVowel (220.0, 1.0), {}).left, 0.3) < 1.0e-4f);
    }

    SECTION ("About as loud as the voice, in every MODE")
    {
        const auto voice = sungVowel (220.0, 1.0);
        for (int mode = 0; mode < 3; ++mode)
        {
            auto withMode = settings;
            withMode.mode = (Voc::Mode) mode;
            const auto out = runVocoder (withMode, voice, { 45 });
            const auto difference = dB (rms (out.left, 0.4) / rms (voice, 0.4));
            CAPTURE (mode, difference);
            CHECK (std::abs (difference) < 1.5f);
        }
    }

    SECTION ("Off: what comes in goes out untouched")
    {
        Voc::Settings off;
        const auto voice = sungVowel (220.0, 0.5);
        const auto out = runVocoder (off, voice, { 45 }, &voice);
        CHECK (maxAbsDifference (out.left, voice) < 1.0e-6f);
    }

    SECTION ("DRY: what came in, under the vocoder")
    {
        auto withDry = settings;
        withDry.dryDb = 0.0f;
        withDry.levelDb = Voc::offDb;
        const auto voice = sungVowel (220.0, 0.5);
        const auto out = runVocoder (withDry, voice, { 45 }, &voice);
        CHECK (maxAbsDifference (out.left, voice, (size_t) (0.1 * sampleRate)) < 1.0e-4f);
    }
}

TEST_CASE ("VOCODER: Auto plays what you sing, and the INTERVAL voices", "[effects][vocoder]")
{
    auto settings = vocoderOn (Voc::Carrier::autoPitch);
    const auto A3 = 220.0, E4 = 329.63;
    const auto sung = sungVowel (A3, 1.2, 700.0);

    SECTION ("The synth follows the sung pitch")
    {
        CHECK (std::abs (cents (pitchOf (runVocoder (settings, sung).left), A3)) < 10.0);
        CHECK (std::abs (cents (pitchOf (runVocoder (settings, sungVowel (174.61, 1.2)).left), 174.61)) < 10.0);   // F3
    }

    SECTION ("5th Up in C major on an A: the E above, a diatonic fifth")
    {
        settings.intervals[0] = Voc::intervalNames.indexOf ("5th Up");
        settings.intervalDb = 0.0f;
        const auto out = runVocoder (settings, sung).left;
        const auto plain = runVocoder (vocoderOn (Voc::Carrier::autoPitch), sung).left;
        CAPTURE (levelAt (out, E4, 0.6), levelAt (plain, E4, 0.6));
        CHECK (levelAt (out, E4, 0.6) > 3.0f * levelAt (plain, E4, 0.6));   // the E itself: none of A's harmonics is near it
    }

    SECTION ("3rd Up keeps to the key: in A minor, C's third up is E, not E flat")
    {
        settings.key = 9;   // A
        settings.scaleType = Scales::Scale::naturalMinor;
        settings.intervals[0] = Voc::intervalNames.indexOf ("3rd Up");
        settings.intervalDb = 0.0f;
        const auto out = runVocoder (settings, sungVowel (261.63, 1.2, 700.0)).left;   // C4
        const auto plain = runVocoder (vocoderOn (Voc::Carrier::autoPitch), sungVowel (261.63, 1.2, 700.0)).left;
        CHECK (levelAt (out, E4, 0.6) > 3.0f * levelAt (plain, E4, 0.6));
        CHECK (levelAt (out, 311.13, 0.6) < 0.1f * levelAt (out, E4, 0.6));   // not E flat
    }
}

TEST_CASE ("VOCODER: SHIFT, the bands' VOL and PAN, PAN, the cuts", "[effects][vocoder]")
{
    const auto settings = vocoderOn();

    SECTION ("SHIFT +12: the voice's 440 Hz comes out an octave higher, at 880 Hz")
    {
        auto shifted = settings;
        shifted.shift = 12.0f;
        const auto out = runVocoder (shifted, sine (440.0, 1.0), { 45 }).left;
        CAPTURE (levelAt (out, 880.0, 0.5), levelAt (out, 440.0, 0.5));
        CHECK (levelAt (out, 880.0, 0.5) > 3.0f * levelAt (out, 440.0, 0.5));
    }

    SECTION ("Band VOL: the lower half off takes the low voice away, not the high one")
    {
        auto cut = settings;
        for (int p = 0; p < Voc::curvePoints / 2; ++p)
            cut.bandDb[(size_t) p] = Voc::minBandDb;
        const auto lowPlain = rms (runVocoder (settings, sine (300.0, 1.0), { 45 }).left, 0.5);
        const auto lowCut = rms (runVocoder (cut, sine (300.0, 1.0), { 45 }).left, 0.5);
        const auto highPlain = rms (runVocoder (settings, sine (4000.0, 1.0), { 45 }).left, 0.5);
        const auto highCut = rms (runVocoder (cut, sine (4000.0, 1.0), { 45 }).left, 0.5);
        CAPTURE (lowPlain, lowCut, highPlain, highCut);
        CHECK (lowCut < 0.1f * lowPlain);
        CHECK (std::abs (dB (highCut / highPlain)) < 1.0f);
    }

    SECTION ("Band PAN all left: nothing on the right; the module's PAN right: nothing on the left")
    {
        auto left = settings;
        left.bandPan.fill (-1.0f);
        const auto out = runVocoder (left, sungVowel (220.0, 1.0), { 45 });
        CHECK (rms (out.right, 0.3) < 1.0e-5f);
        CHECK (rms (out.left, 0.3) > 1.0e-3f);
        auto right = settings;
        right.pan = 1.0f;
        const auto panned = runVocoder (right, sungVowel (220.0, 1.0), { 45 });
        CHECK (rms (panned.left, 0.3) < 1.0e-5f);
    }

    SECTION ("HIGH CUT at 1 kHz takes the top off")
    {
        auto cut = settings;
        cut.highCutHz = 1000.0f;
        const auto plain = runVocoder (settings, sine (4000.0, 1.0), { 45 }).left;
        const auto dull = runVocoder (cut, sine (4000.0, 1.0), { 45 }).left;
        CHECK (rms (dull, 0.5) < 0.1f * rms (plain, 0.5));
    }
}

TEST_CASE ("VOCODER: the synth's oscillators and noise", "[effects][vocoder]")
{
    SECTION ("A square has no even harmonics; a saw has them")
    {
        std::vector<float> square, saw;
        auto phase = 0.0f;
        const auto increment = (float) (110.0 / sampleRate);
        for (int i = 0; i < (int) sampleRate; ++i)
        {
            square.push_back (Voc::waveSample (Voc::square, phase, increment, 0.0f));
            saw.push_back (Voc::waveSample (Voc::saw, phase, increment, 0.0f));
            phase += increment;
            phase -= std::floor (phase);
        }
        CHECK (levelAt (square, 220.0, 0.0) < 0.02f * levelAt (square, 110.0, 0.0));
        CHECK (levelAt (saw, 220.0, 0.0) > 0.4f * levelAt (saw, 110.0, 0.0));
    }

    SECTION ("COARSE -12 on the only oscillator: the synth an octave down")
    {
        auto settings = vocoderOn();
        settings.osc[0].coarse = -12.0f;
        const auto out = runVocoder (settings, sungVowel (220.0, 1.0), { 57 }).left;   // A3 key, an octave down: A2
        CHECK (std::abs (cents (pitchOf (out), 110.0)) < 10.0);
    }

    SECTION ("NOISE alone (oscillator off) still speaks: the consonant-like hiss shaped by the voice")
    {
        auto settings = vocoderOn();
        settings.osc[0].on = false;
        settings.noise.on = true;
        settings.noise.gainDb = 0.0f;
        settings.noise.crush = 1.0f;
        const auto out = runVocoder (settings, sine (3000.0, 1.0), { 45 }).left;
        CHECK (levelAt (out, 3000.0, 0.5) > 0.0f);
        CHECK (rms (out, 0.5) > 0.01f);
    }
}

TEST_CASE ("VOCODER in the plugin: on by its switch, Auto or MIDI", "[effects][vocoder]")
{
    const auto sung = sungVowel (220.0, 1.2, 700.0);

    SECTION ("Auto: the robot voice sings your note; off: your voice untouched")
    {
        EffectRig rig;
        rig.set (ParamIDs::vocoderOn, 1.0f);
        const auto out = rig.runMono (sung);
        CHECK (std::abs (cents (pitchOf (out), 220.0)) < 10.0);
        EffectRig plain;
        EffectRig off;
        off.set (ParamIDs::vocoderMode, 2.0f);   // anything but on
        CHECK (maxAbsDifference (plain.runMono (sung), off.runMono (sung)) < 1.0e-6f);
    }

    SECTION ("MIDI: the key you play")
    {
        EffectRig rig;
        rig.set (ParamIDs::vocoderOn, 1.0f);
        rig.set (ParamIDs::vocoderCarrier, 1.0f);
        const auto out = runWithMidi (rig, sung, { { 0.0, keyDown (45) } });
        CHECK (std::abs (cents (pitchOf (out), 110.0)) < 10.0);
    }
}

TEST_CASE ("VOCODER: the heaviest setting runs well within real time", "[effects][vocoder][.cpu]")
{
    auto settings = vocoderOn();
    settings.mode = Voc::Mode::hard;
    settings.voicing = Voc::Voicing::poly;
    settings.osc[0].wave = settings.osc[1].wave = Voc::saws;
    settings.osc[1].on = true;
    settings.noise.on = true;
    const auto voice = sungVowel (220.0, 5.0);
    const auto started = juce::Time::getMillisecondCounterHiRes();
    runVocoder (settings, voice, { 48, 52, 55, 59, 62, 65, 69, 72 });
    const auto seconds = (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0;
    WARN ("5 s of audio in " << seconds << " s: " << seconds / 5.0 * 100.0 << "% of a core");
    CHECK (seconds < 5.0);
}
