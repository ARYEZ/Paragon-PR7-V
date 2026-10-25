#pragma once

#include "PitchShifter.h"

namespace effects
{
    // The delay half of footswitch 4, in eight types:
    //
    //   digital    clean, exact repeats (the original Paragon delay); TONE
    //              darkens each repeat a little more than the last
    //   analog     a bucket-brigade pedal (Boss DM-2, MXR Carbon Copy): warm,
    //              dark repeats that soften and blur, with a slight wobble,
    //              and that sing into oscillation with FEEDBACK up high
    //   tape       an Echoplex or Space Echo: wow and flutter, tape
    //              saturation, a little hiss, repeats that warm as they fade
    //   mod        clean repeats with chorus on them
    //   reverse    each repeat played backwards
    //   multiTap   repeats at rhythmic spacings inside each beat, accenting it
    //   shimmer    each repeat pitched up an octave, rising into a glowing wash
    //   ducking    Digital with DUCK preset high: repeats tuck under your
    //              playing and swell back in the gaps (DUCK works on every
    //              type; this keeps rigs that used the type sounding the same)
    //
    // Echo time is either free (milliseconds) or locked to the DAW's tempo.
    // Ping-pong bounces repeats between the left and right speakers.
    // Switching off lets the echoes ring out ("trails") instead of cutting
    // them dead.
    //
    // Since 0.10.0, on every type, controls inspired by Waves H-Delay (the
    // owner's reference, from its user guide; our own design):
    //
    //   DUAL       the right side its own time (or note value)
    //   START      which side ping-pong's first echo is on
    //   PHASE      flip the left or right echoes' polarity
    //   FEEDBACK   up to 200%: past 100% the repeats build instead of
    //              fading, held from running away by a soft limit
    //   LOW CUT and HIGH CUT  filters on what goes into the delay, so each
    //              repeat is filtered again (Off at their ends)
    //   MOD        DEPTH and RATE: a triangle wave sweeping the echo time,
    //              for chorus and flanging on short times
    //   ANALOG     Off or four characters of our own (the guide doesn't say
    //              what its four do): 1 warm, 2 warmer and rounder,
    //              3 as 2 with tape hiss, 4 gritty, dark and noisy
    //   LO-FI      an early digital delay's narrow band and 12-bit grain
    class Delay
    {
    public:
        // Note lengths, in beats (quarter notes), for tempo sync.
        // Index 0 is "Off": use the time in milliseconds instead.
        static constexpr float syncBeats[] = { 0.0f, 4.0f, 2.0f, 1.0f, 1.5f, 2.0f / 3.0f, 0.5f, 0.75f, 1.0f / 3.0f, 0.25f };
        static inline const juce::StringArray syncNames { "Off", "1/1", "1/2", "1/4", "1/4 dotted", "1/4 triplet",
                                                          "1/8", "1/8 dotted", "1/8 triplet", "1/16" };

        static constexpr float maxDelaySeconds = 3.5f;
        static constexpr float maxFeedback = 2.0f;
        static constexpr float maxModMs = 4.0f;          // MOD DEPTH at 100%: the time swings this far either way
        static constexpr float cutOffLowHz = 20.0f;      // LOW CUT at (or below) this: off
        static constexpr float cutOffHighHz = 20000.0f;  // HIGH CUT at (or above) this: off

        // Saved by number: only ever add to the end.
        enum class Analog { off, warm, warmer, hiss, grit };
        static inline const juce::StringArray analogNames { "Off", "1 Warm", "2 Round", "3 Hiss", "4 Grit" };
        static inline const juce::StringArray pingStartNames { "Left", "Right" };

        // Saved by number in rigs and projects: only ever add to the end.
        enum class Type { digital, analog, tape, mod, reverse, multiTap, shimmer, ducking };
        static inline const juce::StringArray typeNames { "Digital", "Analog", "Tape", "Mod", "Reverse", "Multi-Tap", "Shimmer", "Ducking" };

        struct Settings
        {
            bool on = false;
            float timeMs = 375.0f;
            int syncIndex = 3;           // 1/4
            float feedback = 0.35f;      // 0 to 0.95
            float tone = 7.0f;           // 0 to 10
            float mix = 0.25f;           // 0 to 1
            bool pingPong = false;
            Type type = Type::digital;
            float duck = 0.0f;           // 0 to 1: DUCK
            bool dual = false;           // DUAL: the right side's own time
            float timeRightMs = 280.0f;
            int syncRightIndex = 7;      // 1/8 dotted
            bool startRight = false;     // ping-pong's first echo on the right
            bool phaseLeft = false, phaseRight = false;
            float lowCutHz = cutOffLowHz, highCutHz = cutOffHighHz;
            float modDepth = 0.0f;       // 0 to 1
            float modRateHz = 0.5f;
            Analog analog = Analog::off;
            bool loFi = false;
        };

        void prepare (double sampleRate, int maxBlockSize);
        void process (juce::dsp::AudioBlock<float> block, const Settings& settings, double bpm) noexcept;

        // The echo time these settings give at this tempo, in seconds (the
        // right side's, with DUAL on, from delaySecondsRight).
        static float delaySeconds (const Settings& settings, double bpm) noexcept;
        static float delaySecondsRight (const Settings& settings, double bpm) noexcept;

    private:
        void resetState() noexcept;

        // A circular buffer read from behind its write position, for the
        // reverse and shimmer types.
        struct Ring
        {
            std::vector<float> data;
            int mask = 0, write = 0;
            void prepare (int minimumLength);
            void clear() { std::fill (data.begin(), data.end(), 0.0f); write = 0; }
            void push (float x) noexcept { data[(size_t) write] = x; write = (write + 1) & mask; }
            float at (float samplesAgo) const noexcept;
        };

        double sampleRate = 48000.0;
        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> delayLine { 4 };
        juce::SmoothedValue<float> send, feedback, mix;
        std::array<juce::SmoothedValue<float>, maxChannels> delaySamples;   // each side's (the same, unless DUAL)
        float feedbackFilter[maxChannels] {}, feedbackFilter2[maxChannels] {}, lowCutState[maxChannels] {};
        bool running = false;
        Type lastType = Type::digital;
        TailWatch tail;
        juce::AudioBuffer<float> wet;
        juce::Random random;

        // Analog, tape and mod: the drifting read position.
        float wowPhase = 0.0f, flutterPhase = 0.0f, drift = 0.0f, driftTarget = 0.0f;
        int driftCountdown = 0;

        // Reverse: what goes into the delay, recorded, and two heads
        // playing it backwards in delay-length chunks.
        std::array<Ring, maxChannels> reverseRings;
        struct Head { int start = 0, count = 0, length = 0; };   // each chunk keeps the length it started with
        std::array<Head, 2> reverseHeads {};

        // Shimmer: an octave-up pitch shifter in the feedback loop, per side.
        std::array<PitchShifter, maxChannels> shimmerShifters;

        // DUCK (and the Ducking type, which is Digital with DUCK preset).
        Ducker ducker;

        // H-Delay's controls: the filters, ANALOG and LO-FI on what goes in,
        // and MOD's sweep.
        std::array<juce::dsp::StateVariableTPTFilter<float>, maxChannels> lowCutFilters, highCutFilters;
        float analogTone[maxChannels] {}, loFiState[maxChannels][2] {};
        float modPhase = 0.0f;
    };
}
