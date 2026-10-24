#pragma once

#include "EffectUtils.h"

namespace effects
{
    // Stomp 3, DOUBLER: automatic double tracking, after the graphic EQ and
    // before modulation. It sounds as if the part was sung three times: the
    // original in the middle and a "take" each side. Each take is the voice
    // a little late (19 and 27 ms, so they don't line up into a chorus), its
    // timing and pitch drifting slowly and independently, as no two takes
    // are ever sung exactly alike.
    //
    // RATE is how quickly the takes drift, DEPTH how loose they are, MIX how
    // loud they are against the original. (Until 0.3.0 this was MODULATION's
    // Doubler type; the sound is the same.)
    class Doubler
    {
    public:
        struct Settings
        {
            bool on = false;
            float rateHz = 0.8f;
            float depth = 0.4f;   // 0 to 1
            float mix = 0.7f;     // 0 to 1
        };

        Doubler();
        void prepare (double sampleRate, int maxBlockSize);
        void process (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept;

    private:
        double sampleRate = 48000.0;
        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> delayLine;
        float lfoPhase = 0.0f;   // 0 to 1
        juce::SmoothedValue<float> depth, mix;
        BypassFade bypass;
    };
}
