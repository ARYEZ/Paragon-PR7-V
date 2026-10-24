#pragma once

#include "EffectUtils.h"
#include "PitchShifter.h"

namespace effects
{
    // Stomp 3, DOUBLER, after HARMONY and before modulation. Inspired by Waves
    // Doubler (the owner's reference, from its user guide): our own design.
    //
    // Up to four voices, each a copy of the voice that's truly detuned (its
    // pitch moved by a few cents with its timing kept, rather than a delay
    // swept to bend the pitch, as a chorus does), delayed, and placed in the
    // stereo picture. A copy that's a little late and a little out of tune
    // decorrelates from the original: it sounds like a second take sung
    // alongside, rather than a chorus. Each voice:
    //
    //   GAIN, PAN, DELAY (0-100 ms), FEEDBACK (some of the voice back into
    //   its own delay, through the FEEDBACK HIGH-PASS), OCTAVE (an octave
    //   down), DETUNE (cents), its own pitch wobble: DEPTH (cents either
    //   way; positive starts upwards) at RATE, and DRIFT.
    //
    // DRIFT (our own addition, not on Waves Doubler) is what makes a copy
    // sound like another take: a slow, random wander in its timing (up to
    // 6 ms either way) and pitch (up to 6 cents), different for each voice,
    // as a singer's second take is a little early here, a little late and
    // flat there. A steady copy, however detuned, the ear blends back into
    // the voice. Timing that moves bends the pitch a little while it moves,
    // as a singer's does; it glides slowly, so that stays within a few cents.
    //
    // On a mono output the voices sum at their full GAIN, whatever their PAN,
    // rather than at half, so they don't vanish on a mono track.
    //
    // Then a low and a high shelf on the voices (not the direct voice), the
    // DIRECT voice's own gain and pan, and an OUTPUT gain for everything.
    //
    // The detuning (the correlation-spliced PitchShifter) has a delay of its
    // own, about 12.5 ms, that drifts as it detunes: DELAY settings below it
    // come out at about that.
    class Doubler
    {
    public:
        static constexpr int numVoices = 4;
        static constexpr float minGainDb = -48.0f;
        static constexpr float maxDelayMs = 100.0f;

        struct Voice
        {
            bool on = false;
            float gainDb = -6.0f;
            float pan = 0.0f;          // -1 left to +1 right
            float delayMs = 10.0f;
            float feedback = 0.0f;     // 0 to 1
            bool octaveDown = false;
            float detuneCents = 0.0f;
            float depthCents = 0.0f;   // the wobble, either way of DETUNE
            float rateHz = 1.0f;
            float drift = 0.0f;        // 0 to 1
        };

        static constexpr float maxDriftMs = 6.0f, maxDriftCents = 6.0f;

        struct Settings
        {
            bool on = false;
            std::array<Voice, numVoices> voices;
            bool directOn = true;
            float directGainDb = 0.0f;
            float directPan = 0.0f;
            float feedbackHighPassHz = 16.0f;
            bool lowShelfOn = true, highShelfOn = true;
            float lowShelfDb = 0.0f, lowShelfHz = 101.0f;
            float highShelfDb = 0.0f, highShelfHz = 1306.0f;
            float outputDb = 0.0f;
            float wet = 1.0f;          // the expression pedal's MORPH: the voices faded in from the heel
        };

        void prepare (double sampleRate, int maxBlockSize);
        void process (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept;

        // MOD RESET: start every voice's wobble together, from the top.
        void resetModulation() noexcept { resetRequested = true; }

        // The detuning's own delay at rest, in ms. For tests.
        float getDetuneDelayMs() const noexcept { return nominalDelayMs; }

    private:
        // A slow random wander between -1 and 1: a new random goal every
        // half to one and a half seconds, glided to through two smoothing
        // stages (each `glideSeconds`), so it never jumps.
        struct Wander
        {
            void prepare (double sampleRate, int seed, float glideSeconds);
            float next() noexcept;

            juce::Random random;
            float goal = 0.0f, first = 0.0f, value = 0.0f, glide = 0.0f, rate = 48000.0f;
            int countdown = 0;
        };

        struct VoiceState
        {
            juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> delay { 1 << 14 };
            PitchShifter shifter;
            juce::dsp::StateVariableTPTFilter<float> feedbackFilter;
            juce::SmoothedValue<float> delaySamples, left, right;
            float lfoPhase = 0.0f, lastOut = 0.0f;
            Wander timing, pitch;
        };

        void updateShelves (const Settings&) noexcept;

        double sampleRate = 48000.0;
        float nominalDelayMs = 12.5f;
        std::array<VoiceState, numVoices> voices;
        std::array<juce::dsp::IIR::Filter<float>, 2> lowShelf, highShelf;   // left and right
        float shelfSettings[4] { 1.0e9f, 0.0f, 0.0f, 0.0f };               // what the shelves were last set to
        juce::SmoothedValue<float> directLeft, directRight, output;
        std::atomic<bool> resetRequested { false };
        BypassFade bypass;
    };
}
