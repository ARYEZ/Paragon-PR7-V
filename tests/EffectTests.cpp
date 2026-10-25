#include <catch2/catch_test_macros.hpp>
#include <set>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "dsp/effects/Modulation.h"
#include "dsp/effects/Delay.h"
#include "dsp/effects/Mars.h"
#include "dsp/effects/Reverb.h"
#include "dsp/effects/Glitch.h"
#include "dsp/effects/TalkBox.h"
#include "ui/EqPresets.h"

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

        // The curve's average power change over a guitar's range (80 Hz to
        // 5 kHz, sampled every 1/24 octave), plus LEVEL, is
        // close to nothing: switching presets changes the tone, not the volume.
        auto power = 0.0;
        auto count = 0;
        for (auto frequency = 80.0f; frequency <= 5000.0f; frequency *= std::pow (2.0f, 1.0f / 24.0f), ++count)
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
