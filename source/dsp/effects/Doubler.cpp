#include "Doubler.h"

namespace effects
{
    namespace
    {
        constexpr float twoPi = juce::MathConstants<float>::twoPi;
        constexpr float takeMs[2] = { 19.0f, 27.0f };
        constexpr float maxDelayMs = 40.0f;   // the later take plus its widest wander, with room to spare
    }

    Doubler::Doubler() : delayLine (8192) {}

    void Doubler::prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        delayLine.setMaximumDelayInSamples ((int) std::ceil (maxDelayMs * 0.001 * sampleRate) + 4);
        delayLine.prepare ({ sampleRate, (juce::uint32) maxBlockSize, 1 });
        depth.reset (sampleRate, 0.05);
        mix.reset (sampleRate, 0.05);
        lfoPhase = 0.0f;
        bypass.prepare (sampleRate, maxBlockSize);
    }

    void Doubler::process (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        bypass.process (block, settings.on, [&] (juce::dsp::AudioBlock<float> wet)
        {
            depth.setTargetValue (settings.depth);
            mix.setTargetValue (settings.mix);

            const auto msToSamples = (float) sampleRate * 0.001f;
            const auto numChannels = (int) wet.getNumChannels();

            for (size_t i = 0; i < wet.getNumSamples(); ++i)
            {
                // A slow wander for each take: two sines at unrelated speeds, so it never quite repeats.
                lfoPhase += settings.rateHz * 0.25f / (float) sampleRate;
                lfoPhase -= std::floor (lfoPhase);
                const auto depthNow = depth.getNextValue();
                const auto mixNow = mix.getNextValue();
                const auto wanderMs = 0.15f + 1.2f * depthNow;

                const auto dry = wet.getChannelPointer (0)[i];
                delayLine.pushSample (0, dry);

                float takes[2];
                for (int take = 0; take < 2; ++take)
                {
                    const auto offset = 0.37f * (float) take;
                    const auto wander = 0.6f * std::sin (twoPi * (lfoPhase + offset))
                                      + 0.4f * std::sin (twoPi * (2.71f * lfoPhase + 0.13f + offset));
                    takes[take] = delayLine.popSample (0, (takeMs[take] + wanderMs * wander) * msToSamples, take == 1);
                }

                // The original stays in the middle at full level; the takes come up beside it.
                if (numChannels == 1)
                {
                    wet.getChannelPointer (0)[i] = dry + 0.5f * mixNow * (takes[0] + takes[1]);
                }
                else
                {
                    const auto original = wet.getChannelPointer (1)[i];   // same as the left: the chain is mono up to here
                    wet.getChannelPointer (0)[i] = dry + mixNow * takes[0];
                    wet.getChannelPointer (1)[i] = original + mixNow * takes[1];
                }
            }
        });
    }
}
