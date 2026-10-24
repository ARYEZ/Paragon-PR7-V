#include <catch2/catch_test_macros.hpp>
#include <set>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginProcessor.h"
#include "dsp/effects/Modulation.h"
#include "dsp/effects/Delay.h"
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

TEST_CASE ("Fuzzes: Muff sustains with odd harmonics and scoops the mids; Face clips unevenly and cleans up", "[effects][drive]")
{
    const auto play = [] (effects::Drive::Model model, float amplitude, float drive = 5.0f, float tone = 5.0f, double hz = 220.0)
    {
        EffectRig rig;
        rig.set (ParamIDs::driveOn, 1.0f);
        rig.set (ParamIDs::driveModel, (float) model);
        rig.set (ParamIDs::driveGain, drive);
        rig.set (ParamIDs::driveTone, tone);
        const auto output = rig.runMono (sine (hz, amplitude, 1.0));
        REQUIRE (allFinite (output));
        return output;
    };

    SECTION ("At LEVEL 0 dB both sit near the guitar's own level, at any FUZZ / SUSTAIN")
    {
        for (const auto model : { effects::Drive::Model::muff, effects::Drive::Model::face })
            for (const auto drive : { 2.0f, 5.0f, 9.0f })
            {
                INFO ("model " << (int) model << ", drive " << drive);
                CHECK (std::abs (dB (rms (play (model, 0.3f, drive), 0.3) / (0.3f / std::sqrt (2.0f)))) < 3.0f);
            }
    }

    SECTION ("Muff clips both halves alike (odd harmonics); Face clips one harder (a strong 2nd)")
    {
        const auto secondToThird = [&] (effects::Drive::Model model)
        {
            const auto output = play (model, 0.3f, 7.0f, 7.0f);
            return dB (levelAt (output, 440.0, 0.3) / levelAt (output, 660.0, 0.3));
        };
        CHECK (secondToThird (effects::Drive::Model::face) > secondToThird (effects::Drive::Model::muff) + 10.0f);
    }

    SECTION ("Face cleans up as the guitar's volume comes down")
    {
        const auto harmonics = [&] (float amplitude)
        {
            const auto output = play (effects::Drive::Model::face, amplitude, 5.0f);
            return dB ((levelAt (output, 440.0, 0.3) + levelAt (output, 660.0, 0.3)) / levelAt (output, 220.0, 0.3));
        };
        CHECK (harmonics (0.3f) > harmonics (0.01f) + 10.0f);
    }

    SECTION ("Muff's TONE scoops the mids at noon")
    {
        // A quiet 1 kHz tone (still fuzzed), against 250 Hz, through the tone stack.
        const auto midsAgainstLows = [&] (float tone)
        {
            const auto mids = play (effects::Drive::Model::muff, 0.3f, 5.0f, tone, 1000.0);
            const auto lows = play (effects::Drive::Model::muff, 0.3f, 5.0f, tone, 250.0);
            return dB (rms (mids, 0.3) / rms (lows, 0.3));
        };
        CHECK (midsAgainstLows (5.0f) < midsAgainstLows (10.0f) - 3.0f);
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

TEST_CASE ("Overdrive adds harmonics, and oversampling keeps aliasing down", "[effects][drive]")
{
    EffectRig rig;
    rig.set (ParamIDs::driveOn, 1.0f);
    rig.set (ParamIDs::driveModel, (float) effects::Drive::Model::classic);
    rig.set (ParamIDs::driveTone, 10.0f);

    SECTION ("Harmonics: odd from clipping, even from the asymmetry")
    {
        rig.set (ParamIDs::driveGain, 7.0f);
        const auto output = rig.runMono (sine (1000.0, 0.3f, 0.5));
        REQUIRE (allFinite (output));
        const auto fundamental = levelAt (output, 1000.0);
        CHECK (dB (levelAt (output, 3000.0) / fundamental) > -30.0f);
        CHECK (dB (levelAt (output, 2000.0) / fundamental) > -40.0f);
    }

    SECTION ("Aliasing")
    {
        // A 5 kHz note at full drive makes harmonics at 15, 25, 35 kHz...
        // Without oversampling, 35 kHz folds back to 13 kHz (48 - 35), a
        // frequency that isn't musically related to the note.
        rig.set (ParamIDs::driveGain, 10.0f);
        const auto output = rig.runMono (sine (5000.0, 0.5f, 0.5));
        CHECK (dB (levelAt (output, 13000.0) / levelAt (output, 5000.0)) < -45.0f);
    }

    SECTION ("Drive 0 at level 0 dB is close to unity; level adds gain")
    {
        rig.set (ParamIDs::driveGain, 0.0f);
        const auto input = sine (1000.0, 0.1f, 0.5);
        CHECK_THAT (dB (rms (rig.runMono (input), 0.2) / rms (input, 0.2)), WithinAbs (0.0, 1.0));

        rig.set (ParamIDs::driveLevel, 6.0f);
        CHECK_THAT (dB (rms (rig.runMono (input), 0.2) / rms (input, 0.2)), WithinAbs (6.0, 1.0));
    }
}

TEST_CASE ("The Screamer drive behaves like a Tube Screamer", "[effects][drive]")
{
    EffectRig rig;
    rig.set (ParamIDs::driveOn, 1.0f);
    rig.set (ParamIDs::driveModel, (float) effects::Drive::Model::screamer);

    SECTION ("It's the default for new rigs")
    {
        EffectRig fresh;
        CHECK (juce::roundToInt (fresh.processor.getParameters().getRawParameterValue (ParamIDs::driveModel)->load())
               == (int) effects::Drive::Model::screamer);
    }

    SECTION ("Mid hump: the bass passes clean while the mids are boosted")
    {
        // A quiet signal, so nothing clips and this is just the frequency response.
        rig.set (ParamIDs::driveGain, 0.0f);
        rig.set (ParamIDs::driveTone, 10.0f);
        const auto gainAt = [&] (double hz)
        {
            const auto input = sine (hz, 0.003f, 0.5);
            return dB (rms (rig.runMono (input), 0.2) / rms (input, 0.2));
        };
        CHECK (gainAt (1000.0) - gainAt (100.0) > 9.0f);

        // TONE: dark at 0 (the top well below the mids), bright at 10.
        const auto brightTreble = gainAt (6000.0) - gainAt (1000.0);
        rig.set (ParamIDs::driveTone, 0.0f);
        const auto darkTreble = gainAt (6000.0) - gainAt (1000.0);
        CHECK (darkTreble < -10.0f);
        CHECK (brightTreble - darkTreble > 12.0f);
    }

    SECTION ("Its diodes clip both halves alike: odd harmonics, hardly any even")
    {
        rig.set (ParamIDs::driveGain, 7.0f);
        rig.set (ParamIDs::driveTone, 10.0f);
        const auto output = rig.runMono (sine (500.0, 0.3f, 0.5));
        REQUIRE (allFinite (output));
        const auto fundamental = levelAt (output, 500.0);
        CHECK (dB (levelAt (output, 1500.0) / fundamental) > -25.0f);
        CHECK (dB (levelAt (output, 1000.0) / fundamental) < -50.0f);
    }

    SECTION ("Its level at noon is close to the guitar's own")
    {
        rig.set (ParamIDs::driveGain, 5.0f);
        rig.set (ParamIDs::driveTone, 5.0f);
        const auto input = sine (440.0, 0.3f, 0.5);
        CHECK_THAT (dB (rms (rig.runMono (input), 0.2) / rms (input, 0.2)), WithinAbs (0.0, 3.0));
    }

    SECTION ("Aliasing stays down at full drive")
    {
        rig.set (ParamIDs::driveGain, 10.0f);
        rig.set (ParamIDs::driveTone, 10.0f);
        const auto output = rig.runMono (sine (5000.0, 0.5f, 0.5));
        CHECK (dB (levelAt (output, 13000.0) / levelAt (output, 5000.0)) < -45.0f);
    }
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

TEST_CASE ("DOUBLER adds a take on each side", "[effects][doubler]")
{
    EffectRig rig;
    rig.set (ParamIDs::doublerOn, 1.0f);
    rig.set (ParamIDs::doublerMix, 0.7f);
    const auto input = sine (440.0, 0.3f, 1.0);
    const auto output = rig.run (input);
    REQUIRE (allFinite (output[0]));
    REQUIRE (allFinite (output[1]));

    std::vector<float> difference (output[0].size());
    for (size_t i = 0; i < difference.size(); ++i)
        difference[i] = output[0][i] - output[1][i];
    CHECK (rms (difference, 0.2) > 0.1f * rms (output[0], 0.2));   // the two sides differ
    CHECK (rms (output[0], 0.2) < 2.0f * rms (input, 0.2));
}

TEST_CASE ("Projects saved before the drive MODEL keep the original overdrive", "[effects][drive]")
{
    EffectRig original;
    juce::MemoryBlock saved;
    original.processor.getStateInformation (saved);

    // As an older version saved it: no driveModel parameter at all.
    auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
    REQUIRE (xml != nullptr);
    for (auto* child : xml->getChildIterator())
        if (child->getStringAttribute ("id") == ParamIDs::driveModel)
        {
            xml->removeChildElement (child, true);
            break;
        }
    juce::MemoryBlock older;
    juce::AudioProcessor::copyXmlToBinary (*xml, older);

    const auto modelAfterLoading = [] (const juce::MemoryBlock& state)
    {
        EffectRig rig;
        rig.processor.setStateInformation (state.getData(), (int) state.getSize());
        return juce::roundToInt (rig.processor.getParameters().getRawParameterValue (ParamIDs::driveModel)->load());
    };
    CHECK (modelAfterLoading (older) == (int) effects::Drive::Model::classic);
    CHECK (modelAfterLoading (saved) == (int) effects::Drive::Model::screamer);
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
    CHECK_THAT (value (ParamIDs::doublerRate), WithinAbs (1.5, 0.01));
    CHECK_THAT (value (ParamIDs::doublerDepth), WithinAbs (0.3, 0.01));
    CHECK_THAT (value (ParamIDs::doublerMix), WithinAbs (0.6, 0.01));
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
    CHECK (currentXml->getIntAttribute ("parameterVersion") == 26);
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

    const auto input = sine (220.0, 0.3f, 5.0);
    const auto startMs = juce::Time::getMillisecondCounterHiRes();
    rig.runMono (input);
    const auto elapsedSeconds = (juce::Time::getMillisecondCounterHiRes() - startMs) / 1000.0;
    const auto realtimeFraction = elapsedSeconds / 5.0;

    WARN ("Full chain uses " << juce::String (realtimeFraction * 100.0, 1) << "% of one core in real time");
    CHECK (realtimeFraction < 0.5);
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
