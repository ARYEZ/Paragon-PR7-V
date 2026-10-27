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
    // doesn't give them.
    //
    // GENERATE NOTES and CHORD MAPS (stage 3; the guide's Note Generator and
    // Note Mapper): the singing is turned into steady notes (a new one after
    // a silence, or when the pitch moves more than TOLERANCE cents for
    // longer than TIME), and each note into up to eight NOTE VOICES by the
    // CHORD map, kept in the KEY and SCALE as the fixed voices are. Note
    // voices play their notes exactly, whatever the singer's own tuning, and
    // fade in and out (ATTACK, RELEASE). They have their own pool of eight,
    // apart from the fixed voices (on Waves Harmony the two share eight).
    // The chord maps are ours; the guide names none. CORRECT's NOTES state
    // tunes the singer hard to the generated note.
    //
    // MIDI NOTES (stage 4): keys played on a MIDI keyboard (or a MIDI track)
    // become note voices too, sharing the eight with generated notes: AS
    // PLAYED, exactly the keys held (no key correction: a played chord is
    // sung as played), or THROUGH CHORD, each key through the CHORD map and
    // into the key, as a generated note is. Velocity sets each one's level
    // (soft keys quieter: our curve, 30% to 100%); the sustain pedal holds
    // them. CORRECT's NOTES state then tunes the singer to the nearest held
    // key (in whichever octave is nearest), the played "robot" effect.
    //
    // NOTE MAP EDITOR (stage 5): CHORD Custom plays your own map, note by
    // note (SPREAD or FOLD), and SCALE Custom your own scale, each note in it
    // or sent up or down to the next one that is.
    //
    // PER-VOICE DELAY, FEEDBACK, FILTER and FLATTEN (stage 6; the guide's
    // Harmony Panel and Notes Panel): each fixed voice, and the note voices
    // together, can be filtered (low-pass, high-pass or band-pass, 12 dB per
    // octave), then delayed (with repeats, FEEDBACK). FLATTEN holds a fixed
    // voice on one NOTE whatever is sung (still put into the key when its
    // CORRECT is on, and FINE after that). The order (filter, then delay),
    // the slope, DELAY 0 meaning none, the 2 s longest echo and FEEDBACK
    // 100% fading very slowly rather than building forever are our choices:
    // the guide doesn't give them.
    //
    // GLOBAL (stage 7; the guide's Global Controls): GLIDE, how smoothly
    // each harmony voice moves to a new pitch (1 ms: straight there; the
    // note voices never faster than their own 30 ms); PITCH, an offset for
    // every harmony voice after the key correction and note mapping; and
    // FORMANT, added to every harmony voice's own. The singer's own voice
    // is left alone by PITCH and FORMANT (our reading of "the entire
    // process"), and GLIDE follows the whole pitch curve, so a long one also
    // smooths how closely the voices follow vibrato.
    class Harmony
    {
    public:
        static constexpr int numVoices = 8;
        static constexpr float silentDb = -60.0f;   // levels at or below this are off
        static constexpr double lowestHz = 60.0, highestHz = 1100.0;   // low bass voice to high soprano

        // Saved by number: only ever add to the end.
        static inline const juce::StringArray filterNames { "Low-pass", "High-pass", "Band-pass" };
        static constexpr float filterLowestHz = 20.0f, filterHighestHz = 20000.0f;   // LOW-PASS at the top, HIGH-PASS at the bottom: off
        static constexpr float maxDelayMs = 1000.0f;       // DELAY's range (free)
        static constexpr double maxEchoSeconds = 2.0;      // the longest echo, synced or not
        static constexpr float feedbackAtFull = 0.97f;     // the loop's gain at FEEDBACK 100%: repeats fade very slowly
        static constexpr float flattenDefault = 48.0f;     // FLATTEN's NOTE to start: C3 (MIDI), as in the guide

        // A voice's (or the note voices') DELAY, FEEDBACK and FILTER.
        struct Fx
        {
            float delayMs = 0.0f;      // 0: none (the processor turns a synced time into ms)
            float feedback = 0.0f;     // 0 to 1
            int filter = 0;            // filterNames
            float cutoffHz = filterHighestHz;
            bool isNeutral() const noexcept;
        };

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
            bool flatten = false;   // FLATTEN: play NOTE whatever is sung
            float note = flattenDefault;
            Fx fx;
        };

        // Saved by number: only ever add to the end.
        enum class Correct { off, natural, hard, notes };
        static inline const juce::StringArray correctNames { "Off", "Natural", "Hard", "Notes" };

        // CHORD maps: the intervals each generated note becomes (in semitones,
        // corrected into the key so a third stays a third). Saved by number:
        // only ever add to the end.
        struct ChordMap
        {
            const char* name;
            const char* description;
            std::array<int, 8> intervals;
            int count;
        };
        static inline const std::array<ChordMap, 19> chordMaps { {
            { "3rd Above",            "A harmony a third above every note",                    { 4 }, 1 },
            { "3rd Below",            "A harmony a third below every note",                    { -4 }, 1 },
            { "5th Above",            "A fifth above every note",                              { 7 }, 1 },
            { "3rd + 5th Above",      "The classic three-part harmony, both above you",        { 4, 7 }, 2 },
            { "3rd Above, 4th Below", "One above and one below: a full triad around you",      { 4, -5 }, 2 },
            { "Octave Up",            "Your note an octave up",                                { 12 }, 1 },
            { "Octave Down",          "Your note an octave down",                              { -12 }, 1 },
            { "Octaves Up + Down",    "An octave above and below",                             { 12, -12 }, 2 },
            { "Power (5th + Octave)", "A fifth and an octave above: big and open",             { 7, 12 }, 2 },
            { "Three-Part Stack",     "A third, a fifth and an octave above",                  { 4, 7, 12 }, 3 },
            { "Choir",                "Five voices around you, from an octave down to an octave up", { -12, -5, 4, 7, 12 }, 5 },
            { "Triad Below",          "A chord under you: you sing the top note",              { -4, -7 }, 2 },
            { "Seventh Chord",        "A seventh chord on every note",                         { 4, 7, 11 }, 3 },
            { "Sus2",                 "A suspended second chord: open, unresolved",            { 2, 7 }, 2 },
            { "Sus4",                 "A suspended fourth chord",                              { 5, 7 }, 2 },
            { "Add9",                 "A major or minor chord with a ninth on top",            { 4, 7, 14 }, 3 },
            { "Open Fifths",          "An octave below and a fifth above: hollow and wide",    { -12, 7 }, 2 },
            { "Gospel Stack",         "A wide chord under and over you: low root, third, fifth, octave", { -12, -8, 4, 7, 12 }, 5 },
            { "Custom",               "Your own note map: click EDIT to make it",              {}, 0 },
        } };
        static constexpr int customChord = 18;

        // The Custom chord map (the Note Map Editor, inspired by Waves
        // Harmony's Note Mapper): for each of the 12 notes, which notes it
        // plays, as a bit per semitone from 23 below it (bit 0) to 23 above
        // (bit 46). SPREAD: they move with the octave sung or played; FOLD:
        // they stay where they were set, in the octave from middle C (C4).
        static constexpr int mapSpan = 23, mapBits = 2 * mapSpan + 1;
        static constexpr int maxShift = 24;   // semitones a voice can move (its pitch ratio is kept within 0.25-4)
        static constexpr int foldOctave = 60;   // C4
        using NoteMap = std::array<std::uint64_t, 12>;
        static juce::StringArray chordMapNames();

        static constexpr int numNoteVoices = 8;
        static constexpr double minNoteLowest = 24.0, minNoteHighest = 48.0;   // MIN NOTE's range: C1 to C3 (MIDI)
        static constexpr float toleranceOff = 210.0f;                          // TOLERANCE above 200 cents: off

        struct Notes
        {
            bool generate = false;
            int chord = 3;                   // 3rd + 5th Above
            float minNote = 31.0f;           // MIDI: G1 (the guide's G0, in its naming)
            float toleranceCents = 50.0f;    // above 200: off (only a silence starts a new note)
            float toleranceMs = 100.0f;
            float levelDb = -3.0f;
            float width = 0.7f;              // how far apart the note voices are panned
            float formant = 0.0f;
            float attackMs = 20.0f, releaseMs = 150.0f;
            int midiMap = 0;                 // MIDI: 0 As Played, 1 Through Chord
            NoteMap customMap {};            // CHORD Custom
            bool fold = false;
            Fx fx;                           // shared by the note voices
        };
        static inline const juce::StringArray midiMapNames { "As Played", "Through Chord" };

        // The MIDI keys held (stage 4, MIDI note input): kept by the processor
        // from block to block. The sustain pedal (CC 64) holds released keys.
        struct MidiKeys
        {
            std::array<float, 128> velocity {};   // 0: not held; else 0 to 1
            std::array<bool, 128> pedalled {};    // let go while the pedal was down
            bool pedal = false;
            void read (const juce::MidiBuffer&) noexcept;
            bool any() const noexcept;
        };
        static constexpr float minGlideMs = 1.0f, maxGlideMs = 1000.0f;   // GLIDE's range; at the bottom, straight to the note
        static constexpr float noteGlideMs = 30.0f;                      // the note voices' own glide time constant (GLIDE only slows them)
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
            Notes notes;
            const MidiKeys* midi = nullptr;
            Scales::Custom customScale {};      // SCALE Custom (global, like KEY and SCALE)
            float glideMs = minGlideMs;         // GLOBAL: GLIDE (the processor turns a synced one into ms)
            float pitch = 0.0f;                 // GLOBAL: PITCH, semitones
            float formant = 0.0f;               // GLOBAL: FORMANT, semitones
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
        // The generated note (a MIDI number, in the key), or -1.
        int getGeneratedNote() const noexcept { return generatedNote.load(); }
        // A note voice's note (MIDI), or -1 when silent; its pan and level (0 to 1) as it sounds now. For the panel.
        int getNoteVoiceNote (int voice) const noexcept { return noteVoiceShown[(size_t) voice].note.load(); }
        float getNoteVoicePan (int voice) const noexcept { return noteVoiceShown[(size_t) voice].pan.load(); }
        float getNoteVoiceLevel (int voice) const noexcept { return noteVoiceShown[(size_t) voice].level.load(); }
        // A note voice's pitch ratio right now. For tests.
        float getNoteVoiceRatio (int voice) const noexcept { return noteVoices[(size_t) voice].state.ratio; }

    private:
        static constexpr int bufferSize = 1 << 15;   // ~0.7 s at 48 kHz: room for the longest slices
        static constexpr int mask = bufferSize - 1;
        static constexpr int hop = 128;              // how often the pitch is read

        // FILTER then DELAY (with FEEDBACK), after a voice is made.
        struct VoiceFx
        {
            std::vector<float> line;                 // the echo
            int write = 0;
            double delay = 0.0;                      // samples, gliding to `target` (no clicks when DELAY moves)
            double target = 0.0;
            float loop = 0.0f;
            int filter = -1;                         // -1: no filter
            float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, k = 1.41421356f, s1 = 0.0f, s2 = 0.0f;
            int ringing = 0;                         // samples the echo still has to play
            void set (const Fx&, double sampleRate) noexcept;   // once a block
            float process (float x) noexcept;
            void clear() noexcept;
        };

        struct VoiceState
        {
            std::vector<float> output;               // slices overlap-added here, ahead of time
            double nextSlice = 0.0;                  // where the next slice is centred (output time)
            bool running = false;
            float ratio = 1.0f, formantRatio = 1.0f;
            float leftGain = 0.0f, rightGain = 0.0f; // where the last block left them
            double glidedNote = -1.0;                // GLIDE: the pitch it's gliding along (MIDI), or -1 between notes
            VoiceFx fx;
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

        // GENERATE NOTES: the note held, and how long the voice has been away from it.
        int heldNote = -1;
        double awayFor = 0.0;
        std::atomic<int> generatedNote { -1 };
        void generateNotes (const Settings&, double sung) noexcept;
        // What one incoming note (sung or played) becomes through the CHORD map, in the key.
        template <typename Want> static void mapNote (const Settings&, int incoming, bool inKeyFirst, Want&& want);
    public:
        // The notes the Custom map plays for `incoming` (before the scale), into
        // `out`; returns how many. No allocation: it runs on the audio thread.
        static int customNotes (const NoteMap&, bool fold, int incoming, std::array<int, mapBits>& out) noexcept;
    private:

        struct NoteVoice
        {
            VoiceState state;
            int note = -1;                           // what it plays (MIDI), or -1: let go
            double glided = 0.0;                     // its pitch, gliding to `note` (MIDI)
            float envelope = 0.0f, pan = 0.0f, panTarget = 0.0f;
            float velocity = 1.0f;
        };
        std::array<NoteVoice, numNoteVoices> noteVoices;
        struct Shown { std::atomic<int> note { -1 }; std::atomic<float> pan { 0.0f }, level { 0.0f }; };
        std::array<Shown, numNoteVoices> noteVoiceShown;
        int latencyInUse = 0;                        // the delay the last block ran at
        int correctTarget = -1;                      // the note it's tuned to, held between readings
        double correctSemitones = 0.0;               // the glide towards it
        std::atomic<float> correction { 0.0f }, correctNote { -1.0f };
        float sourceGain = 1.0f;
        std::atomic<float> sungNote { -1.0f };
        BypassFade bypass;
    };
}
