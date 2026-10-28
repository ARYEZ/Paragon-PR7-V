#pragma once

#include "EffectUtils.h"

namespace effects
{
    // CYCLONE: the MOD block's rotary speaker, inspired by the Neo
    // Instruments Ventilator II (the owner sent its manual), which simulates
    // a model 122 rotary speaker cabinet. Our own design; where the manual
    // gives numbers (the rotors' speeds, how long they take to speed up and
    // slow down, the 800 Hz crossover, what each knob does) they are used
    // exactly, and everything it doesn't say is marked as our guess.
    //
    //   in -> DRIVE (the cabinet's valve amp, 4x oversampled) -> 800 Hz crossover
    //         treble -> HORN rotor: Doppler and level by its angle to each of
    //                   a stereo pair of mics, duller as it turns away
    //         bass   -> DRUM rotor: the same, to one mono mic below
    //         each rotor mixed with its own unspun signal (MIX/DIST, first
    //         half), the mics moved back (second half), BALANCE between them
    //         -> MODE's response (GIT 1, GIT 2, KEY) -> LEVEL
    //
    // The rotors: SLOW / FAST picks a speed, each set by its own knob; the
    // heavy drum turns a little slower than the horn and takes far longer to
    // get there (ACCELERATION). STOP brakes both to a standstill facing
    // forward. With the expression pedal on SPEED, the pedal sets the speed
    // instead, from SLOW SPEED at the heel to FAST SPEED at the toe.
    class Cyclone
    {
    public:
        // Saved by number in rigs and projects: only ever add to the end.
        enum class Mode { guitar1, guitar2, keys };
        static inline const juce::StringArray modeNames { "GIT 1", "GIT 2", "KEY" };

        struct Settings
        {
            bool fast = false, stop = false;
            float fastHz = 6.8f, slowHz = 0.8f;   // the horn's speed; the drum turns a little slower
            float acceleration = 6.0f;           // 0 to 10 (6 is the manual's 1 o'clock)
            float balance = 5.0f;                // 0 to 10, 5 even; up cuts the drum, down the horn
            float drive = 0.0f;                  // 0 to 10
            float mixDistLow = 7.0f, mixDistHigh = 7.0f;   // 0 to 10: up to 5 the mix, then the mics' distance
            Mode mode = Mode::guitar2;
            float level = 5.0f;                  // 0 to 10, 5 about the bypassed level
            float pedal = -1.0f;                 // the expression pedal on SPEED, 0 to 1 (below 0: not)
            float morph = 1.0f;                  // MORPH FX: 0 dry to 1 the full effect
        };

        // The manual's speed ranges (each knob sets both rotors). Its
        // defaults are the speeds of an average 122: fast horn 6.8 Hz and
        // drum 6.5 Hz, slow horn 0.8 Hz and drum 0.76 Hz.
        static constexpr float fastMinHz = 2.72f, fastMaxHz = 8.16f, fastDefaultHz = 6.8f;
        static constexpr float slowMinHz = 0.13f, slowMaxHz = 2.62f, slowDefaultHz = 0.8f;
        // The drum's speed for the horn's: the manual's pairs are all 0.95
        // to 0.96 times the horn's (we use one ratio for them all).
        static constexpr float drumRatio = 0.953f;

        // How long each rotor takes to change speed (the manual's ACCELERATION
        // table), in seconds, for the knob (0 to 10).
        struct Ramps { float drumUp, hornUp, drumDown, hornDown; };
        static Ramps rampSeconds (float acceleration) noexcept;

        // The cabinet's Doppler: each rotor's sound source swings this far
        // towards and away from the mics (our guesses from a 122's size: the
        // horn's mouths turn on about an 18 cm radius).
        static constexpr float hornRadiusMetres = 0.18f, drumRadiusMetres = 0.15f;

        // The original rotary type (PR7-G before 0.55.0, PR7-V before 0.40.0) had one RATE for the
        // horn, DEPTH and MIX. A rig or project saved with it gets Cyclone
        // set as close to it as it goes: RATE picks SLOW or FAST and that
        // speed; MIX, then DEPTH, set the MIX / DIST knobs; GIT 1 (the
        // original had no speaker response). Close, not identical.
        struct FromOldRotary { bool fast; float fastHz, slowHz, mixDist; Mode mode; };
        static FromOldRotary fromOldRotary (float rateHz, float depth, float mix) noexcept;

        Cyclone();
        void prepare (double sampleRate, int maxBlockSize);
        void reset() noexcept;
        // `block` carries the same signal on both sides (the chain is mono
        // up to here); out comes the stereo pair of mics.
        void process (juce::dsp::AudioBlock<float> block, const Settings&) noexcept;

        // Where the rotors are, for the faceplate and tests (the audio thread
        // writes them; reading them anywhere is safe).
        float getHornHz() const noexcept { return hornHzShown.load(); }
        float getDrumHz() const noexcept { return drumHzShown.load(); }
        float getHornTurn() const noexcept { return hornTurnShown.load(); }   // 0 to 1, 0 facing forward
        float getDrumTurn() const noexcept { return drumTurnShown.load(); }

    private:
        struct Biquad
        {
            float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
            float z1 = 0.0f, z2 = 0.0f;
            float process (float x) noexcept
            {
                const auto y = b0 * x + z1;
                z1 = b1 * x - a1 * y + z2;
                z2 = b2 * x - a2 * y;
                return y;
            }
            void clear() noexcept { z1 = z2 = 0.0f; }
        };

        struct Rotor
        {
            float hz = 0.0f, turn = 0.0f;
            bool parked = false;
            void advance (float targetHz, float upSeconds, float downSeconds, bool stopping, double sampleRate) noexcept;
        };

        void updateModeFilters (Mode) noexcept;
        float driveSample (float x, float drive) noexcept;

        double sampleRate = 48000.0;
        Rotor horn, drum;
        juce::dsp::LinkwitzRileyFilter<float> crossover;
        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> hornDelay, drumDelay;
        float hornTone[maxChannels] {};
        float drumTone = 0.0f;

        juce::dsp::Oversampling<float> oversampling;
        juce::AudioBuffer<float> mono;
        std::array<float, 11> driveMakeUp {};   // the level matching, per DRIVE step (measured in prepare)

        // MODE: the 122's speaker response on each rotor (KEY), and GIT 2's lift.
        Biquad hornSpeaker[2], drumSpeaker[2], lift[maxChannels][2];
        Mode currentMode = Mode::guitar2;
        bool speakerOn = false, liftOn = false;

        juce::SmoothedValue<float> hornMix, drumMix, hornNear, drumNear, hornGain, drumGain, outputGain, wet;
        juce::SmoothedValue<float> driveSmoothed;

        std::atomic<float> hornHzShown { 0.0f }, drumHzShown { 0.0f }, hornTurnShown { 0.0f }, drumTurnShown { 0.0f };
    };
}
