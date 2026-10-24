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
        }
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

        for (size_t v = 0; v < voices.size(); ++v)
        {
            const auto& setting = settings.voices[v];
            auto shift = 0.0;   // semitones from the sung pitch
            if (voiced)
            {
                if (setting.correct)
                {
                    // The note the singer is aiming at, then the harmony
                    // note in the key, sung exactly in tune.
                    const auto aimedAt = Scales::nearest (sung, settings.key, settings.scale);
                    const auto target = Scales::harmonyNote (aimedAt, juce::roundToInt (setting.semitones), settings.key, settings.scale);
                    shift = (double) target + setting.cents / 100.0 - sung;
                }
                else
                {
                    // Uncorrected: the interval exactly, following the singer's own tuning.
                    shift = setting.semitones + setting.cents / 100.0;
                }
            }
            auto& voice = voices[v];
            voice.ratio = (float) juce::jlimit (0.25, 4.0, std::pow (2.0, shift / 12.0));
            // CORRECT FORMANT at 1 keeps the voice's resonances where they
            // are; at 0 they move with the pitch, as a sped-up tape's do.
            const auto formantSemitones = setting.formant + (1.0f - settings.correctFormant) * (float) shift;
            voice.formantRatio = juce::jlimit (0.5f, 2.0f, std::pow (2.0f, formantSemitones / 12.0f));
        }
    }

    void Harmony::addSlice (VoiceState& voice, double centre) noexcept
    {
        // One slice: two pulses of the voice, faded in and out (a Hann
        // window), centred on the analysis mark nearest where it's going.
        const auto half = juce::jmax (8, juce::roundToInt (period));
        const auto read = (double) voice.formantRatio;
        const auto delay = (double) half * (1.0 + juce::jmax (1.0, read)) + 4.0;
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
        if (settings.on && bypass.isSilent())
        {
            // Coming back on: start the voices afresh rather than from where
            // they were when it went off.
            for (auto& voice : voices)
            {
                std::fill (voice.output.begin(), voice.output.end(), 0.0f);
                voice.running = false;
                voice.leftGain = voice.rightGain = 0.0f;
            }
        }

        bypass.process (block, settings.on, [&] (juce::dsp::AudioBlock<float> wet)
        {
            const auto numSamples = (int) wet.getNumSamples();
            const auto stereo = wet.getNumChannels() > 1;
            auto* left = wet.getChannelPointer (0);
            auto* right = stereo ? wet.getChannelPointer (1) : nullptr;

            // Where each voice's gains are headed this block; they glide
            // there across it, so moving a level or pan doesn't click.
            const auto harmonyGain = gainFor (settings.harmonyDb);
            std::array<std::pair<float, float>, numVoices> targets {};
            for (size_t v = 0; v < voices.size(); ++v)
            {
                const auto& setting = settings.voices[v];
                if (! setting.active || ! setting.on)
                    continue;
                const auto pan = juce::jlimit (-1.0f, 1.0f, setting.pan * settings.spread);
                const auto gain = gainFor (setting.levelDb) * harmonyGain;
                // Centred, a voice is at full level on both sides, as the
                // singer's own voice is; panned, it fades from the far side.
                targets[v] = { gain * juce::jmin (1.0f, 1.0f - pan), gain * juce::jmin (1.0f, 1.0f + pan) };
            }
            const auto sourceTarget = gainFor (settings.sourceDb);

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
                const auto source = (sourceGain + (sourceTarget - sourceGain) * through) * voice;
                auto outLeft = source, outRight = source;

                for (size_t v = 0; v < voices.size(); ++v)
                {
                    auto& state = voices[v];
                    const auto [targetLeft, targetRight] = targets[v];
                    const auto sounding = targetLeft > 0.0f || targetRight > 0.0f || state.leftGain > 0.0f || state.rightGain > 0.0f;
                    if (! state.running)
                    {
                        if (! sounding)
                            continue;
                        state.running = true;
                        state.nextSlice = (double) now;
                    }

                    // Lay down every slice whose source audio has arrived.
                    const auto half = juce::jmax (8.0, period);
                    const auto read = (double) state.formantRatio;
                    const auto delay = half * (1.0 + juce::jmax (1.0, read)) + 4.0;
                    if (state.nextSlice < (double) now + half)
                        state.nextSlice = (double) now + delay - half * read;   // fell behind (just started)
                    while (state.nextSlice <= (double) now + delay - half * read)
                    {
                        addSlice (state, state.nextSlice);
                        state.nextSlice += half / (double) state.ratio;
                    }

                    auto& slot = state.output[(size_t) (now & mask)];
                    const auto sample = slot;
                    slot = 0.0f;
                    outLeft += (state.leftGain + (targetLeft - state.leftGain) * through) * sample;
                    outRight += (state.rightGain + (targetRight - state.rightGain) * through) * sample;
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
            for (size_t v = 0; v < voices.size(); ++v)
            {
                auto& state = voices[v];
                state.leftGain = targets[v].first;
                state.rightGain = targets[v].second;
                if (state.running && state.leftGain <= 0.0f && state.rightGain <= 0.0f)
                {
                    // Faded out: stop, and clear what it had laid down ahead.
                    state.running = false;
                    std::fill (state.output.begin(), state.output.end(), 0.0f);
                }
            }
        });
    }
}
