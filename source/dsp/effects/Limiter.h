#pragma once

#include "EffectUtils.h"

namespace effects
{
    // LIMITER under OUTPUT: the master limiter, last in the chain, so nothing
    // leaving the plugin goes over its CEILING. A safety net for loud notes
    // and sudden peaks, so the DAW never receives a clipped signal.
    //
    // No latency: most limiters delay the sound a few milliseconds to see
    // peaks coming, which a singer listening live would feel. This one turns
    // down at once, on the sample that would cross the ceiling, and lets go
    // over RELEASE. The cost: on a sudden spike the first instant is shaped
    // rather than faded (a click's worth); for a safety limiter, that's the
    // right trade. Both channels turn down together, so the stereo image
    // doesn't lean.
    //
    // At the top of its range the knob reads OFF and the limiter is switched
    // out, so OFF is exactly the input.
    class Limiter
    {
    public:
        static constexpr float minCeilingDb = -12.0f;
        static constexpr float offDb = 0.0f;   // the knob's top setting: OFF

        static bool isOff (float ceilingDb) noexcept { return ceilingDb >= offDb - 0.05f; }

        void prepare (double sampleRate)
        {
            releaseCoefficient = (float) std::exp (-1.0 / (releaseSeconds * sampleRate));
            envelope = 0.0f;
            reductionDb = 0.0f;
            ceiling.reset (sampleRate, 0.05);
            ceiling.setCurrentAndTargetValue (offGain);
        }

        // Straight to `ceilingDb`, without gliding: when playback starts, so
        // the very first peaks already meet the ceiling that's set.
        void reset (float ceilingDb) noexcept
        {
            ceiling.setCurrentAndTargetValue (isOff (ceilingDb) ? offGain : juce::Decibels::decibelsToGain (ceilingDb));
            envelope = 0.0f;
            reductionDb = 0.0f;
        }

        void process (juce::dsp::AudioBlock<float> block, float ceilingDb) noexcept
        {
            if (isOff (ceilingDb))
            {
                // Glide the ceiling far up out of the way rather than jump;
                // once there it's doing nothing, so it can step aside
                // without a click, however loud the sound is.
                ceiling.setTargetValue (offGain);
                if (! ceiling.isSmoothing())
                {
                    envelope = 0.0f;
                    reductionDb = 0.0f;
                    return;
                }
            }
            else
            {
                ceiling.setTargetValue (juce::Decibels::decibelsToGain (ceilingDb));
            }

            const auto numChannels = block.getNumChannels();
            auto deepest = 1.0f;
            for (size_t i = 0; i < block.getNumSamples(); ++i)
            {
                // The loudest channel's peak, held and released slowly: rises
                // at once, falls over RELEASE.
                auto peak = 0.0f;
                for (size_t ch = 0; ch < numChannels; ++ch)
                    peak = std::max (peak, std::abs (block.getChannelPointer (ch)[i]));
                envelope = std::max (peak, envelope * releaseCoefficient);

                const auto limit = ceiling.getNextValue();
                if (envelope <= limit)
                    continue;   // under the ceiling: untouched

                const auto gain = limit / envelope;
                deepest = std::min (deepest, gain);
                for (size_t ch = 0; ch < numChannels; ++ch)
                    block.getChannelPointer (ch)[i] *= gain;
            }
            reductionDb = juce::Decibels::gainToDecibels (deepest);
        }

        // How far the last block was turned down (dB, 0 or less), for the light.
        float getReductionDb() const noexcept { return reductionDb; }

    private:
        static constexpr double releaseSeconds = 0.1;
        static constexpr float offGain = 64.0f;   // +36 dB: a "ceiling" nothing reaches, while OFF
        float releaseCoefficient = 0.0f;
        float envelope = 0.0f;
        std::atomic<float> reductionDb { 0.0f };
        juce::SmoothedValue<float> ceiling;
    };
}
