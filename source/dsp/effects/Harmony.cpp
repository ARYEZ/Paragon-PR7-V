#include "Harmony.h"

namespace effects
{
    namespace
    {
        float gainFor (float db) noexcept { return db <= Harmony::silentDb ? 0.0f : juce::Decibels::decibelsToGain (db); }

        // Between notes the slices are this long (5 ms), at the original pitch.
        constexpr double unvoicedSeconds = 0.005;

        // A gap in the note shorter than this (a flicker in the reading) keeps
        // the last note's pulse; longer, it's a consonant or a breath.
        constexpr double holdSeconds = 0.04;
    }

    void Harmony::prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        detector.prepare (sampleRate);
        input.assign ((size_t) bufferSize, 0.0f);
        clean.assign ((size_t) bufferSize, 0.0f);
        window.assign ((size_t) detector.windowSizeAtHostRate(), 0.0f);
        for (auto& voice : voices)
        {
            voice.output.assign ((size_t) bufferSize, 0.0f);
            voice.running = false;
            voice.ratio = voice.formantRatio = 1.0f;
            voice.leftGain = voice.rightGain = 0.0f;
            voice.fx.line.assign ((size_t) (maxEchoSeconds * sampleRate) + 2, 0.0f);
            voice.fx.clear();
        }
        for (auto& voice : noteVoices)
        {
            voice.state.output.assign ((size_t) bufferSize, 0.0f);
            voice.state.fx.line.assign ((size_t) (maxEchoSeconds * sampleRate) + 2, 0.0f);
            voice.state.fx.clear();
            voice.state.running = false;
            voice.state.ratio = voice.state.formantRatio = 1.0f;
            voice.note = -1;
            voice.envelope = voice.pan = 0.0f;
        }
        heldNote = -1;
        awayFor = 0.0;
        generatedNote = -1;
        corrected.output.assign ((size_t) bufferSize, 0.0f);
        corrected.running = false;
        corrected.ratio = corrected.formantRatio = 1.0f;
        correctTarget = -1;
        correctSemitones = 0.0;
        correction = 0.0f;
        correctNote = -1.0f;
        latencyInUse = 0;
        now = 0;
        samplesToNextRead = 0;
        period = unvoicedSeconds * sampleRate;
        voiced = false;
        unvoicedFor = 0;
        marks.fill (0.0);
        newestMark = 0;
        nextMark = 0.0;
        sourceGain = 1.0f;
        sungNote = -1.0f;
        bypass.prepare (sampleRate, maxBlockSize);
    }

    bool Harmony::Fx::isNeutral() const noexcept
    {
        const auto noFilter = (filter == 0 && cutoffHz >= filterHighestHz) || (filter == 1 && cutoffHz <= filterLowestHz);
        return noFilter && delayMs <= 0.0f;
    }

    void Harmony::VoiceFx::set (const Fx& fx, double sampleRate) noexcept
    {
        // The filter: a two-pole state-variable filter (12 dB per octave),
        // left out entirely where it would do nothing.
        const auto cutoff = juce::jlimit (filterLowestHz, filterHighestHz, fx.cutoffHz);
        const auto off = (fx.filter == 0 && cutoff >= filterHighestHz) || (fx.filter == 1 && cutoff <= filterLowestHz);
        if (off)
        {
            filter = -1;
        }
        else
        {
            if (filter < 0)
                s1 = s2 = 0.0f;
            filter = juce::jlimit (0, 2, fx.filter);
            const auto g = (float) std::tan (juce::MathConstants<double>::pi * juce::jmin ((double) cutoff, 0.45 * sampleRate) / sampleRate);
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }
        target = juce::jlimit (0.0, (double) line.size() - 2.0, fx.delayMs * 0.001 * sampleRate);
        if (delay <= 0.0 && ringing <= 0 && target > 0.0)
        {
            // Starting (DELAY was off): no glide from wherever it was, and
            // nothing left in the line from the last time it was used.
            std::fill (line.begin(), line.end(), 0.0f);
            write = 0;
            delay = target;
        }
        loop = feedbackAtFull * juce::jlimit (0.0f, 1.0f, fx.feedback);
    }

    float Harmony::VoiceFx::process (float x) noexcept
    {
        if (filter >= 0)
        {
            const auto v3 = x - s2;
            const auto v1 = a1 * s1 + a2 * v3;
            const auto v2 = s2 + a2 * s1 + a3 * v3;
            s1 = 2.0f * v1 - s1;
            s2 = 2.0f * v2 - s2;
            x = filter == 0 ? v2 : filter == 1 ? x - k * v1 - v2 : k * v1;   // band-pass: unity at the centre
        }
        if (target <= 0.0 && delay < 0.5)
        {
            delay = 0.0;
            return x;   // no delay (DELAY at 0): straight through
        }
        // A tape-like glide to a new time (about 30 ms), read between samples.
        delay += 0.0015 * (target - delay);
        const auto size = (int) line.size();
        auto position = (double) write - juce::jmax (1.0, delay);
        if (position < 0.0)
            position += size;
        const auto whole = (int) position;
        const auto fraction = (float) (position - whole);
        const auto a = line[(size_t) whole], b = line[(size_t) ((whole + 1) % size)];
        const auto y = a + (b - a) * fraction;
        line[(size_t) write] = x + loop * y;
        write = (write + 1) % size;
        if (std::abs (x) > 1.0e-6f || std::abs (y) > 1.0e-6f)
            ringing = (int) delay + 2;
        else if (ringing > 0)
            --ringing;
        return y;
    }

    void Harmony::VoiceFx::clear() noexcept
    {
        std::fill (line.begin(), line.end(), 0.0f);
        s1 = s2 = 0.0f;
        ringing = 0;
        delay = 0.0;
    }

    float Harmony::readInput (double position) const noexcept
    {
        const auto whole = (juce::int64) std::floor (position);
        const auto fraction = (float) (position - (double) whole);
        const auto a = input[(size_t) (whole & mask)], b = input[(size_t) ((whole + 1) & mask)];
        return a + (b - a) * fraction;
    }

    double Harmony::nearestMark (double target, double latest) const noexcept
    {
        auto best = latest;
        auto bestDistance = std::numeric_limits<double>::max();
        for (const auto mark : marks)
            if (mark <= latest && std::abs (mark - target) < bestDistance)
            {
                best = mark;
                bestDistance = std::abs (mark - target);
            }
        return best;
    }

    void Harmony::MidiKeys::read (const juce::MidiBuffer& buffer) noexcept
    {
        for (const auto metadata : buffer)
        {
            const auto message = metadata.getMessage();
            if (message.isNoteOn())
            {
                velocity[(size_t) message.getNoteNumber()] = juce::jmax (0.01f, message.getFloatVelocity());
                pedalled[(size_t) message.getNoteNumber()] = false;
            }
            else if (message.isNoteOff())
            {
                // Held on by the pedal, if it's down.
                if (pedal)
                    pedalled[(size_t) message.getNoteNumber()] = true;
                else
                    velocity[(size_t) message.getNoteNumber()] = 0.0f;
            }
            else if (message.isSustainPedalOn())
            {
                pedal = true;
            }
            else if (message.isSustainPedalOff())
            {
                pedal = false;
                for (size_t key = 0; key < velocity.size(); ++key)
                    if (pedalled[key])
                    {
                        velocity[key] = 0.0f;
                        pedalled[key] = false;
                    }
            }
            else if (message.isAllNotesOff() || message.isAllSoundOff())
            {
                velocity.fill (0.0f);
                pedalled.fill (false);
            }
        }
    }

    bool Harmony::MidiKeys::any() const noexcept
    {
        for (const auto v : velocity)
            if (v > 0.0f)
                return true;
        return false;
    }

    juce::StringArray Harmony::chordMapNames()
    {
        juce::StringArray names;
        for (const auto& map : chordMaps)
            names.add (map.name);
        return names;
    }

    int Harmony::customNotes (const NoteMap& map, bool fold, int incoming, std::array<int, mapBits>& out) noexcept
    {
        auto count = 0;
        const auto noteClass = ((incoming % 12) + 12) % 12;
        const auto bits = map[(size_t) noteClass];
        const auto from = fold ? foldOctave + noteClass : incoming;
        for (int bit = 0; bit < mapBits; ++bit)
            if ((bits >> bit) & 1u)
            {
                // FOLD can ask for more than a voice can shift (two octaves):
                // the same note, an octave nearer, until it fits.
                auto note = from + bit - mapSpan;
                while (note - incoming > maxShift)
                    note -= 12;
                while (incoming - note > maxShift)
                    note += 12;
                out[(size_t) count++] = note;
            }
        return count;
    }

    template <typename Want>
    void Harmony::mapNote (const Settings& settings, int incoming, bool, Want&& want)
    {
        const auto chord = juce::jlimit (0, (int) chordMaps.size() - 1, settings.notes.chord);
        if (chord == customChord)
        {
            // Your own map: each note it plays, put into the scale.
            std::array<int, mapBits> notes {};
            const auto count = customNotes (settings.notes.customMap, settings.notes.fold, incoming, notes);
            for (int i = 0; i < count; ++i)
                want (Scales::nearest (notes[(size_t) i], settings.key, settings.scale, &settings.customScale));
            return;
        }
        // A built-in map: the note into the key, then each interval from it (a third stays a third).
        const auto& map = chordMaps[(size_t) chord];
        const auto root = Scales::nearest (incoming, settings.key, settings.scale, &settings.customScale);
        for (int i = 0; i < map.count; ++i)
            want (Scales::harmonyNote (root, map.intervals[(size_t) i], settings.key, settings.scale, &settings.customScale));
    }

    void Harmony::generateNotes (const Settings& settings, double sung) noexcept
    {
        // GENERATE NOTES: hold a note until the voice has been more than
        // TOLERANCE cents away from it for TIME (then the nearest semitone
        // is the new note); a silence ends it, and the next sound starts one.
        const auto& notes = settings.notes;
        const auto wanted = notes.generate || settings.correct == Correct::notes;
        if (! wanted || ! voiced || sung < notes.minNote)
        {
            heldNote = -1;
            awayFor = 0.0;
        }
        else
        {
            const auto nearestSemitone = (int) std::lround (sung);
            if (heldNote < 0)
            {
                heldNote = nearestSemitone;
                awayFor = 0.0;
            }
            else if (notes.toleranceCents <= 200.0f && std::abs (sung - heldNote) * 100.0 > notes.toleranceCents)
            {
                awayFor += hop / sampleRate;
                if (awayFor * 1000.0 >= notes.toleranceMs)
                {
                    heldNote = nearestSemitone;
                    awayFor = 0.0;
                }
            }
            else
            {
                awayFor = 0.0;
            }
        }

        // The note in the key, then the CHORD map's notes from it, kept in
        // the key too (a third stays a third). Then the MIDI keys held.
        const auto root = heldNote >= 0 ? Scales::nearest (heldNote, settings.key, settings.scale, &settings.customScale) : -1;
        generatedNote = root;
        std::array<std::pair<int, float>, numNoteVoices> playing {};   // note, velocity
        auto count = 0;
        const auto want = [&playing, &count] (int note, float velocity)
        {
            if (note < 0 || note > 127)
                return;
            for (int i = 0; i < count; ++i)
                if (playing[(size_t) i].first == note)
                {
                    playing[(size_t) i].second = juce::jmax (playing[(size_t) i].second, velocity);
                    return;
                }
            if (count < numNoteVoices)
                playing[(size_t) count++] = { note, velocity };
        };
        if (settings.midi != nullptr)
            for (int key = 0; key < 128; ++key)
                if (const auto velocity = settings.midi->velocity[(size_t) key]; velocity > 0.0f)
                {
                    if (notes.midiMap == 0)
                    {
                        want (key, velocity);
                        continue;
                    }
                    mapNote (settings, key, true, [&want, velocity] (int note) { want (note, velocity); });
                }
        if (notes.generate && heldNote >= 0)
            mapNote (settings, heldNote, true, [&want] (int note) { want (note, 1.0f); });

        // Each voice keeps its note while it's still playing (no restart);
        // the rest let go, and new notes take free voices.
        std::array<bool, numNoteVoices> placed {};
        for (auto& voice : noteVoices)
        {
            auto keep = false;
            for (int i = 0; i < count && ! keep; ++i)
                if (! placed[(size_t) i] && playing[(size_t) i].first == voice.note)
                {
                    placed[(size_t) i] = keep = true;
                    voice.velocity = playing[(size_t) i].second;
                }
            if (! keep)
                voice.note = -1;
        }
        for (int i = 0; i < count; ++i)
        {
            if (placed[(size_t) i])
                continue;
            // The quietest free voice (the one that has faded furthest).
            NoteVoice* best = nullptr;
            for (auto& voice : noteVoices)
                if (voice.note < 0 && (best == nullptr || voice.envelope < best->envelope))
                    best = &voice;
            if (best == nullptr)
                break;
            best->note = playing[(size_t) i].first;
            best->velocity = playing[(size_t) i].second;
            placed[(size_t) i] = true;
        }
        // Panned across WIDTH, low notes left, high right.
        for (auto& voice : noteVoices)
        {
            if (voice.note < 0)
                continue;
            auto rank = 0;
            for (int i = 0; i < count; ++i)
                rank += playing[(size_t) i].first < voice.note ? 1 : 0;
            voice.panTarget = count > 1 ? notes.width * (-1.0f + 2.0f * (float) rank / (float) (count - 1)) : 0.0f;
        }

        // Each note voice plays its note exactly, gliding when it changes
        // (its own 30 ms, or GLIDE's if that's slower), then GLOBAL PITCH.
        const auto glideSeconds = 0.001 * (double) juce::jmax (noteGlideMs, settings.glideMs / 3.0f);
        const auto glide = 1.0 - std::exp (-(double) hop / (glideSeconds * sampleRate));
        for (auto& voice : noteVoices)
        {
            if (voice.note >= 0)
                voice.glided = voice.envelope < 1.0e-3f ? (double) voice.note : voice.glided + glide * ((double) voice.note - voice.glided);
            const auto shift = voiced ? voice.glided + (double) settings.pitch - sung : 0.0;
            voice.state.ratio = (float) juce::jlimit (0.25, 4.0, std::pow (2.0, shift / 12.0));
            const auto formantSemitones = notes.formant + settings.formant + (1.0f - settings.correctFormant) * (float) shift;
            voice.state.formantRatio = juce::jlimit (0.5f, 2.0f, std::pow (2.0f, formantSemitones / 12.0f));
        }
    }

    void Harmony::readPitch (const Settings& settings) noexcept
    {
        // The newest stretch of the clean voice, oldest first, for the detector.
        const auto length = (int) window.size();
        std::optional<PitchDetector::Result> result;
        if (now >= length)
        {
            for (int i = 0; i < length; ++i)
                window[(size_t) i] = clean[(size_t) ((now - length + i) & mask)];
            result = detector.detect (window.data(), length);
        }

        if (result.has_value())
        {
            voiced = true;
            unvoicedFor = 0;
            period = sampleRate / result->frequency;
        }
        else if ((unvoicedFor += hop) > (int) (holdSeconds * sampleRate))
        {
            voiced = false;
            period = unvoicedSeconds * sampleRate;
        }

        const auto sung = Scales::midiFromHz (sampleRate / period);
        sungNote = voiced ? (float) sung : -1.0f;

        generateNotes (settings, sung);

        // CORRECT: the note the singer is aiming at, held until another is
        // clearly nearer (a fifth of a semitone), so a voice right between
        // two notes doesn't flicker between them.
        auto wanted = 0.0;
        if (settings.correct == Correct::notes)
        {
            // NOTES: hard onto the nearest held MIDI key (in whichever octave
            // is nearest), or with none held, the generated note.
            correctTarget = generatedNote.load();
            if (settings.midi != nullptr && voiced)
            {
                auto bestDistance = 1000.0;
                for (int key = 0; key < 128; ++key)
                    if (settings.midi->velocity[(size_t) key] > 0.0f)
                    {
                        const auto octave = std::round ((sung - key) / 12.0);
                        const auto candidate = key + 12 * (int) octave;
                        if (std::abs (candidate - sung) < bestDistance)
                        {
                            bestDistance = std::abs (candidate - sung);
                            correctTarget = candidate;
                        }
                    }
            }
            if (voiced && correctTarget >= 0)
                wanted = juce::jlimit (-6.0, 6.0, (double) correctTarget - sung);
        }
        else if (settings.correct != Correct::off && voiced)
        {
            const auto nearestNote = Scales::nearest (sung, settings.key, settings.scale, &settings.customScale);
            if (correctTarget < 0 || ! Scales::isIn (correctTarget, settings.key, settings.scale, &settings.customScale)
                || (nearestNote != correctTarget && std::abs (sung - nearestNote) + 0.2 < std::abs (sung - correctTarget)))
                correctTarget = nearestNote;
            wanted = juce::jlimit (-6.0, 6.0, (double) correctTarget - sung);
        }
        else
        {
            correctTarget = -1;
        }
        const auto speed = settings.correct == Correct::natural ? naturalSeconds : hardSeconds;
        correctSemitones += (1.0 - std::exp (-(double) hop / (speed * sampleRate))) * (wanted - correctSemitones);
        corrected.ratio = (float) std::pow (2.0, correctSemitones / 12.0);
        corrected.formantRatio = 1.0f;   // the voice's own resonances stay put
        correction = (float) correctSemitones;
        correctNote = (float) correctTarget;

        // GLIDE is the time to get there (to within 5%: three time constants).
        const auto fixedGlide = 1.0 - std::exp (-(double) hop / (0.001 * (double) juce::jmax (minGlideMs, settings.glideMs) / 3.0 * sampleRate));
        for (size_t v = 0; v < voices.size(); ++v)
        {
            const auto& setting = settings.voices[v];
            auto shift = 0.0;   // semitones from the sung pitch
            if (voiced && setting.flatten)
            {
                // FLATTEN: NOTE whatever is sung (into the key with CORRECT on), then FINE.
                auto target = (double) setting.note;
                if (setting.correct)
                    target = Scales::nearest (target, settings.key, settings.scale, &settings.customScale);
                shift = target + setting.cents / 100.0 - sung;
            }
            else if (voiced)
            {
                if (setting.correct)
                {
                    // The note the singer is aiming at, then the harmony
                    // note in the key, sung exactly in tune.
                    const auto aimedAt = Scales::nearest (sung, settings.key, settings.scale, &settings.customScale);
                    const auto target = Scales::harmonyNote (aimedAt, juce::roundToInt (setting.semitones), settings.key, settings.scale, &settings.customScale);
                    shift = (double) target + setting.cents / 100.0 - sung;
                }
                else
                {
                    // Uncorrected: the interval exactly, following the singer's own tuning.
                    shift = setting.semitones + setting.cents / 100.0;
                }
            }
            auto& voice = voices[v];
            if (voiced)
            {
                // GLOBAL PITCH after everything else, then GLIDE along the
                // pitch it's heading for (straight there at the shortest,
                // and when a new note starts after a silence).
                const auto target = sung + shift + (double) settings.pitch;
                if (voice.glidedNote < 0.0 || settings.glideMs <= minGlideMs)
                    voice.glidedNote = target;
                else
                    voice.glidedNote += fixedGlide * (target - voice.glidedNote);
                shift = voice.glidedNote - sung;
            }
            else
            {
                voice.glidedNote = -1.0;
            }
            voice.ratio = (float) juce::jlimit (0.25, 4.0, std::pow (2.0, shift / 12.0));
            // CORRECT FORMANT at 1 keeps the voice's resonances where they
            // are; at 0 they move with the pitch, as a sped-up tape's do.
            // GLOBAL FORMANT adds to the voice's own.
            const auto formantSemitones = setting.formant + settings.formant + (1.0f - settings.correctFormant) * (float) shift;
            voice.formantRatio = juce::jlimit (0.5f, 2.0f, std::pow (2.0f, formantSemitones / 12.0f));
        }
    }

    double Harmony::sliceDelay (const VoiceState& voice, int fixedDelay) const noexcept
    {
        // What a slice needs (two pulses, more when FORMANT reads faster),
        // or the plugin's fixed delay while CORRECT is on, whichever is longer.
        const auto half = juce::jmax (8.0, period);
        const auto needed = half * (1.0 + juce::jmax (1.0, (double) voice.formantRatio)) + 4.0;
        return juce::jmax (needed, (double) fixedDelay);
    }

    float Harmony::nextFrom (VoiceState& state, int fixedDelay) noexcept
    {
        // Lay down every slice whose source audio has arrived, then take this sample.
        const auto half = juce::jmax (8.0, period);
        const auto read = (double) state.formantRatio;
        const auto delay = sliceDelay (state, fixedDelay);
        if (! state.running)
        {
            state.running = true;
            state.nextSlice = (double) now;
        }
        if (state.nextSlice < (double) now + half)
            state.nextSlice = (double) now + delay - half * read;   // fell behind (just started)
        while (state.nextSlice <= (double) now + delay - half * read)
        {
            addSlice (state, state.nextSlice, delay);
            state.nextSlice += half / (double) state.ratio;
        }
        auto& slot = state.output[(size_t) (now & mask)];
        const auto sample = slot;
        slot = 0.0f;
        return sample;
    }

    void Harmony::resetVoices() noexcept
    {
        for (auto& voice : noteVoices)
        {
            voice.envelope = 0.0f;
            voice.state.running = false;
            std::fill (voice.state.output.begin(), voice.state.output.end(), 0.0f);
            voice.state.fx.clear();
        }
        for (auto* state : { &corrected })
        {
            std::fill (state->output.begin(), state->output.end(), 0.0f);
            state->running = false;
        }
        for (auto& voice : voices)
        {
            std::fill (voice.output.begin(), voice.output.end(), 0.0f);
            voice.running = false;
            voice.leftGain = voice.rightGain = 0.0f;
            voice.glidedNote = -1.0;
            voice.fx.clear();
        }
    }

    void Harmony::addSlice (VoiceState& voice, double centre, double delay) noexcept
    {
        // One slice: two pulses of the voice, faded in and out (a Hann
        // window), centred on the analysis mark nearest where it's going.
        const auto half = juce::jmax (8, juce::roundToInt (period));
        const auto read = (double) voice.formantRatio;
        const auto from = nearestMark (centre - delay, (double) now - half * read);

        // Slices overlap more the higher the pitch, which would make the
        // voice louder: scale them so the overlaps add up to the original level.
        const auto gain = juce::jmin (2.0f, 1.0f / voice.ratio);
        const auto at = (juce::int64) std::llround (centre);
        for (int u = -half; u <= half; ++u)
        {
            const auto fade = 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * (float) u / (float) half);
            voice.output[(size_t) ((at + u) & mask)] += gain * fade * readInput (from + u * read);
        }
    }

    void Harmony::process (juce::dsp::AudioBlock<float> block, const float* cleanVoice, const Settings& settings) noexcept
    {
        // CORRECT on (the processor has the plugin running `latency` late):
        // the voice always goes through here, tuned; HARMONY's switch only
        // brings the harmony voices in and out. Off: as before, no delay.
        const auto latency = juce::jmax (0, settings.latency);
        if (latency != latencyInUse)
        {
            // Every voice changes its delay: start them afresh.
            resetVoices();
            latencyInUse = latency;
        }
        if (latency == 0 && settings.on && bypass.isSilent())
        {
            // Coming back on: start the voices afresh rather than from where
            // they were when it went off.
            resetVoices();
        }

        const auto run = [&] (juce::dsp::AudioBlock<float> wet, bool voicesOn)
        {
            const auto numSamples = (int) wet.getNumSamples();
            const auto stereo = wet.getNumChannels() > 1;
            auto* left = wet.getChannelPointer (0);
            auto* right = stereo ? wet.getChannelPointer (1) : nullptr;

            // Where each voice's gains are headed this block; they glide
            // there across it, so moving a level or pan doesn't click.
            const auto harmonyGain = gainFor (settings.harmonyDb);
            for (size_t v = 0; v < voices.size(); ++v)
                voices[v].fx.set (settings.voices[v].fx, sampleRate);
            for (auto& noteVoice : noteVoices)
                noteVoice.state.fx.set (settings.notes.fx, sampleRate);
            std::array<std::pair<float, float>, numVoices> targets {};
            for (size_t v = 0; v < voices.size(); ++v)
            {
                const auto& setting = settings.voices[v];
                if (! voicesOn || ! setting.active || ! setting.on)
                    continue;
                const auto pan = juce::jlimit (-1.0f, 1.0f, setting.pan * settings.spread);
                const auto gain = gainFor (setting.levelDb) * harmonyGain;
                // Centred, a voice is at full level on both sides, as the
                // singer's own voice is; panned, it fades from the far side.
                targets[v] = { gain * juce::jmin (1.0f, 1.0f - pan), gain * juce::jmin (1.0f, 1.0f + pan) };
            }
            // The note voices' fades, and their level.
            const auto noteGain = gainFor (settings.notes.levelDb) * harmonyGain;
            const auto attack = (float) (1.0 - std::exp (-1.0 / (juce::jmax (1.0f, settings.notes.attackMs) * 0.001 * sampleRate)));
            const auto release = (float) (1.0 - std::exp (-1.0 / (juce::jmax (1.0f, settings.notes.releaseMs) * 0.001 * sampleRate)));
            const auto panGlide = (float) (1.0 - std::exp (-1.0 / (0.02 * sampleRate)));

            // SOURCE is HARMONY's mix: with HARMONY off, the voice is at its own level.
            const auto sourceTarget = voicesOn ? gainFor (settings.sourceDb) : 1.0f;

            for (int i = 0; i < numSamples; ++i)
            {
                const auto voice = left[i];   // the processed voice, the same on both sides here
                input[(size_t) (now & mask)] = voice;
                clean[(size_t) (now & mask)] = cleanVoice[i];

                if (--samplesToNextRead <= 0)
                {
                    readPitch (settings);
                    samplesToNextRead = hop;
                }
                while ((double) now >= nextMark)
                {
                    newestMark = (newestMark + 1) % (int) marks.size();
                    marks[(size_t) newestMark] = nextMark;
                    nextMark += period;
                }

                const auto through = (float) (i + 1) / (float) numSamples;
                const auto singer = latency > 0 ? nextFrom (corrected, latency) : voice;
                const auto source = (sourceGain + (sourceTarget - sourceGain) * through) * singer;
                auto outLeft = source, outRight = source;

                for (size_t v = 0; v < voices.size(); ++v)
                {
                    auto& state = voices[v];
                    const auto [targetLeft, targetRight] = targets[v];
                    const auto sounding = targetLeft > 0.0f || targetRight > 0.0f || state.leftGain > 0.0f || state.rightGain > 0.0f;
                    if (! state.running && ! sounding)
                        continue;
                    const auto sample = state.fx.process (nextFrom (state, latency));
                    outLeft += (state.leftGain + (targetLeft - state.leftGain) * through) * sample;
                    outRight += (state.rightGain + (targetRight - state.rightGain) * through) * sample;
                }

                for (auto& noteVoice : noteVoices)
                {
                    const auto gate = voicesOn && noteVoice.note >= 0;
                    const auto echoing = noteVoice.state.fx.ringing > 0;
                    if (! noteVoice.state.running && ! gate && ! echoing)
                        continue;
                    noteVoice.envelope += (gate ? attack : release) * ((gate ? 1.0f : 0.0f) - noteVoice.envelope);
                    noteVoice.pan += panGlide * (noteVoice.panTarget - noteVoice.pan);
                    // Its echo carries on after the note has faded.
                    const auto made = noteVoice.state.running || gate
                                          ? nextFrom (noteVoice.state, latency) * noteVoice.envelope * (0.3f + 0.7f * noteVoice.velocity)
                                          : 0.0f;
                    const auto sample = noteVoice.state.fx.process (made) * noteGain;
                    outLeft += sample * juce::jmin (1.0f, 1.0f - noteVoice.pan);
                    outRight += sample * juce::jmin (1.0f, 1.0f + noteVoice.pan);
                    if (noteVoice.state.running && ! gate && noteVoice.envelope < 1.0e-4f)
                    {
                        // Faded out: stop, and clear what it had laid down ahead.
                        noteVoice.envelope = 0.0f;
                        noteVoice.state.running = false;
                        std::fill (noteVoice.state.output.begin(), noteVoice.state.output.end(), 0.0f);
                    }
                }

                if (stereo)
                {
                    left[i] = outLeft;
                    right[i] = outRight;
                }
                else
                {
                    left[i] = 0.5f * (outLeft + outRight);
                }
                ++now;
            }

            sourceGain = sourceTarget;
            for (size_t v = 0; v < noteVoices.size(); ++v)
            {
                const auto& noteVoice = noteVoices[v];
                const auto sounding = noteVoice.state.running && noteVoice.envelope > 0.01f;
                noteVoiceShown[v].note.store (sounding ? juce::roundToInt (noteVoice.glided) : -1);
                noteVoiceShown[v].pan.store (noteVoice.pan);
                noteVoiceShown[v].level.store (noteVoice.envelope);
            }
            for (size_t v = 0; v < voices.size(); ++v)
            {
                auto& state = voices[v];
                state.leftGain = targets[v].first;
                state.rightGain = targets[v].second;
                if (state.running && state.leftGain <= 0.0f && state.rightGain <= 0.0f)
                {
                    // Faded out: stop, and clear what it had laid down ahead
                    // (and its echo: a voice switched off is off completely).
                    state.running = false;
                    std::fill (state.output.begin(), state.output.end(), 0.0f);
                    state.fx.clear();
                }
            }
        };

        if (latency > 0)
            run (block, settings.on);
        else
            bypass.process (block, settings.on, [&] (juce::dsp::AudioBlock<float> wet) { run (wet, true); });
    }
}
