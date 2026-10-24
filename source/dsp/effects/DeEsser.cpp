#include "DeEsser.h"

namespace effects
{
    namespace
    {
        float coefficient (double sampleRate, double seconds) { return (float) std::exp (-1.0 / (seconds * sampleRate)); }
    }

    void DeEsser::prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 1 };
        for (auto* filter : { &split, &split2, &detector })
        {
            filter->prepare (spec);
            filter->setResonance (1.0f / juce::MathConstants<float>::sqrt2);
        }
        split.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        split2.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        detector.setType (juce::dsp::StateVariableTPTFilterType::highpass);

        // The detector follows each ess within a millisecond and lets go over
        // 40 ms; the turning down itself is a touch smoother (1 ms / 60 ms),
        // so it doesn't flutter within one "s".
        attack = coefficient (sampleRate, 0.0005);
        release = coefficient (sampleRate, 0.04);
        gainAttack = coefficient (sampleRate, 0.001);
        gainRelease = coefficient (sampleRate, 0.06);
        highEnvelope = fullEnvelope = reductionNowDb = 0.0f;
        reductionDb = 0.0f;

        frequency.reset (sampleRate, 0.05);
        frequency.setCurrentAndTargetValue (6000.0f);
        split.setCutoffFrequency (3000.0f);
        split2.setCutoffFrequency (3000.0f);
        detector.setCutoffFrequency (6000.0f);
        listenAmount.reset (sampleRate, 0.02);
        listenAmount.setCurrentAndTargetValue (0.0f);
        bypass.prepare (sampleRate, maxBlockSize);
    }

    void DeEsser::process (juce::dsp::AudioBlock<float> monoBlock, const Settings& settings) noexcept
    {
        if (! settings.on && bypass.isSilent())
        {
            reductionDb = 0.0f;
            return;
        }

        bypass.process (monoBlock, settings.on, [&] (juce::dsp::AudioBlock<float> block)
        {
            frequency.setTargetValue (juce::jlimit (1000.0f, 16000.0f, settings.frequencyHz));
            listenAmount.setTargetValue (settings.listen ? 1.0f : 0.0f);

            auto* samples = block.getChannelPointer (0);
            auto deepest = 0.0f;
            for (size_t i = 0; i < block.getNumSamples(); ++i)
            {
                if (frequency.isSmoothing() || i == 0)
                {
                    const auto hz = frequency.getNextValue();
                    split.setCutoffFrequency (hz * 0.5f);   // an octave below
                    split2.setCutoffFrequency (hz * 0.5f);
                    detector.setCutoffFrequency (hz);
                }

                const auto input = samples[i];
                const auto low = split2.processSample (0, split.processSample (0, input));
                const auto high = input - low;   // the two parts add back to exactly the input

                // How far the highs stand out from the whole voice.
                const auto follow = [this] (float& envelope, float level)
                {
                    envelope = level + (level > envelope ? attack : release) * (envelope - level);
                };
                follow (highEnvelope, std::abs (detector.processSample (0, input)));
                follow (fullEnvelope, std::abs (input));

                auto targetDb = 0.0f;
                if (fullEnvelope > 1.0e-4f)   // above -80 dB: something to judge
                {
                    const auto standOutDb = juce::Decibels::gainToDecibels (highEnvelope / fullEnvelope, -100.0f);
                    targetDb = -juce::jlimit (0.0f, settings.amountDb, standOutDb - sibilanceThresholdDb);
                }
                reductionNowDb = targetDb + (targetDb < reductionNowDb ? gainAttack : gainRelease) * (reductionNowDb - targetDb);
                deepest = std::min (deepest, reductionNowDb);

                // What's taken away: the top part, by the reduction.
                const auto removed = high * (1.0f - juce::Decibels::decibelsToGain (reductionNowDb));
                const auto listen = listenAmount.getNextValue();
                samples[i] = (input - removed) + listen * (removed - (input - removed));
            }
            reductionDb = deepest;
        });
    }
}
