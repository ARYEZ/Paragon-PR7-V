#pragma once

#include "EffectUtils.h"

namespace effects
{
    // The reverb half of footswitch 4, in eight types:
    //
    //   classic    JUCE's Freeverb-style room: the original Paragon reverb,
    //              kept so rigs and projects made with it sound the same
    //   room       a small room: strong early reflections, a short tail
    //   hall       a concert hall: a gap, then a smooth, long, warm tail
    //   cathedral  a huge stone space: a long gap, a very long, darker tail
    //   plate      a studio plate: no early reflections, dense and bright
    //              from the first moment
    //   spring     an amp's spring tank: dispersive, drippy, with the boing
    //   digital    an 80s rack reverb: clean, with a lush chorused tail
    //   reverse    the reverb played backwards, swelling into each note
    //   chamber    an echo chamber: a small, hard, irregular room that turns
    //              dense at once, with no slap off its walls (since 0.11.0)
    //
    // Room, hall, cathedral, plate, digital and reverse share one engine: a
    // feedback delay network (eight delay lines feeding back into each
    // other through a mixing matrix, the design behind most good studio
    // reverbs), which builds a dense, smooth tail without the metallic
    // ringing of simpler designs. Each type sets its own room size,
    // early reflections, diffusion, damping and tail modulation.
    //
    // SIZE sets how long the tail lasts (and, for reverse, the length of
    // each reversed swell); PRE-DELAY the gap before the reverb starts;
    // TONE how bright it is; DUCK how far it tucks under your playing.
    // Switching off lets the tail ring out.
    //
    // Since 0.11.0, for the network's types (all but Classic and Spring),
    // controls inspired by the Lexicon LXP reverbs (the owner's reference,
    // from their manual; our own design). Each starts where it changes
    // nothing:
    //
    //   DECAY      the tail's length, as a multiple of what TYPE and SIZE give
    //   BASS RT    the lows' tail as a multiple of the rest, below CROSSOVER
    //   ROLLOFF    a high cut on the way out
    //   DIFFUSION  how fast the first echoes smear into a dense wash
    //   EARLY and REVERB  the early reflections' and the tail's levels
    //   SPREAD and SHAPE  the sound fed into the tail over a window (SPREAD
    //              long) rather than at once: early in it (SHAPE low), evenly
    //              (middle) or late (high). With a short DECAY: gated and
    //              inverse reverbs.
    //   SPIN and WANDER   how fast and how far the tail's lines drift
    class Reverb
    {
    public:
        // Saved by number in rigs and projects: only ever add to the end.
        enum class Type { classic, room, hall, cathedral, plate, spring, digital, reverse, chamber };
        static inline const juce::StringArray typeNames { "Classic", "Room", "Hall", "Cathedral", "Plate", "Spring", "Digital", "Reverse", "Chamber" };
        static constexpr float maxPreDelayMs = 250.0f;
        static constexpr float maxSpreadMs = 300.0f;
        static constexpr float rolloffOffHz = 20000.0f;   // ROLLOFF at (or above) this: off
        static constexpr float levelOffDb = -48.0f;       // EARLY and REVERB at (or below) this: silent

        // Whether a type has the controls inspired by LXP (the network's types).
        static bool hasNetworkControls (Type type) noexcept { return type != Type::classic && type != Type::spring; }

        struct Settings
        {
            bool on = false;
            Type type = Type::digital;
            float size = 0.5f;        // 0 to 1
            float tone = 6.0f;        // 0 (dark) to 10 (bright)
            float mix = 0.28f;        // 0 to 1
            float preDelayMs = 20.0f;
            float duck = 0.0f;        // 0 to 1: DUCK
            float decay = 1.0f;       // DECAY: x the type's own
            float bassRt = 1.0f;      // BASS RT: x DECAY, below CROSSOVER
            float crossoverHz = 300.0f;
            float rolloffHz = rolloffOffHz;
            float diffusion = 1.0f;   // x the type's own
            float earlyDb = 0.0f, reverbDb = 0.0f;
            float spreadMs = 0.0f;
            float shape = 0.5f;       // 0 early, 0.5 even, 1 late
            float spin = 1.0f, wander = 1.0f;   // x the type's own drift rate and depth
        };

        void prepare (double sampleRate, int maxBlockSize);
        void process (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept;

        // Exposed for tests (and the stomp): how long the tail lasts (to
        // -60 dB), in seconds; `decay` is DECAY's multiple.
        static float decaySeconds (Type type, float size, float decay = 1.0f) noexcept;

    private:
        static constexpr int numLines = 8;

        void resetAll() noexcept;
        void processClassic (juce::dsp::AudioBlock<float> wetBlock, const Settings& settings) noexcept;
        void processNetwork (juce::dsp::AudioBlock<float> wetBlock, const Settings& settings) noexcept;
        void processSpring (juce::dsp::AudioBlock<float> wetBlock, const Settings& settings) noexcept;
        void reverseChunks (juce::dsp::AudioBlock<float> wetBlock, const Settings& settings) noexcept;

        double sampleRate = 48000.0;

        // Classic
        juce::dsp::Reverb classic;
        float lastSize = -1.0f, lastDamping = -1.0f;   // -1: not set yet

        // Shared: the send (muted for trails), the mix, pre-delay, and the
        // wet signal being built.
        juce::SmoothedValue<float> send, mix, preDelay;
        juce::AudioBuffer<float> wet;
        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> preDelayLine;
        bool running = false;
        Type lastType = Type::hall;
        Ducker ducker;
        TailWatch tail;

        // A simple circular buffer with fractional reads.
        struct Line
        {
            std::vector<float> data;
            int mask = 0, write = 0;
            void prepare (int minimumLength);
            void clear() { std::fill (data.begin(), data.end(), 0.0f); write = 0; }
            void push (float x) noexcept { data[(size_t) write] = x; write = (write + 1) & mask; }
            float at (float delay) const noexcept;   // `delay` samples ago (at least 1)
        };

        // The network
        std::array<Line, numLines> lines;
        std::array<float, numLines> lineDamping {}, linePhase {}, lineBass {};
        std::array<Line, maxChannels> spreadLines;     // SPREAD: the input, read at taps across its window
        float rolloffState[maxChannels][2] {};
        std::array<Line, maxChannels * 4> diffusers;   // input diffusion: four all-passes per channel
        std::array<Line, maxChannels> earlyTaps;       // early reflections, read from the pre-delayed input
        float outputLowCut[maxChannels] {};

        // Spring: two springs (left and right), each a delay loop through a
        // chain of all-passes that smear the sound out in time by pitch.
        static constexpr int springStages = 40;
        std::array<Line, maxChannels> springLoops;
        float springAllPass[maxChannels][springStages][2] {};   // each stage's previous input and output
        float springLowPass[maxChannels][2] {}, springHighPass[maxChannels] {}, springInputHigh[maxChannels] {}, springOutput[maxChannels][2] {};

        // Reverse: the wet signal recorded, and two read heads playing it backwards.
        std::array<Line, maxChannels> reverseBuffers;
        struct ReverseHead { int start = 0; int count = 0; };
        std::array<ReverseHead, 2> reverseHeads {};
        int reverseLength = 0;
    };
}
