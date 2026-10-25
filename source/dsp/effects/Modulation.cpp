#include "Modulation.h"

namespace effects
{
    namespace
    {
        constexpr float twoPi = juce::MathConstants<float>::twoPi;

        // Delay times in milliseconds: the centre of the sweep and how far
        // full depth swings either side of it.
        struct DelaySweep
        {
            float centreMs, sweepMs, feedback;
        };
        constexpr DelaySweep chorusSweep { 14.0f, 6.0f, 0.0f };

        constexpr float maxDelayMs = 40.0f;

        // A one-pole low-pass coefficient for a cutoff, and a smoothing
        // coefficient for a time constant.
        float onePole (double sampleRate, float hz) { return 1.0f - std::exp (-twoPi * hz / (float) sampleRate); }
        float smoothing (double sampleRate, float seconds) { return 1.0f - std::exp (-1.0f / (seconds * (float) sampleRate)); }

        // A triangle wave from a 0-1 phase, -1 to 1: the Dimension's LFO shape.
        float triangle (float phase) { phase -= std::floor (phase); return 4.0f * std::abs (phase - 0.5f) - 1.0f; }
    }

    Modulation::Modulation() : delayLine (8192), hornDelay (2048), drumDelay (2048) {}

    void Modulation::prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, (juce::uint32) maxChannels };
        const juce::dsp::ProcessSpec monoSpec { sampleRate, (juce::uint32) maxBlockSize, 1 };

        delayLine.setMaximumDelayInSamples ((int) std::ceil (maxDelayMs * 0.001 * sampleRate) + 4);
        delayLine.prepare (spec);
        phaser.prepare (spec);
        phaser.setCentreFrequency (900.0f);
        phaser.setFeedback (0.5f);

        for (auto* rotor : { &hornDelay, &drumDelay })
        {
            rotor->setMaximumDelayInSamples ((int) std::ceil (0.004 * sampleRate) + 4);
            rotor->prepare (monoSpec);
        }
        rotaryCrossover.setCutoffFrequency (800.0f);   // a real Leslie's crossover frequency
        rotaryCrossover.prepare (monoSpec);
        harmonicCrossover.setCutoffFrequency (900.0f);
        harmonicCrossover.prepare (spec);

        depth.reset (sampleRate, 0.05);
        mix.reset (sampleRate, 0.05);
        lfoPhase = hornPhase = drumPhase = 0.0f;
        hornHz = 0.8f;
        drumHz = 0.7f;
        hornTone = lampGlow = 0.0f;
        std::fill (std::begin (feedbackSample), std::end (feedbackSample), 0.0f);
        std::fill (std::begin (dimensionTone), std::end (dimensionTone), 0.0f);
        for (auto& channel : vibeStages)
            for (auto& stage : channel)
                stage = {};
        bypass.prepare (sampleRate, maxBlockSize);
        aeon.prepare (sampleRate, maxBlockSize);
    }

    float Modulation::advanceLfo (float rateHz) noexcept
    {
        lfoPhase += rateHz / (float) sampleRate;
        lfoPhase -= std::floor (lfoPhase);
        return lfoPhase;
    }

    void Modulation::process (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        bypass.process (block, settings.on, [&] (juce::dsp::AudioBlock<float> wet)
        {
            depth.setTargetValue (settings.depth);
            mix.setTargetValue (settings.mix);

            // Switched to AEON: start with its delay empty, not holding old echoes.
            if (settings.type != lastType && settings.type == Type::flanger)
                aeon.reset();
            lastType = settings.type;

            switch (settings.type)
            {
                case Type::chorus:
                    processDelayBased (wet, settings);
                    break;

                case Type::flanger:
                    aeon.process (wet, { settings.manual, settings.depth, settings.rateHz, settings.resonance });
                    depth.skip ((int) wet.getNumSamples());
                    mix.skip ((int) wet.getNumSamples());
                    break;

                case Type::phaser:
                {
                    phaser.setRate (settings.rateHz);
                    phaser.setDepth (settings.depth);
                    phaser.setMix (settings.mix);
                    juce::dsp::ProcessContextReplacing<float> context (wet);
                    phaser.process (context);
                    depth.skip ((int) wet.getNumSamples());
                    mix.skip ((int) wet.getNumSamples());
                    break;
                }

                case Type::tremolo:          processTremolo (wet, settings); break;
                case Type::dimension:        processDimension (wet, settings); break;
                case Type::uniVibe:          processUniVibe (wet, settings); break;
                case Type::rotary:           processRotary (wet, settings); break;
                case Type::vibrato:          processVibrato (wet, settings); break;
                case Type::harmonicTremolo:  processHarmonicTremolo (wet, settings); break;
                case Type::autoPan:          processAutoPan (wet, settings); break;
            }
        });
    }

    void Modulation::processDelayBased (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        const auto& sweep = chorusSweep;
        const auto msToSamples = (float) sampleRate * 0.001f;
        const auto numChannels = (int) block.getNumChannels();

        for (size_t i = 0; i < block.getNumSamples(); ++i)
        {
            const auto phase = advanceLfo (settings.rateHz);
            const auto depthNow = depth.getNextValue();
            const auto mixNow = mix.getNextValue();

            for (int ch = 0; ch < numChannels; ++ch)
            {
                // Right channel's LFO runs a quarter-cycle behind the left.
                const auto lfo = std::sin (twoPi * (phase + 0.25f * (float) ch));
                const auto delayMs = sweep.centreMs + sweep.sweepMs * depthNow * lfo;

                auto* samples = block.getChannelPointer ((size_t) ch);
                const auto dry = samples[i];
                delayLine.pushSample (ch, dry + feedbackSample[ch] * sweep.feedback);
                const auto delayed = delayLine.popSample (ch, delayMs * msToSamples);
                feedbackSample[ch] = delayed;

                samples[i] = dry * (1.0f - mixNow) + delayed * mixNow;
            }
        }
    }

    void Modulation::processTremolo (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        for (size_t i = 0; i < block.getNumSamples(); ++i)
        {
            const auto lfo = 0.5f + 0.5f * std::sin (twoPi * advanceLfo (settings.rateHz));
            const auto gain = 1.0f - depth.getNextValue() * lfo;
            mix.skip (1);

            for (size_t ch = 0; ch < block.getNumChannels(); ++ch)
                block.getChannelPointer (ch)[i] *= gain;
        }
    }

    void Modulation::processDimension (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        // Two chorus voices, swept by triangle LFOs in opposite directions
        // (one rises while the other falls), so their pitch changes largely
        // cancel: lush and wide without the seasick wobble of a single
        // chorus. Each side hears mostly one voice, with a little of the
        // other inverted, as the Dimension D's outputs do. The wet path is
        // darkened like the bucket-brigade delay chips of the originals.
        const auto msToSamples = (float) sampleRate * 0.001f;
        const auto numChannels = (int) block.getNumChannels();
        const auto toneCoefficient = onePole (sampleRate, 6500.0f);

        for (size_t i = 0; i < block.getNumSamples(); ++i)
        {
            const auto phase = advanceLfo (settings.rateHz);
            const auto depthNow = depth.getNextValue();
            const auto mixNow = mix.getNextValue();
            const auto sweepMs = 0.6f + 2.4f * depthNow;
            const auto delayA = (7.5f + sweepMs * triangle (phase)) * msToSamples;
            const auto delayB = (7.5f + sweepMs * triangle (phase + 0.5f)) * msToSamples;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* samples = block.getChannelPointer ((size_t) ch);
                const auto dry = samples[i];
                delayLine.pushSample (ch, dry);
                const auto voiceA = delayLine.popSample (ch, delayA, false);
                const auto voiceB = delayLine.popSample (ch, delayB, true);

                // Left: voice A with a little of B inverted; right: the other way round.
                auto wet = numChannels == 1 ? 0.5f * (voiceA + voiceB)
                         : ch == 0           ? voiceA - 0.3f * voiceB
                                             : voiceB - 0.3f * voiceA;
                dimensionTone[ch] += (wet - dimensionTone[ch]) * toneCoefficient;
                wet = dimensionTone[ch];

                samples[i] = dry * (1.0f - mixNow) + wet * mixNow;
            }
        }
    }

    void Modulation::processUniVibe (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        // Four first-order all-pass stages tuned far apart (the original's
        // stages use very different capacitors), all swept together by a
        // lamp. The lamp is what gives the Vibe its throb: it brightens fast
        // and dims slowly, and spends longer dark than bright, so the sweep
        // is lopsided rather than a smooth sine. Wet alone is the original's
        // VIBRATO setting; mixed half and half with the dry signal it's the
        // CHORUS setting, where the phase shifts become moving notches.
        constexpr float stageHz[4] = { 70.0f, 260.0f, 950.0f, 3400.0f };
        const auto numChannels = (int) block.getNumChannels();
        const auto glowUp = smoothing (sampleRate, 0.004f), glowDown = smoothing (sampleRate, 0.03f);
        const auto nyquistGuard = (float) sampleRate * 0.45f;

        for (size_t i = 0; i < block.getNumSamples(); ++i)
        {
            const auto depthNow = depth.getNextValue();
            const auto mixNow = mix.getNextValue();

            const auto drive = std::pow (0.5f + 0.5f * std::sin (twoPi * advanceLfo (settings.rateHz)), 1.7f);
            lampGlow += (drive - lampGlow) * (drive > lampGlow ? glowUp : glowDown);
            const auto sweep = std::exp2 (3.0f * depthNow * lampGlow);   // up to three octaves

            float coefficients[4];
            for (int stage = 0; stage < 4; ++stage)
            {
                const auto t = std::tan (juce::MathConstants<float>::pi * juce::jmin (stageHz[stage] * sweep, nyquistGuard) / (float) sampleRate);
                coefficients[stage] = (t - 1.0f) / (t + 1.0f);
            }

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* samples = block.getChannelPointer ((size_t) ch);
                const auto dry = samples[i];
                auto wet = dry;
                for (int stage = 0; stage < 4; ++stage)
                {
                    auto& state = vibeStages[ch][stage];
                    const auto out = coefficients[stage] * wet + state.x1 - coefficients[stage] * state.y1;
                    state.x1 = wet;
                    state.y1 = out;
                    wet = out;
                }
                samples[i] = dry * (1.0f - mixNow) + wet * mixNow;
            }
        }
    }

    void Modulation::processRotary (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        // A Leslie speaker: the signal splits at 800 Hz into a treble horn and
        // a bass drum, each spinning. As each turns towards and away from the
        // two microphones its pitch shifts (Doppler: a delay that sweeps) and
        // its level rises and falls. RATE sets the horn's speed; the drum
        // turns a little slower. Both have inertia, the heavy drum far more
        // than the horn, so changing RATE gives the classic ramp: the horn
        // spins up first and the drum catches up seconds later.
        const auto numChannels = (int) block.getNumChannels();
        const auto msToSamples = (float) sampleRate * 0.001f;
        const auto hornInertia = smoothing (sampleRate, 0.7f), drumInertia = smoothing (sampleRate, 3.0f);
        const auto hornToneCoefficient = onePole (sampleRate, 5500.0f);
        constexpr float micOffset = 0.3f;   // the right mic sits a third of a turn round from the left

        for (size_t i = 0; i < block.getNumSamples(); ++i)
        {
            const auto intensity = 0.35f + 0.65f * depth.getNextValue();
            const auto mixNow = mix.getNextValue();

            hornHz += (settings.rateHz - hornHz) * hornInertia;
            drumHz += (settings.rateHz * 0.86f - drumHz) * drumInertia;
            hornPhase += hornHz / (float) sampleRate;
            hornPhase -= std::floor (hornPhase);
            drumPhase += drumHz / (float) sampleRate;
            drumPhase -= std::floor (drumPhase);

            // Both channels carry the same signal at this point in the chain.
            float input = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                input += block.getChannelPointer ((size_t) ch)[i];
            input /= (float) numChannels;

            float bass = 0.0f, treble = 0.0f;
            rotaryCrossover.processSample (0, input, bass, treble);
            hornTone += (treble - hornTone) * hornToneCoefficient;   // the horn's own mellow top end
            hornDelay.pushSample (0, hornTone);
            drumDelay.pushSample (0, bass);

            float out[maxChannels] {};
            for (int ch = 0; ch < juce::jmin (numChannels, maxChannels); ++ch)
            {
                const auto hornAngle = twoPi * (hornPhase + micOffset * (float) ch);
                const auto drumAngle = twoPi * (drumPhase + micOffset * (float) ch);
                const auto last = ch == numChannels - 1;

                const auto hornSample = hornDelay.popSample (0, (1.2f + 0.35f * intensity * std::sin (hornAngle)) * msToSamples, last);
                const auto drumSample = drumDelay.popSample (0, (1.2f + 0.15f * intensity * std::sin (drumAngle)) * msToSamples, last);

                const auto hornLevel = 1.0f - 0.55f * intensity * (0.5f - 0.5f * std::cos (hornAngle));
                const auto drumLevel = 1.0f - 0.3f * intensity * (0.5f - 0.5f * std::cos (drumAngle));
                out[ch] = hornSample * hornLevel + drumSample * drumLevel;
            }

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& sample = block.getChannelPointer ((size_t) ch)[i];
                sample = sample * (1.0f - mixNow) + out[juce::jmin (ch, maxChannels - 1)] * mixNow;
            }
        }
    }

    void Modulation::processVibrato (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        // Only the delayed copy, with its delay swept: the pitch itself
        // wavers, with no dry signal to turn it into a chorus.
        const auto msToSamples = (float) sampleRate * 0.001f;
        const auto numChannels = (int) block.getNumChannels();

        for (size_t i = 0; i < block.getNumSamples(); ++i)
        {
            const auto lfo = std::sin (twoPi * advanceLfo (settings.rateHz));
            const auto delayMs = 5.0f + 3.5f * depth.getNextValue() * lfo;
            mix.skip (1);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* samples = block.getChannelPointer ((size_t) ch);
                delayLine.pushSample (ch, samples[i]);
                samples[i] = delayLine.popSample (ch, delayMs * msToSamples);
            }
        }
    }

    void Modulation::processHarmonicTremolo (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        // Splits the sound at 900 Hz and pulses the lows and highs in turn,
        // as the "brownface" Fender amps of the early '60s did: a softer,
        // swirling tremolo with a hint of phaser, rather than an on-off chop.
        const auto numChannels = (int) block.getNumChannels();
        for (size_t i = 0; i < block.getNumSamples(); ++i)
        {
            const auto lfo = 0.5f + 0.5f * std::sin (twoPi * advanceLfo (settings.rateHz));
            const auto depthNow = depth.getNextValue();
            mix.skip (1);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& sample = block.getChannelPointer ((size_t) ch)[i];
                float low = 0.0f, high = 0.0f;
                harmonicCrossover.processSample (ch, sample, low, high);
                sample = low * (1.0f - depthNow * lfo) + high * (1.0f - depthNow * (1.0f - lfo));
            }
        }
    }

    void Modulation::processAutoPan (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        // Sweeps the sound from side to side. Equal-power panning keeps the
        // loudness steady as it moves; at full depth it reaches each side.
        if (block.getNumChannels() < 2)
        {
            depth.skip ((int) block.getNumSamples());
            mix.skip ((int) block.getNumSamples());
            return;   // a mono track can't pan
        }

        auto* left = block.getChannelPointer (0);
        auto* right = block.getChannelPointer (1);
        for (size_t i = 0; i < block.getNumSamples(); ++i)
        {
            const auto lfo = std::sin (twoPi * advanceLfo (settings.rateHz));
            const auto angle = juce::MathConstants<float>::halfPi * 0.5f * (1.0f + depth.getNextValue() * lfo);
            mix.skip (1);
            left[i] *= juce::MathConstants<float>::sqrt2 * std::cos (angle);
            right[i] *= juce::MathConstants<float>::sqrt2 * std::sin (angle);
        }
    }
}
