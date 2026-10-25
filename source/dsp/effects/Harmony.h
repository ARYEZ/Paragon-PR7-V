#pragma once

#include "EffectUtils.h"
#include "dsp/PitchDetector.h"
#include "dsp/Scales.h"

namespace effects
{
    // HARMONY, on the PITCH stomp: up to eight harmony voices made from the
    // singer's own voice, kept in the song's key. Inspired by Waves Harmony
    // (the owner's reference): our own design and code, following how its
    // user guide describes the instrument.
    //
    // Where it sits: at the start of the stereo part of the chain, after the
    // graphic EQ, so the harmonies have the same compressed, saturated and
    // EQ'd tone as the lead, then go through the doubler, modulation, delay
    // and reverb with it. The note being sung is read from the clean voice
    // (straight after LOW CUT), where it's clearest.
    //
    // How a voice is made (PSOLA, "pitch-synchronous overlap-add"): the voice
    // is cut into short slices, one per vocal-cord pulse, each two pulses
    // long and faded at both ends. Laying the slices down closer together
    // raises the pitch; further apart lowers it. Each slice keeps the shape
    // of the voice's resonances (its formants), so a voice raised a fifth
    // still sounds like a person rather than a chipmunk. FORMANT moves those
    // resonances on their own, by reading each slice a little faster or
    // slower. Between notes (consonants, breaths) the slices are laid down
    // at the original pitch.
    //
    // The singer's own voice isn't delayed at all (the plugin has no
    // latency). Each harmony voice comes out about two vocal pulses behind
    // it, the time its slices need: 8 ms on a high note to 25 ms on a low
    // one, like a second singer a few metres away.
    //
    // CORRECT (stage 2; Waves Harmony's CORRECT button on the source): the
    // singer's own voice tuned to the nearest note in the KEY and SCALE, by
    // the same slices. NATURAL glides to the note (about 60 ms, so vibrato
    // and slides mostly survive); HARD jumps to it (about 3 ms: the robotic
    // effect). It works whether HARMONY is on or not. Tuning the voice needs
    // its slices, so while CORRECT is on the plugin runs `latency` samples
    // late (the processor reports it, so a DAW lines the voice back up) and
    // the voices come out at exactly that delay too, in time with the voice.
    // The speeds and the hold between notes are our choices: the guide
    // doesn't give them. Its NOTES state (tune to played or generated notes)
    // waits for MIDI input and Generate Notes.
    class Harmony
    {
    public:
        static constexpr int numVoices = 8;
        static constexpr float silentDb = -60.0f;   // levels at or below this are off
        static constexpr double lowestHz = 60.0, highestHz = 1100.0;   // low bass voice to high soprano

        struct Voice
        {
            bool active = false;    // on the graph at all
            bool on = true;         // sounding
            float semitones = 0.0f; // PITCH: the interval, before correction
            float cents = 0.0f;     // FINE, after correction
            float formant = 0.0f;   // FORMANT, semitones
            bool correct = true;    // into the key and scale
            float pan = 0.0f;       // -1 (left) to +1 (right)
            float levelDb = 0.0f;
        };

        // Saved by number: only ever add to the end.
        enum class Correct { off, natural, hard };
        static inline const juce::StringArray correctNames { "Off", "Natural", "Hard" };
        static constexpr double correctLatencySeconds = 0.04;   // what CORRECT delays the plugin by
        static constexpr double naturalSeconds = 0.06, hardSeconds = 0.003;

        struct Settings
        {
            bool on = false;
            Correct correct = Correct::off;
            int latency = 0;                    // samples the plugin is running late (CORRECT on), or 0
            int key = 0;                        // 0 = C
            Scales::Scale scale = Scales::Scale::major;
            float sourceDb = 0.0f;              // the singer's own voice
            float harmonyDb = 0.0f;             // all the harmony voices
            float correctFormant = 1.0f;        // 1: formants stay put; 0: they move with the pitch (chipmunk)
            float spread = 1.0f;                // 1: pans as set; 0: everything in the middle
            std::array<Voice, numVoices> voices;
        };

        void prepare (double sampleRate, int maxBlockSize);

        // `block`: the stereo chain, both channels still the same mono voice.
        // `cleanVoice`: the voice before any effects, for reading its pitch.
        void process (juce::dsp::AudioBlock<float> block, const float* cleanVoice, const Settings& settings) noexcept;

        // The note being sung as a MIDI number (fractional), or -1 between
        // notes. For the panel.
        float getSungNote() const noexcept { return sungNote.load(); }

        // Each voice's pitch ratio right now (1 = the singer's pitch). For tests.
        float getRatio (int voice) const noexcept { return voices[(size_t) voice].ratio; }
        // How far CORRECT is moving the singer's voice right now, in semitones. For tests and the panel.
        float getCorrection() const noexcept { return correction.load(); }
        // The note CORRECT is tuning to (a MIDI number), or -1. For the panel.
        float getCorrectNote() const noexcept { return correctNote.load(); }

    private:
        static constexpr int bufferSize = 1 << 15;   // ~0.7 s at 48 kHz: room for the longest slices
        static constexpr int mask = bufferSize - 1;
        static constexpr int hop = 128;              // how often the pitch is read

        struct VoiceState
        {
            std::vector<float> output;               // slices overlap-added here, ahead of time
            double nextSlice = 0.0;                  // where the next slice is centred (output time)
            bool running = false;
            float ratio = 1.0f, formantRatio = 1.0f;
            float leftGain = 0.0f, rightGain = 0.0f; // where the last block left them
        };

        void readPitch (const Settings& settings) noexcept;
        void resetVoices() noexcept;
        void addSlice (VoiceState& voice, double centre, double delay) noexcept;
        double sliceDelay (const VoiceState& voice, int fixedDelay) const noexcept;
        float nextFrom (VoiceState& voice, int fixedDelay) noexcept;   // lays down slices, returns this sample
        double nearestMark (double target, double latest) const noexcept;
        float readInput (double position) const noexcept;

        double sampleRate = 48000.0;
        PitchDetector detector { lowestHz, highestHz };
        std::vector<float> input, clean, window;     // rings, and the detector's window
        juce::int64 now = 0;                         // samples since prepare
        int samplesToNextRead = 0;

        // The vocal pulses: the period, and the marks one period apart that
        // slices are centred on.
        double period = 240.0;                       // samples
        bool voiced = false;
        int unvoicedFor = 0;                         // samples since the last clear note
        std::array<double, 64> marks {};
        int newestMark = 0;
        double nextMark = 0.0;

        std::array<VoiceState, numVoices> voices;
        VoiceState corrected;                        // the singer's voice, tuned (CORRECT)
        int latencyInUse = 0;                        // the delay the last block ran at
        int correctTarget = -1;                      // the note it's tuned to, held between readings
        double correctSemitones = 0.0;               // the glide towards it
        std::atomic<float> correction { 0.0f }, correctNote { -1.0f };
        float sourceGain = 1.0f;
        std::atomic<float> sungNote { -1.0f };
        BypassFade bypass;
    };
}
