#pragma once

#include "Aeon.h"
#include "Cyclone.h"
#include "EffectUtils.h"

namespace effects
{
    // Stomp 5: the modulation block, after the DOUBLER. Ten types:
    //
    //   chorus, phaser, tremolo                  the originals (unchanged)
    //   flanger        AEON, modelled on the Boss BF-2 (see Aeon.h; PR7-V 0.18.0,
    //                  replacing the original generic flanger)
    //   dimension      Roland Dimension D / Boss CE-1 style dual chorus
    //   uniVibe        Shin-ei Uni-Vibe: four uneven phase stages, lamp LFO
    //   rotary         CYCLONE, a rotary speaker inspired by the Neo Ventilator II
    //                  (see Cyclone.h; PR7-V 0.40.0, replacing the original generic one)
    //   vibrato        pure pitch vibrato
    //   harmonicTremolo  Fender brownface: lows and highs pulse in turn
    //   autoPan        sweeps between the speakers at constant loudness
    //
    // (Until 0.3.0 there was an eleventh, Doubler, now the DOUBLER stomp: see
    // Doubler.h. Settings saved with it are moved there when they load.)
    //
    // The chorus is built here rather than using juce::dsp::Chorus,
    // because JUCE's version moves both channels together. Running the right
    // channel's sweep a quarter-cycle behind the left is what makes a chorus
    // sound wide in stereo.
    class Modulation
    {
    public:
        // Saved by number in rigs and projects: only ever add to the end.
        // Number 10 was Doubler until 0.3.0 (see above).
        enum class Type { chorus, flanger, phaser, tremolo, dimension, uniVibe, rotary, vibrato, harmonicTremolo, autoPan };
        static inline const juce::StringArray typeNames { "Chorus", "AEON", "Phaser", "Tremolo", "Dimension Chorus", "Uni-Vibe",
                                                          "Cyclone", "Vibrato", "Harmonic Tremolo", "Auto-Pan" };
        static constexpr int oldDoublerType = 10;

        // Types for which MIX means something (the others are fully "wet" by nature).
        static bool usesMix (Type type) noexcept
        {
            // (AEON has none: the pedal mixes dry and wet 50/50. Cyclone has
            // none either: its MIX / DIST knobs mix each rotor in.)
            return type == Type::chorus || type == Type::phaser || type == Type::dimension
                || type == Type::uniVibe;
        }

        struct Settings
        {
            bool on = false;
            Type type = Type::chorus;
            float rateHz = 0.8f;
            float depth = 0.5f;   // 0 to 1
            float mix = 0.5f;     // 0 to 1 (ignored where usesMix() is false)
            float manual = 5.0f, resonance = 5.0f;   // AEON's MANUAL and RES, 0 to 10
            Cyclone::Settings cyclone;  // Cyclone's own controls (RATE, DEPTH and MIX don't apply to it)
        };

        Modulation();
        void prepare (double sampleRate, int maxBlockSize);
        // The rotary speaker, for the faceplate (where its rotors are).
        const Cyclone& getCyclone() const noexcept { return cyclone; }
        void process (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept;

    private:
        void processDelayBased (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept;
        void processTremolo (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept;
        void processDimension (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept;
        void processUniVibe (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept;
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
        Type lastType = Type::chorus;

        // AEON, the flanger.
        Aeon aeon;

        // Dimension: the darker tone of the bucket-brigade chips the originals used.
        float dimensionTone[maxChannels] {};

        // Uni-Vibe: four first-order all-pass stages per channel, and the lamp's glow.
        struct AllPassState { float x1 = 0.0f, y1 = 0.0f; };
        AllPassState vibeStages[maxChannels][4] {};
        float lampGlow = 0.0f;

        // CYCLONE, the rotary speaker.
        Cyclone cyclone;

        // Harmonic tremolo: a crossover per channel.
        juce::dsp::LinkwitzRileyFilter<float> harmonicCrossover;
    };
}
