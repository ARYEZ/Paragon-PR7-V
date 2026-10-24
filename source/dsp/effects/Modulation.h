#pragma once

#include "EffectUtils.h"

namespace effects
{
    // Stomp 5: the modulation block, after the DOUBLER. Ten types:
    //
    //   chorus, flanger, phaser, tremolo         the originals (unchanged)
    //   dimension      Roland Dimension D / Boss CE-1 style dual chorus
    //   uniVibe        Shin-ei Uni-Vibe: four uneven phase stages, lamp LFO
    //   rotary         Leslie speaker: horn and drum rotors, Doppler, stereo mics
    //   vibrato        pure pitch vibrato
    //   harmonicTremolo  Fender brownface: lows and highs pulse in turn
    //   autoPan        sweeps between the speakers at constant loudness
    //
    // (Until 0.3.0 there was an eleventh, Doubler, now the DOUBLER stomp: see
    // Doubler.h. Settings saved with it are moved there when they load.)
    //
    // Chorus and flanger are built here rather than using juce::dsp::Chorus,
    // because JUCE's version moves both channels together. Running the right
    // channel's sweep a quarter-cycle behind the left is what makes a chorus
    // sound wide in stereo.
    class Modulation
    {
    public:
        // Saved by number in rigs and projects: only ever add to the end.
        // Number 10 was Doubler until 0.3.0 (see above).
        enum class Type { chorus, flanger, phaser, tremolo, dimension, uniVibe, rotary, vibrato, harmonicTremolo, autoPan };
        static inline const juce::StringArray typeNames { "Chorus", "Flanger", "Phaser", "Tremolo", "Dimension Chorus", "Uni-Vibe",
                                                          "Rotary", "Vibrato", "Harmonic Tremolo", "Auto-Pan" };
        static constexpr int oldDoublerType = 10;

        // Types for which MIX means something (the others are fully "wet" by nature).
        static bool usesMix (Type type) noexcept
        {
            return type == Type::chorus || type == Type::flanger || type == Type::phaser || type == Type::dimension
                || type == Type::uniVibe || type == Type::rotary;
        }

        struct Settings
        {
            bool on = false;
            Type type = Type::chorus;
            float rateHz = 0.8f;
            float depth = 0.5f;   // 0 to 1
            float mix = 0.5f;     // 0 to 1 (ignored where usesMix() is false)
        };

        Modulation();
        void prepare (double sampleRate, int maxBlockSize);
        void process (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept;

    private:
        void processDelayBased (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept;
        void processTremolo (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept;
        void processDimension (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept;
        void processUniVibe (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept;
        void processRotary (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept;
        void processVibrato (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept;
        void processHarmonicTremolo (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept;
        void processAutoPan (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept;
        float advanceLfo (float rateHz) noexcept;

        double sampleRate = 48000.0;
        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> delayLine;
        juce::dsp::Phaser<float> phaser;
        float lfoPhase = 0.0f;   // 0 to 1
        float feedbackSample[maxChannels] {};
        juce::SmoothedValue<float> depth, mix;
        BypassFade bypass;

        // Dimension: the darker tone of the bucket-brigade chips the originals used.
        float dimensionTone[maxChannels] {};

        // Uni-Vibe: four first-order all-pass stages per channel, and the lamp's glow.
        struct AllPassState { float x1 = 0.0f, y1 = 0.0f; };
        AllPassState vibeStages[maxChannels][4] {};
        float lampGlow = 0.0f;

        // Rotary: a crossover into horn (treble) and drum (bass), each with
        // its own delay line for the Doppler shift and its own spinning speed.
        juce::dsp::LinkwitzRileyFilter<float> rotaryCrossover;
        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> hornDelay, drumDelay;
        float hornPhase = 0.0f, drumPhase = 0.0f, hornHz = 0.8f, drumHz = 0.7f, hornTone = 0.0f;

        // Harmonic tremolo: a crossover per channel.
        juce::dsp::LinkwitzRileyFilter<float> harmonicCrossover;
    };
}
