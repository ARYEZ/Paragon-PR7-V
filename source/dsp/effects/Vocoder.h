#pragma once

#include "EffectUtils.h"
#include "../PitchDetector.h"
#include "../Scales.h"

#include <atomic>

namespace effects
{
    // The VOCODER block, inspired by the Vocoder module of iZotope
    // VocalSynth 2 (the owner sent its manual); our own design wherever the
    // manual doesn't say how something works.
    //
    // A vocoder needs two sounds. The voice (the modulator) is split into
    // frequency bands and how loud it is in each band is measured, many
    // times a second. A synth (the carrier) is split into the same bands,
    // and each of its bands is turned up and down by the voice's. What comes
    // out is the synth, shaped into the voice's vowels and consonants: the
    // robot voice. The synth here plays:
    //
    //   Auto   the pitch you sing (read from your clean voice), plus up to
    //          three INTERVAL voices, a diatonic step away in HARMONY's KEY
    //          and SCALE (2nd, 3rd ... octave, up or down)
    //   MIDI   your keyboard: MONO (one note, GLIDE between them) or POLY
    //          (up to 8 at once)
    //
    // MODE: how many bands (SMOOTH 12, VINTAGE 20, HARD 32; the manual
    // gives no counts, these are ours), spaced evenly in pitch from 90 Hz
    // to 11 kHz. SHIFT moves the synth's bands up or down against the
    // voice's (brighter or darker). SCALE: how fast the bands follow the
    // voice (high: crisp, every consonant; low: soft, pad-like).
    //
    // Each band has a VOL and PAN, kept as a 32-point curve across the
    // range (so a band reads the curve where it sits, whatever the MODE).
    //
    // The synth: two oscillators (SAW, SQUARE, SYNC SAW, TRIANGLE, SAWS;
    // GAIN, COARSE, FINE, SHAPE, and how far the LFO moves the pitch and
    // the shape), a NOISE (its CRUSH from buzzing at the note's pitch to
    // white hiss) and one LFO for them all. Then the MIX: PAN and a low
    // and high cut on what comes out, LEVEL, and DRY (what came in).
    //
    // The synth's bands are levelled before the voice shapes them (each is
    // divided by its own level, up to 24 dB), so whichever waveform plays,
    // what comes out follows the voice's own tone; that's what keeps the
    // words clear. Our choice; the manual doesn't say.
    class Vocoder
    {
    public:
        // Saved by number in rigs and projects: only ever add to the end.
        enum class Mode { smooth, vintage, hard };
        static inline const juce::StringArray modeNames { "Smooth", "Vintage", "Hard" };
        static constexpr int bandCounts[] = { 12, 20, 32 };
        static constexpr int maxBands = 32, curvePoints = 32;
        static constexpr float lowestHz = 90.0f, highestHz = 11000.0f;

        enum class Carrier { autoPitch, midi };
        static inline const juce::StringArray carrierNames { "Auto", "MIDI" };
        enum class Voicing { mono, poly };
        static inline const juce::StringArray voicingNames { "Mono", "Poly" };
        // INTERVAL: Off, then diatonic steps (in the key's scale) up and down.
        static inline const juce::StringArray intervalNames { "Off", "Unison", "2nd Up", "3rd Up", "4th Up", "5th Up", "6th Up",
                                                              "7th Up", "Octave Up", "2nd Down", "3rd Down", "4th Down",
                                                              "5th Down", "6th Down", "7th Down", "Octave Down" };
        static constexpr int intervalSteps[] = { 0, 0, 1, 2, 3, 4, 5, 6, 7, -1, -2, -3, -4, -5, -6, -7 };

        enum Wave { saw, square, syncSaw, triangle, saws };
        static inline const juce::StringArray waveNames { "Saw", "Square", "Sync Saw", "Triangle", "Saws" };
        enum LfoShape { sineLfo, triangleLfo, sawLfo, squareLfo, randomLfo };
        static inline const juce::StringArray lfoShapeNames { "Sine", "Triangle", "Saw", "Square", "Random" };
        static inline const juce::StringArray noiseModNames { "LFO", "Pitch" };

        static constexpr int maxVoices = 8, numIntervals = 3;
        static constexpr float offDb = -60.0f;       // a level at or below this is off
        static constexpr float minBandDb = -30.0f;   // a band's VOL at the bottom: off
        static constexpr float pitchModSemitones = 2.0f;   // PITCH MOD at 100%: the LFO moves the pitch this far either way
        static constexpr float maxBandBoost = 16.0f;       // how far a quiet synth band is brought up (24 dB)
        static constexpr int defaultNote = 48;             // Auto, before any note is sung: C3

        struct Osc
        {
            bool on = true;
            int wave = saw;
            float gainDb = 0.0f;
            float coarse = 0.0f;      // semitones
            float fineCents = 0.0f;
            float pitchMod = 0.0f;    // 0 to 1: the LFO on the pitch (pitchModSemitones at 1)
            float shape = 0.0f;       // 0 to 1
            float shapeMod = 0.0f;    // 0 to 1: the LFO on SHAPE
        };
        struct Noise
        {
            bool on = false;
            float gainDb = -12.0f;
            float crush = 0.5f;       // 0: buzzing at the note's pitch; 1: white noise
            float crushMod = 0.0f;    // -1 to 1
            int modSource = 0;        // noiseModNames
        };
        struct Settings
        {
            bool on = false;
            Mode mode = Mode::vintage;
            float shift = 0.0f;       // semitones, the synth's bands against the voice's
            float scale = 0.5f;       // 0 (soft) to 1 (crisp)
            float levelDb = 0.0f;
            float dryDb = offDb;      // what came in (the voice and anything before), under the vocoder
            Carrier carrier = Carrier::autoPitch;
            Voicing voicing = Voicing::mono;
            float glideMs = 0.0f;     // MIDI Mono: the time to slide to a new note
            std::array<int, numIntervals> intervals {};   // intervalNames
            float intervalDb = -6.0f; // the INTERVAL voices against your own
            int key = 0;
            Scales::Scale scaleType = Scales::Scale::major;
            const Scales::Custom* custom = nullptr;
            std::array<Osc, 2> osc { Osc {}, Osc { false, square, -6.0f, -12.0f } };   // as the parameters start
            Noise noise;
            int lfoShape = sineLfo;
            float lfoRateHz = 5.0f;
            float pan = 0.0f;
            float lowCutHz = 20.0f, highCutHz = 20000.0f;
            std::array<float, curvePoints> bandDb {};    // VOL, minBandDb to +6
            std::array<float, curvePoints> bandPan {};   // PAN, -1 to 1
        };

        Vocoder();
        void prepare (double sampleRate, int maxBlockSize);
        void reset();

        // The keys held (for MIDI): read before each block.
        void handleMidi (const juce::MidiBuffer&) noexcept;

        // `block`: what comes in (turned to DRY), with the vocoder added;
        // `modulator`: the voice that does the talking (mono); `clean`: the
        // clean voice, for Auto's pitch. Both as long as the block.
        void process (juce::dsp::AudioBlock<float> block, const float* modulator, const float* clean, const Settings&) noexcept;

        // Where band `band` of `count` sits (Hz), and its place along the range (0 to 1).
        static float bandHz (int band, int count) noexcept;
        static float bandPlace (int band, int count) noexcept { return count > 1 ? (float) band / (float) (count - 1) : 0.5f; }
        // A curve's value at a place (0 to 1) along it, between its points.
        static float curveAt (const std::array<float, curvePoints>&, float place) noexcept;

        // For the panel and tests (any thread): how loud the voice is in each band now (0 to 1-ish), the bands in use, the notes playing.
        float getBandLevel (int band) const noexcept { return shownBands[(size_t) juce::jlimit (0, maxBands - 1, band)].load(); }
        int getNumBands() const noexcept { return shownNumBands.load(); }
        float getVoiceNote (int voice) const noexcept { return shownNotes[(size_t) juce::jlimit (0, maxVoices - 1, voice)].load(); }   // -1: silent
        float getSungNote() const noexcept { return sungNote.load(); }

        // One sample of an oscillator's wave at phase `phase` (0 to 1) moving
        // `increment` a sample, with SHAPE (for tests; SYNC SAW and SAWS are
        // drawn from their state, not here).
        static float waveSample (int wave, float phase, float increment, float shape) noexcept;

    private:
        // A 2-pole band-pass (TPT state-variable), run twice in a row for each band.
        struct BandPass
        {
            float g = 0.0f, k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
            float s1 = 0.0f, s2 = 0.0f;
            void set (float hz, float q, double sampleRate) noexcept;
            float process (float x) noexcept;
            void clear() noexcept { s1 = s2 = 0.0f; }
        };
        struct Band
        {
            std::array<BandPass, 2> voice, synth;
            float voiceLevel = 0.0f, synthLevel = 0.0f;
            float gainLeft = 1.0f, gainRight = 1.0f;   // VOL and PAN
        };
        struct SynthVoice
        {
            int note = -1;            // the key (MIDI) or Auto's voice it's playing
            float targetNote = 48.0f, currentNote = 48.0f;
            float gain = 0.0f;        // its envelope
            bool gate = false;
            std::uint32_t age = 0;    // when its note began (the oldest is stolen first)
            std::array<std::array<float, 7>, 2> phases {};   // each oscillator's (SAWS: seven)
            std::array<float, 2> syncPhase {};               // SYNC SAW's second saw
            float level = 1.0f;       // Auto's INTERVAL voices are quieter
        };

        void readPitch (const float* clean, int numSamples) noexcept;
        void setVoices (const Settings&) noexcept;
        void setBands (const Settings&) noexcept;
        float renderSynth (const Settings&, float lfo, float glide, float attack, float release) noexcept;
        float nextLfo (const Settings&) noexcept;
        int diatonic (int note, int steps, const Settings&) const noexcept;

        double sampleRate = 48000.0;
        BypassFade bypass;
        std::array<Band, maxBands> bands;
        int numBands = 20;
        Mode bandsMode = Mode::vintage;
        float bandsShift = 1000.0f;
        std::array<SynthVoice, maxVoices> voices;
        std::uint32_t clock = 0;
        std::vector<float> left, right;   // the vocoder's output, a block at a time
        juce::SmoothedValue<float> dry, level;
        juce::dsp::StateVariableTPTFilter<float> lowCut, highCut;   // MIX's cuts (both channels each)

        // MIDI: each key's velocity (0: not held) and when it was pressed.
        std::array<float, 128> keyVelocity {};
        std::array<std::uint32_t, 128> keyOrder {};
        std::uint32_t keyClock = 0;

        // Auto's pitch reading: the clean voice in a ring, read every hop.
        static constexpr int ringSize = 1 << 13, hop = 128;
        std::vector<float> ring, window;
        int ringWrite = 0, sinceRead = 0, heldFor = 0;
        PitchDetector detector { 70.0, 1000.0 };
        float autoNote = (float) defaultNote;

        // The LFO and the noise.
        double lfoPhase = 0.0;
        float lfoHeld = 0.0f;
        juce::Random random { 7 };
        float noiseHeld = 0.0f;
        float noiseCount = 0.0f;

        std::array<std::atomic<float>, maxBands> shownBands {};
        std::atomic<int> shownNumBands { 20 };
        std::array<std::atomic<float>, maxVoices> shownNotes {};
        std::atomic<float> sungNote { -1.0f };
    };
}
