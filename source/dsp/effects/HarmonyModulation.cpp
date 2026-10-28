#include "HarmonyModulation.h"

namespace effects
{
    namespace
    {
        // A fixed random value (-1 to 1) for step `n`: the same every time,
        // so a random shape repeats when it's synced and doesn't crawl.
        float randomAt (std::uint32_t n, int seed) noexcept
        {
            auto x = n * 2654435761u + (std::uint32_t) seed * 40503u + 0x9e3779b9u;
            x ^= x >> 15;
            x *= 0x2c1b3c6du;
            x ^= x >> 12;
            x *= 0x297a2d39u;
            x ^= x >> 15;
            return (float) (x & 0xffffff) / (float) 0x7fffff - 1.0f;
        }

        float coefficient (float ms, double sampleRate) noexcept
        {
            return 1.0f - std::exp (-1.0f / juce::jmax (1.0e-3f, ms * 0.001f * (float) sampleRate));
        }
    }

    float HarmonyModulation::shapeValue (int shape, float p, std::uint32_t cycle, int seed) noexcept
    {
        p = juce::jlimit (0.0f, 1.0f, p);
        constexpr auto pi = juce::MathConstants<float>::pi;
        switch (shape)
        {
            case 1:  return p < 0.25f ? 4.0f * p : p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f;   // Triangle
            case 2:  return 2.0f * p - 1.0f;                                                         // Saw Up
            case 3:  return 1.0f - 2.0f * p;                                                         // Saw Down
            case 4:  return p < 0.5f ? 1.0f : -1.0f;                                                 // Square
            case 5:  return p < 0.25f ? 1.0f : -1.0f;                                                // Pulse (a quarter on)
            case 6:  return juce::jmin (3.0f, std::floor (p * 4.0f)) * (2.0f / 3.0f) - 1.0f;         // Stairs: four steps up
            case 7:  return randomAt (cycle * 4u + (std::uint32_t) juce::jmin (3.0f, std::floor (p * 4.0f)), seed);   // Random Steps: four a cycle
            case 8:                                                                                   // Smooth Random: glides between four a cycle
            {
                const auto place = p * 4.0f;
                const auto step = (std::uint32_t) juce::jmin (3.0f, std::floor (place));
                const auto t = 0.5f - 0.5f * std::cos (pi * (place - (float) step));
                const auto a = randomAt (cycle * 4u + step, seed), b = randomAt (cycle * 4u + step + 1u, seed);
                return a + (b - a) * t;
            }
            case 9:  return std::sin (pi * p);                                                       // Bump: 0 up to 1 and back (one way only)
            default: return std::sin (2.0f * pi * p);                                                // Sine
        }
    }

    float HarmonyModulation::modValue (const Mod& mod, float position, std::uint32_t cycle, int seed) noexcept
    {
        // WARP bends the time through the cycle, keeping its length.
        const auto warp = juce::jlimit (minWarp, maxWarp, mod.warp);
        const auto p = std::abs (warp - 1.0f) < 1.0e-4f ? position : std::pow (juce::jlimit (0.0f, 1.0f, position), 1.0f / warp);
        if (mod.type == 1)
        {
            const auto steps = juce::jlimit (2, maxSteps, mod.steps);
            const auto step = juce::jlimit (0, steps - 1, (int) std::floor (p * (float) steps));
            return juce::jlimit (-1.0f, 1.0f, mod.stepValues[(size_t) step] / pitchSpan);
        }
        if (mod.shape == drawnShape)
        {
            // The drawn points, joined by straight lines, round the cycle.
            const auto place = juce::jlimit (0.0f, 1.0f, p) * (float) drawnPoints;
            const auto a = juce::jlimit (0, drawnPoints - 1, (int) std::floor (place));
            const auto b = (a + 1) % drawnPoints;
            const auto t = place - (float) a;
            return juce::jlimit (-1.0f, 1.0f, mod.drawn[(size_t) a] + (mod.drawn[(size_t) b] - mod.drawn[(size_t) a]) * t);
        }
        return shapeValue (mod.shape, p, cycle, seed);
    }

    float HarmonyModulation::modAt (const Mod& mod, double position, int seed) const noexcept
    {
        // PLAY: where `position` (cycles since its start) puts it in its cycle; then PHASE.
        auto p = 0.0;
        std::uint32_t cycle = 0;
        switch (mod.play)
        {
            case oneShot: p = juce::jlimit (0.0, 0.999999, position); break;
            case seesaw:
            {
                const auto u = std::fmod (juce::jmax (0.0, position), 2.0);
                p = u < 1.0 ? u : 2.0 - u;
                cycle = (std::uint32_t) juce::jmax (0.0, position / 2.0);
                break;
            }
            default:
                p = position - std::floor (position);
                cycle = (std::uint32_t) juce::jmax (0.0, std::floor (position));
                break;
        }
        p += (double) juce::jlimit (0.0f, 1.0f, mod.phase);
        p -= std::floor (p);
        return modValue (mod, (float) p, cycle, seed);
    }

    namespace
    {
        // CURVE: 1 straight; below 1 slow then fast (exponential); above 1 fast then slow (logarithmic).
        float curved (float x, float curve) noexcept
        {
            x = juce::jlimit (0.0f, 1.0f, x);
            const auto c = juce::jlimit (0.1f, 10.0f, curve);
            return std::abs (c - 1.0f) < 1.0e-4f ? x : std::pow (x, 1.0f / c);
        }
    }

    float HarmonyModulation::envelopeHeld (const Envelope& envelope, double seconds) noexcept
    {
        const auto attack = juce::jmax (1.0e-4, (double) envelope.attackMs * 0.001), decay = juce::jmax (1.0e-4, (double) envelope.decayMs * 0.001);
        const auto sustain = juce::jlimit (0.0f, 1.0f, envelope.sustain);
        if (seconds < attack)
            return curved ((float) (seconds / attack), envelope.attackCurve);
        if (seconds < attack + decay)
            return 1.0f - (1.0f - sustain) * curved ((float) ((seconds - attack) / decay), envelope.decayCurve);
        return sustain;
    }

    float HarmonyModulation::envelopeReleased (const Envelope& envelope, float from, double releaseSeconds) noexcept
    {
        const auto release = juce::jmax (1.0e-4, (double) envelope.releaseMs * 0.001);
        return releaseSeconds >= release ? 0.0f : from * (1.0f - curved ((float) (releaseSeconds / release), envelope.releaseCurve));
    }

    void HarmonyModulation::runEnvelope (const Envelope& envelope, EnvelopeState& state, bool start, bool held, double seconds) noexcept
    {
        if (start)
        {
            state = { true, 0.0, 0.0f, 0.0f };
        }
        else if (state.held && ! held)
        {
            state.releasedFrom = state.level;
            state.held = false;
            state.time = 0.0;
        }
        state.level = state.held ? envelopeHeld (envelope, state.time) : envelopeReleased (envelope, state.releasedFrom, state.time);
        state.time += seconds;
    }

    bool HarmonyModulation::isPerNote (int source, const Settings& settings) const noexcept
    {
        if (source >= m1 && source <= m4)
            return settings.mods[(size_t) (source - m1)].trigger == poly;
        return source == a1 || source == a2 || source == sp || source == velocity || source == keyboard;
    }

    float HarmonyModulation::getVoiceValue (int source, int voice) const noexcept
    {
        if (! juce::isPositiveAndBelow (source, (int) numSources) || ! juce::isPositiveAndBelow (voice, numNoteVoices))
            return getValue (source);
        return voiceValues[(size_t) source][(size_t) voice];
    }

    double HarmonyModulation::unitSeconds (const Mod& mod, double bpm) noexcept
    {
        const auto syncs = (int) std::size (syncBeats);
        if (mod.sync > 0 && mod.sync < syncs)
            return (double) syncBeats[mod.sync] * 60.0 / juce::jmax (20.0, bpm);
        return 1.0 / (double) juce::jlimit (minRateHz, maxRateHz, mod.rateHz);
    }

    void HarmonyModulation::prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        reset();
    }

    void HarmonyModulation::reset()
    {
        modStates = {};
        voiceValues = {};
        voiceEnvelopes = {};
        envelopes = {};
        lastNotes.fill (-1);
        spreadValues.fill (0.0f);
        newestVoice = -1;
        amEnvelope = 0.0f;
        ptSmoothed = 0.0f;
        ptStarted = false;
        values.fill (0.0f);
        for (auto& shown : shownValues)
            shown = 0.0f;
        for (auto& shown : shownPositions)
            shown = 0.0f;
        for (auto& source : shownVoiceValues)
            for (auto& shown : source)
                shown = 0.0f;
    }

    void HarmonyModulation::advance (const Settings& settings, const Context& context, const float* voice, int numSamples) noexcept
    {
        const auto seconds = (double) numSamples / sampleRate;

        // The note voices: which began a note now, which let theirs go.
        std::array<bool, numNoteVoices> started {}, sounding {};
        auto anyStarted = false, anyHeld = false;
        for (int v = 0; v < numNoteVoices; ++v)
        {
            const auto note = context.noteVoices[(size_t) v].note;
            started[(size_t) v] = note >= 0 && note != lastNotes[(size_t) v];
            sounding[(size_t) v] = note >= 0;
            anyStarted = anyStarted || started[(size_t) v];
            anyHeld = anyHeld || note >= 0;
            if (started[(size_t) v])
                newestVoice = v;
            lastNotes[(size_t) v] = note;
        }

        // M1-M4.
        for (int m = 0; m < numMods; ++m)
        {
            const auto& mod = settings.mods[(size_t) m];
            auto& state = modStates[(size_t) m];
            const auto unit = unitSeconds (mod, context.bpm);
            const auto cycleSeconds = mod.type == 1 ? unit * juce::jlimit (2, maxSteps, mod.steps) : unit;

            // TRIGGER: where the cycle restarts (Poly: each note voice's own, from its start).
            if (mod.trigger == sync && context.songBeats >= 0.0)
                state.position = context.songBeats / (cycleSeconds * context.bpm / 60.0);   // locked to the song
            else if (context.newNote && (mod.trigger == retrigger || (mod.trigger == legato && ! context.heldBefore)))
                state.position = 0.0;
            for (int v = 0; v < numNoteVoices; ++v)
                if (started[(size_t) v])
                    state.voiceStart[(size_t) v] = state.position;
            const auto levelNow = juce::jlimit (0.0f, 1.0f, mod.level);
            for (int v = 0; v < numNoteVoices; ++v)
                voiceValues[(size_t) (m1 + m)][(size_t) v] = modAt (mod, state.position - state.voiceStart[(size_t) v], m + 1) * levelNow;

            // One value for everything else: Poly's the newest note's cycle.
            const auto position = mod.trigger == poly && newestVoice >= 0 ? state.position - state.voiceStart[(size_t) newestVoice] : state.position;
            auto p = position;
            if (mod.play == oneShot)
                p = juce::jlimit (0.0, 0.999999, p);
            else if (mod.play == seesaw)
            {
                const auto u = std::fmod (juce::jmax (0.0, p), 2.0);
                p = u < 1.0 ? u : 2.0 - u;
            }
            else
                p -= std::floor (p);
            p += (double) juce::jlimit (0.0f, 1.0f, mod.phase);
            p -= std::floor (p);

            const auto raw = modAt (mod, position, m + 1);
            if (! state.started)
            {
                state.smoothed = raw;
                state.started = true;
            }
            else
            {
                const auto alpha = 1.0f - std::exp (-(float) numSamples / juce::jmax (1.0e-3f, mod.smoothMs * 0.001f * (float) sampleRate));
                state.smoothed += (raw - state.smoothed) * alpha;
            }
            values[(size_t) (m1 + m)] = state.smoothed * levelNow;
            shownPositions[(size_t) m] = (float) p;

            if (mod.play != hold)
                state.position += seconds / cycleSeconds;
        }

        // AM: how loud the voice is, 0 (-60 dB and below) to 1 (full scale), times LEVEL.
        {
            const auto attack = coefficient (settings.am.attackMs, sampleRate), release = coefficient (settings.am.releaseMs, sampleRate);
            for (int i = 0; i < numSamples; ++i)
            {
                const auto x = voice != nullptr ? std::abs (voice[i]) : 0.0f;
                amEnvelope += (x - amEnvelope) * (x > amEnvelope ? attack : release);
            }
            const auto db = juce::Decibels::gainToDecibels (amEnvelope, -60.0f);
            values[am] = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f) * juce::jlimit (0.0f, 2.0f, settings.am.level);
        }

        // PT: the pitch sung, 0 at middle C (plus OFFSET), 1 two octaves up; held between notes.
        {
            const auto& pitch = settings.pt;
            auto target = ptStarted ? ptSmoothed : juce::jlimit (-1.0f, 1.0f, pitch.offset);
            if (context.sungNote >= 0.0f)
            {
                target = juce::jlimit (-1.0f, 1.0f, (context.sungNote - 60.0f) / pitchSpan + pitch.offset);
                ptStarted = true;
            }
            const auto alpha = 1.0f - std::exp (-(float) numSamples / juce::jmax (1.0e-3f, pitch.smoothMs * 0.001f * (float) sampleRate));
            ptSmoothed += (target - ptSmoothed) * alpha;
            values[pt] = ptSmoothed * juce::jlimit (0.0f, 2.0f, pitch.level);
        }

        // A1-A2: one envelope per note voice, and one following the notes
        // as a whole (restarted by each new note, let go when none is held).
        for (int e = 0; e < numEnvelopes; ++e)
        {
            const auto& envelope = settings.envelopes[(size_t) e];
            for (int v = 0; v < numNoteVoices; ++v)
            {
                auto& state = voiceEnvelopes[(size_t) e][(size_t) v];
                runEnvelope (envelope, state, started[(size_t) v], sounding[(size_t) v], seconds);
                voiceValues[(size_t) (a1 + e)][(size_t) v] = state.level;
            }
            runEnvelope (envelope, envelopes[(size_t) e], anyStarted, anyHeld, seconds);
            values[(size_t) (a1 + e)] = envelopes[(size_t) e].level;
        }

        // SP: each note its own value, by its place among the notes sounding.
        {
            const auto& spreader = settings.spreader;
            const auto spread = juce::jlimit (0.0f, 1.0f, spreader.spread);
            const auto valueFor = [&] (int thisVoice)
            {
                if (spreader.mode == 2)
                    return (random.nextFloat() * 2.0f - 1.0f) * spread;
                auto count = 0, below = 0;
                for (int v = 0; v < numNoteVoices; ++v)
                    if (sounding[(size_t) v])
                    {
                        ++count;
                        below += context.noteVoices[(size_t) v].note < context.noteVoices[(size_t) thisVoice].note ? 1 : 0;
                    }
                const auto place = count > 1 ? -1.0f + 2.0f * (float) below / (float) (count - 1) : 0.0f;
                return (spreader.mode == 1 ? -place : place) * spread;
            };
            for (int v = 0; v < numNoteVoices; ++v)
                if (sounding[(size_t) v] && (started[(size_t) v] || (anyStarted && spreader.trigger == 0)))
                    spreadValues[(size_t) v] = valueFor (v);
            const auto level = juce::jlimit (0.0f, 1.0f, spreader.level);
            for (int v = 0; v < numNoteVoices; ++v)
                voiceValues[sp][(size_t) v] = spreadValues[(size_t) v] * level;
            values[sp] = newestVoice >= 0 && sounding[(size_t) newestVoice] ? spreadValues[(size_t) newestVoice] * level : 0.0f;
        }

        // MIDI: the last key's velocity and place, the wheels, aftertouch.
        values[velocity] = context.midi.velocity;
        values[keyboard] = context.midi.keyboard;
        values[pitchWheel] = context.midi.pitchWheel;
        values[modWheel] = context.midi.modWheel;
        values[aftertouch] = context.midi.aftertouch;
        for (int v = 0; v < numNoteVoices; ++v)
        {
            const auto& noteVoice = context.noteVoices[(size_t) v];
            voiceValues[velocity][(size_t) v] = noteVoice.note >= 0 ? noteVoice.velocity : 0.0f;
            voiceValues[keyboard][(size_t) v] = noteVoice.note >= 0 ? juce::jlimit (-1.0f, 1.0f, ((float) noteVoice.note - 60.0f) / pitchSpan) : 0.0f;
        }

        // The sources that don't go note by note: the same for every note voice.
        for (int source = 1; source < numSources; ++source)
            if (! isPerNote (source, settings))
                voiceValues[(size_t) source].fill (values[(size_t) source]);

        for (size_t s = 0; s < values.size(); ++s)
        {
            shownValues[s] = values[s];
            for (size_t v = 0; v < (size_t) numNoteVoices; ++v)
                shownVoiceValues[s][v] = voiceValues[s][v];
        }
    }
}
