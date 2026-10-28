#pragma once

#include "EffectUtils.h"

namespace effects
{
    // HARMONY's modulators (stage 9), inspired by the Modulators chapter of
    // Waves Harmony's user guide (the owner's reference); our own design
    // where the guide doesn't say how something works.
    //
    //   M1-M4   each an LFO (one of our own shapes) or a step SEQUENCER (2 to
    //           16 steps of -24 to +24); RATE in Hz or synced to the tempo,
    //           PHASE, WARP, SMOOTH, LEVEL, TRIGGER (Free, Sync, Retrigger,
    //           Legato) and PLAY (Loop, One Shot, Seesaw, Hold). Output -1 to 1.
    //   AM      how loud the voice is (an envelope follower): ATTACK, RELEASE,
    //           LEVEL. Output 0 to LEVEL.
    //   PT      the pitch sung: SMOOTH, OFFSET, LEVEL. Output -1 to 1 times
    //           LEVEL; 0 at middle C (plus OFFSET), 1 two octaves above.
    //
    // A control can take up to four of them (its modulation slots), each at
    // its own DEPTH (-100% to 100%). At 100% a source at full swing moves the
    // control through its whole range; a pitch control (a voice's PITCH or
    // NOTE, GLOBAL PITCH) two octaves either way, as in the guide, so a
    // sequencer step of +7 at 100% is exactly a fifth up.
    //
    // Stage 10 adds A1-A2 (envelopes), SP (the Spreader), the MIDI sources
    // and drawn shapes; their numbers are kept for them already.
    class HarmonyModulation
    {
    public:
        static constexpr int numMods = 4, maxSteps = 16, slotsPerTarget = 4;
        static constexpr float pitchSpan = 24.0f;   // semitones either way at 100%

        // Saved by number: only ever add to the end.
        enum Source { none, m1, m2, m3, m4, a1, a2, am, pt, sp, velocity, keyboard, pitchWheel, modWheel, aftertouch, numSources };
        static inline const juce::StringArray sourceNames { "None", "M1", "M2", "M3", "M4", "A1", "A2", "AM", "PT", "SP",
                                                            "Velocity", "Keyboard", "Pitch Wheel", "Mod Wheel", "Aftertouch" };
        // The ones built so far (stage 9).
        static constexpr bool isBuilt (int source) noexcept { return (source >= m1 && source <= m4) || source == am || source == pt; }

        // Saved by number: only ever add to the end.
        static inline const juce::StringArray typeNames { "LFO", "SEQ" };
        static inline const juce::StringArray shapeNames { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Pulse",
                                                           "Stairs", "Random Steps", "Smooth Random", "Bump" };
        static inline const juce::StringArray triggerNames { "Free", "Sync", "Retrigger", "Legato" };   // Poly comes with the Spreader
        static inline const juce::StringArray playNames { "Loop", "One Shot", "Seesaw", "Hold" };
        enum Trigger { free, sync, retrigger, legato };
        enum Play { loop, oneShot, seesaw, hold };
        // RATE SYNC: Off (Hz), then 8 bars down to 1/64, in beats (a quarter note is one).
        static inline const juce::StringArray syncNames { "Off", "8 Bars", "4 Bars", "2 Bars", "1 Bar", "1/2", "1/2 Dotted",
                                                          "1/4", "1/4 Dotted", "1/4 Triplet", "1/8", "1/8 Dotted", "1/8 Triplet",
                                                          "1/16", "1/16 Triplet", "1/32", "1/64" };
        static constexpr float syncBeats[] = { 0.0f, 32.0f, 16.0f, 8.0f, 4.0f, 2.0f, 3.0f, 1.0f, 1.5f, 2.0f / 3.0f,
                                               0.5f, 0.75f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f, 0.0625f };

        static constexpr float minRateHz = 0.06f, maxRateHz = 30.0f;
        static constexpr float minWarp = 0.1f, maxWarp = 100.0f;

        struct Mod
        {
            int type = 0;                         // 0 LFO, 1 SEQ
            int shape = 0;
            float rateHz = 1.0f;                  // LFO: cycles a second; SEQ: steps a second
            int sync = 0;                         // syncNames (0: use rateHz)
            float phase = 0.0f;                   // where it starts, 0 to 1 of a cycle
            float warp = 1.0f;                    // below 1: slow then fast; above 1: fast then slow
            float smoothMs = 1.0f;
            float level = 1.0f;
            int trigger = free, play = loop;
            int steps = 8;
            std::array<float, maxSteps> stepValues {};   // -24 to 24
        };
        struct Am { float attackMs = 10.0f, releaseMs = 150.0f, level = 1.0f; };
        struct Pt { float smoothMs = 50.0f, offset = 0.0f, level = 1.0f; };
        struct Settings
        {
            std::array<Mod, numMods> mods;
            Am am;
            Pt pt;
        };
        // What's happening around it, for this stretch of audio.
        struct Context
        {
            double bpm = 120.0;
            double songBeats = -1.0;   // where it starts in the song (-1: not playing)
            bool newNote = false;      // a note began (generated, or a MIDI key)
            bool heldBefore = false;   // a note was already sounding when it did (Legato)
            float sungNote = -1.0f;    // MIDI, or -1 between notes
        };

        void prepare (double sampleRate);
        void reset();

        // Moves every source on by `numSamples`; `voice` is the singer's
        // clean voice over the same stretch (for AM).
        void advance (const Settings&, const Context&, const float* voice, int numSamples) noexcept;

        float getValue (int source) const noexcept { return juce::isPositiveAndBelow (source, (int) numSources) ? values[(size_t) source] : 0.0f; }
        // For the panel (any thread): each source's value, and M1-M4's place in their cycle (0 to 1).
        float getShownValue (int source) const noexcept { return juce::isPositiveAndBelow (source, (int) numSources) ? shownValues[(size_t) source].load() : 0.0f; }
        float getShownPosition (int mod) const noexcept { return shownPositions[(size_t) juce::jlimit (0, numMods - 1, mod)].load(); }

        // An LFO shape's value (-1 to 1) at `position` (0 to 1) in cycle number `cycle` (the random ones change each cycle).
        static float shapeValue (int shape, float position, std::uint32_t cycle, int seed) noexcept;
        // A modulator's raw value (before SMOOTH and LEVEL) at `position` (0 to 1, after PHASE).
        static float modValue (const Mod&, float position, std::uint32_t cycle, int seed) noexcept;
        // How long one cycle (LFO) or one step (SEQ) lasts, in seconds, at `bpm`.
        static double unitSeconds (const Mod&, double bpm) noexcept;

    private:
        struct ModState
        {
            double position = 0.0;   // through the cycle(s) since the start, before PHASE; can pass 1 (counts cycles)
            float smoothed = 0.0f;
            bool started = false;
        };

        double sampleRate = 48000.0;
        std::array<ModState, numMods> modStates {};
        float amEnvelope = 0.0f, ptSmoothed = 0.0f;
        bool ptStarted = false;
        std::array<float, numSources> values {};
        std::array<std::atomic<float>, numSources> shownValues {};
        std::array<std::atomic<float>, numMods> shownPositions {};
    };
}
