#pragma once

#include "EffectUtils.h"

namespace effects
{
    // LOW CUT on the INPUT strip: a high-pass filter first in the chain, as
    // on a mic preamp or a mixing desk's channel. It clears out what a
    // microphone picks up below the voice (rumble through the stand, traffic,
    // air conditioning) and softens the low thump of plosives ("p" and "b"),
    // before the compressor can pump on them.
    //
    // 12 dB per octave (a Butterworth curve): the frequency on the knob is
    // 3 dB down, an octave below it 12 dB down, two octaves below 24 dB.
    // Gentle enough to sound natural on a voice. At the bottom of its range
    // the knob reads OFF and the filter is switched out, so OFF is exactly
    // the input.
    class LowCut
    {
    public:
        static constexpr float offHz = 20.0f;      // the knob's lowest setting: OFF
        static constexpr float maxHz = 400.0f;

        static bool isOff (float hz) noexcept { return hz <= offHz + 0.5f; }

        void prepare (double sampleRate, int maxBlockSize)
        {
            filter.prepare ({ sampleRate, (juce::uint32) maxBlockSize, 1 });
            filter.setType (juce::dsp::StateVariableTPTFilterType::highpass);
            filter.setResonance (1.0f / juce::MathConstants<float>::sqrt2);   // Butterworth: no bump at the corner
            cutoffHz.reset (sampleRate, 0.05);
            cutoffHz.setCurrentAndTargetValue (offHz);
            filter.setCutoffFrequency (offHz);
            fade.prepare (sampleRate, maxBlockSize);
            fade.setImmediately (false);
        }

        // Straight to `hz`, without gliding or fading: when playback starts,
        // so the first notes are already filtered.
        void reset (float hz) noexcept
        {
            const auto on = ! isOff (hz);
            const auto settled = on ? juce::jlimit (offHz, maxHz, hz) : offHz;
            cutoffHz.setCurrentAndTargetValue (settled);
            filter.setCutoffFrequency (settled);
            filter.reset();
            fade.setImmediately (on);
        }

        void process (juce::dsp::AudioBlock<float> monoBlock, float hz) noexcept
        {
            const auto on = ! isOff (hz);
            // Turned down to OFF, the filter glides to its lowest setting
            // while it fades out, rather than jumping.
            cutoffHz.setTargetValue (juce::jlimit (offHz, maxHz, hz));
            if (! on && fade.isSilent())
            {
                cutoffHz.setCurrentAndTargetValue (offHz);
                filter.setCutoffFrequency (offHz);
                filter.reset();   // so turning it back on starts clean
                return;
            }

            fade.process (monoBlock, on, [this] (juce::dsp::AudioBlock<float> block)
            {
                auto* samples = block.getChannelPointer (0);
                const auto numSamples = (int) block.getNumSamples();
                // While the knob moves, retune every 16 samples: smooth
                // enough not to zip, without recalculating every sample.
                for (int start = 0; start < numSamples; start += retuneEvery)
                {
                    const auto count = juce::jmin (retuneEvery, numSamples - start);
                    if (cutoffHz.isSmoothing())
                        filter.setCutoffFrequency (cutoffHz.skip (count));
                    for (int i = start; i < start + count; ++i)
                        samples[i] = filter.processSample (0, samples[i]);
                }
            });
        }

    private:
        static constexpr int retuneEvery = 16;
        juce::dsp::StateVariableTPTFilter<float> filter;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> cutoffHz;
        BypassFade fade;
    };
}
