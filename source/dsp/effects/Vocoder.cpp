#include "Vocoder.h"

namespace effects
{
    namespace
    {
        float coefficient (float ms, double sampleRate) noexcept
        {
            return 1.0f - std::exp (-1.0f / juce::jmax (1.0e-3f, ms * 0.001f * (float) sampleRate));
        }

        // From `a` to `b` (both above 0) evenly in ratio, as `t` goes 0 to 1.
        float between (float a, float b, float t) noexcept { return a * std::pow (b / a, juce::jlimit (0.0f, 1.0f, t)); }

        float gainOf (float db) noexcept { return db <= Vocoder::offDb ? 0.0f : juce::Decibels::decibelsToGain (db); }

        // The band-limited step correction (polyBLEP) for a jump at phase 0.
        float blep (float t, float dt) noexcept
        {
            if (t < dt)
            {
                t /= dt;
                return t + t - t * t - 1.0f;
            }
            if (t > 1.0f - dt)
            {
                t = (t - 1.0f) / dt;
                return t * t + t + t + 1.0f;
            }
            return 0.0f;
        }

        float wrap (float phase) noexcept { return phase - std::floor (phase); }

        // SAWS: seven saws, spread evenly around the note.
        constexpr float sawsSpread[] = { -1.0f, -0.666f, -0.333f, 0.0f, 0.333f, 0.666f, 1.0f };

        // The synth's band followers: steady (they only level the synth).
        constexpr float synthAttackMs = 2.0f, synthReleaseMs = 40.0f;
        // A synth voice's envelope (our choice: quick, so it plays as the keys do).
        constexpr float voiceAttackMs = 5.0f, voiceReleaseMs = 60.0f;
        // Auto's voices slide to a new pitch over this (so a sung slide is smooth).
        constexpr float autoGlideMs = 15.0f;
        // How long Auto keeps a note when the reading stops (a consonant, a breath).
        constexpr double holdSeconds = 0.04;
    }

    Vocoder::Vocoder()
    {
        for (auto& shown : shownBands)
            shown = 0.0f;
        for (auto& shown : shownNotes)
            shown = -1.0f;
    }

    float Vocoder::bandHz (int band, int count) noexcept
    {
        return lowestHz * std::pow (highestHz / lowestHz, bandPlace (band, count));
    }

    float Vocoder::curveAt (const std::array<float, curvePoints>& curve, float place) noexcept
    {
        const auto at = juce::jlimit (0.0f, 1.0f, place) * (float) (curvePoints - 1);
        const auto a = juce::jlimit (0, curvePoints - 1, (int) std::floor (at));
        const auto b = juce::jmin (curvePoints - 1, a + 1);
        const auto t = at - (float) a;
        return curve[(size_t) a] + (curve[(size_t) b] - curve[(size_t) a]) * t;
    }

    void Vocoder::BandPass::set (float hz, float q, double rate) noexcept
    {
        g = (float) std::tan (juce::MathConstants<double>::pi * juce::jlimit (10.0, 0.45 * rate, (double) hz) / rate);
        k = 1.0f / juce::jmax (0.1f, q);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    float Vocoder::BandPass::process (float x) noexcept
    {
        const auto v3 = x - s2;
        const auto v1 = a1 * s1 + a2 * v3;
        const auto v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2.0f * v1 - s1;
        s2 = 2.0f * v2 - s2;
        return k * v1;   // unity at the centre
    }

    void Vocoder::prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        bypass.prepare (sampleRate, maxBlockSize);
        left.assign ((size_t) maxBlockSize, 0.0f);
        right.assign ((size_t) maxBlockSize, 0.0f);
        dry.reset (sampleRate, 0.02);
        level.reset (sampleRate, 0.02);
        const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 2 };
        lowCut.prepare (spec);
        lowCut.setType (juce::dsp::StateVariableTPTFilterType::highpass);
        highCut.prepare (spec);
        highCut.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        detector.prepare (sampleRate);
        ring.assign ((size_t) ringSize, 0.0f);
        window.assign ((size_t) detector.windowSizeAtHostRate(), 0.0f);
        reset();
    }

    void Vocoder::reset()
    {
        for (auto& band : bands)
        {
            for (auto& filter : band.voice)
                filter.clear();
            for (auto& filter : band.synth)
                filter.clear();
            band.voiceLevel = band.synthLevel = 0.0f;
        }
        bandsShift = 1000.0f;   // set the bands again
        for (auto& voice : voices)
            voice = SynthVoice {};
        keyVelocity.fill (0.0f);
        keyOrder.fill (0);
        std::fill (ring.begin(), ring.end(), 0.0f);
        ringWrite = sinceRead = 0;
        autoNote = (float) defaultNote;
        sungNote = -1.0f;
        lfoPhase = 0.0;
        lfoHeld = 0.0f;
        noiseHeld = noiseCount = 0.0f;
        ringWrite = sinceRead = heldFor = 0;
        lowCut.reset();
        highCut.reset();
        dry.setCurrentAndTargetValue (dry.getTargetValue());
        level.setCurrentAndTargetValue (level.getTargetValue());
    }

    void Vocoder::handleMidi (const juce::MidiBuffer& midi) noexcept
    {
        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();
            if (message.isNoteOn())
            {
                keyVelocity[(size_t) message.getNoteNumber()] = juce::jmax (0.01f, message.getFloatVelocity());
                keyOrder[(size_t) message.getNoteNumber()] = ++keyClock;
            }
            else if (message.isNoteOff())
                keyVelocity[(size_t) message.getNoteNumber()] = 0.0f;
            else if (message.isAllNotesOff() || message.isAllSoundOff())
                keyVelocity.fill (0.0f);
        }
    }

    int Vocoder::diatonic (int note, int steps, const Settings& settings) const noexcept
    {
        if (steps == 0)
            return note;
        if (settings.scaleType == Scales::Scale::chromatic)
        {
            // No scale to step through: a major scale's steps.
            constexpr int major[] = { 0, 2, 4, 5, 7, 9, 11, 12 };
            const auto size = juce::jlimit (0, 7, std::abs (steps));
            return note + (steps > 0 ? major[size] : -major[size]);
        }
        const auto direction = steps > 0 ? 1 : -1;
        auto found = 0;
        auto candidate = note;
        for (int guard = 0; guard < 36 && found < std::abs (steps); ++guard)
        {
            candidate += direction;
            if (Scales::isIn (candidate, settings.key, settings.scaleType, settings.custom))
                ++found;
        }
        return candidate;
    }

    void Vocoder::readPitch (const float* clean, int numSamples) noexcept
    {
        constexpr int mask = ringSize - 1;
        const auto length = (int) window.size();
        for (int i = 0; i < numSamples; ++i)
        {
            ring[(size_t) ringWrite] = clean != nullptr ? clean[i] : 0.0f;
            ringWrite = (ringWrite + 1) & mask;
            if (++sinceRead < hop)
                continue;
            sinceRead = 0;
            for (int s = 0; s < length; ++s)
                window[(size_t) s] = ring[(size_t) ((ringWrite - length + s) & mask)];
            if (const auto result = detector.detect (window.data(), length))
            {
                autoNote = (float) Scales::midiFromHz (result->frequency);
                sungNote = autoNote;
                noiseCount = 0.0f;
                heldFor = 0;
            }
            else if ((heldFor += hop) > (int) (holdSeconds * sampleRate))
                sungNote = -1.0f;   // the synth keeps the last note
        }
    }

    void Vocoder::setVoices (const Settings& settings) noexcept
    {
        if (settings.carrier == Carrier::autoPitch)
        {
            // Your pitch exactly, and the INTERVAL voices from the scale note nearest it.
            auto& own = voices[0];
            own.note = 0;
            own.gate = true;
            own.targetNote = autoNote;
            own.level = 1.0f;
            const auto base = Scales::nearest (autoNote, settings.key, settings.scaleType, settings.custom);
            for (int i = 0; i < numIntervals; ++i)
            {
                auto& voice = voices[(size_t) (i + 1)];
                const auto choice = juce::jlimit (0, intervalNames.size() - 1, settings.intervals[(size_t) i]);
                voice.gate = choice > 0;
                voice.note = choice > 0 ? i + 1 : -1;
                voice.targetNote = (float) diatonic (base, intervalSteps[choice], settings);
                voice.level = gainOf (settings.intervalDb);
                if (voice.gain <= 0.0f)
                    voice.currentNote = voice.targetNote;
            }
            for (int v = numIntervals + 1; v < maxVoices; ++v)
                voices[(size_t) v].gate = false;
            return;
        }

        if (settings.voicing == Voicing::mono)
        {
            // The last key pressed of those held.
            auto newest = -1;
            for (int key = 0; key < 128; ++key)
                if (keyVelocity[(size_t) key] > 0.0f && (newest < 0 || keyOrder[(size_t) key] > keyOrder[(size_t) newest]))
                    newest = key;
            auto& voice = voices[0];
            voice.gate = newest >= 0;
            if (newest >= 0)
            {
                if (voice.gain <= 1.0e-4f)
                    voice.currentNote = (float) newest;   // from silence: straight there
                voice.note = newest;
                voice.targetNote = (float) newest;
                voice.level = 0.3f + 0.7f * keyVelocity[(size_t) newest];
            }
            for (int v = 1; v < maxVoices; ++v)
                voices[(size_t) v].gate = false;
            return;
        }

        // POLY: each held key a voice (a new key takes a silent voice, or the oldest).
        for (auto& voice : voices)
            voice.gate = voice.note >= 0 && voice.note < 128 && keyVelocity[(size_t) voice.note] > 0.0f && voice.gate;
        for (int key = 0; key < 128; ++key)
        {
            if (keyVelocity[(size_t) key] <= 0.0f)
                continue;
            auto playing = false;
            for (const auto& voice : voices)
                playing = playing || (voice.gate && voice.note == key);
            if (playing)
                continue;
            SynthVoice* chosen = nullptr;
            for (auto& voice : voices)
                if (! voice.gate && (chosen == nullptr || voice.gain < chosen->gain))
                    chosen = &voice;
            if (chosen == nullptr)
                for (auto& voice : voices)
                    if (chosen == nullptr || voice.age < chosen->age)
                        chosen = &voice;
            chosen->note = key;
            chosen->gate = true;
            chosen->age = ++clock;
            chosen->targetNote = chosen->currentNote = (float) key;
            chosen->level = 0.3f + 0.7f * keyVelocity[(size_t) key];
        }
    }

    void Vocoder::setBands (const Settings& settings) noexcept
    {
        const auto mode = (Mode) juce::jlimit (0, 2, (int) settings.mode);
        const auto count = bandCounts[(int) mode];
        if (mode != bandsMode || ! juce::approximatelyEqual (settings.shift, bandsShift))
        {
            const auto ratio = std::pow (highestHz / lowestHz, 1.0f / (float) (count - 1));
            const auto q = 0.9f * std::sqrt (ratio) / (ratio - 1.0f);
            const auto shiftBy = std::pow (2.0f, juce::jlimit (-12.0f, 12.0f, settings.shift) / 12.0f);
            for (int b = 0; b < count; ++b)
            {
                auto& band = bands[(size_t) b];
                const auto hz = bandHz (b, count);
                for (auto& filter : band.voice)
                    filter.set (hz, q, sampleRate);
                for (auto& filter : band.synth)
                    filter.set (hz * shiftBy, q, sampleRate);
                if (mode != bandsMode)
                {
                    for (auto& filter : band.voice)
                        filter.clear();
                    for (auto& filter : band.synth)
                        filter.clear();
                    band.voiceLevel = band.synthLevel = 0.0f;
                }
            }
            bandsMode = mode;
            bandsShift = settings.shift;
            numBands = count;
        }
        for (int b = 0; b < numBands; ++b)
        {
            auto& band = bands[(size_t) b];
            const auto place = bandPlace (b, numBands);
            const auto db = curveAt (settings.bandDb, place);
            const auto gain = db <= minBandDb ? 0.0f : juce::Decibels::decibelsToGain (db);
            const auto pan = juce::jlimit (-1.0f, 1.0f, curveAt (settings.bandPan, place));
            band.gainLeft = gain * juce::jmin (1.0f, 1.0f - pan);
            band.gainRight = gain * juce::jmin (1.0f, 1.0f + pan);
        }
    }

    float Vocoder::nextLfo (const Settings& settings) noexcept
    {
        lfoPhase += juce::jlimit (0.01, 40.0, (double) settings.lfoRateHz) / sampleRate;
        if (lfoPhase >= 1.0)
        {
            lfoPhase -= std::floor (lfoPhase);
            lfoHeld = random.nextFloat() * 2.0f - 1.0f;
        }
        const auto p = (float) lfoPhase;
        switch (settings.lfoShape)
        {
            case triangleLfo: return p < 0.25f ? 4.0f * p : p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f;
            case sawLfo:      return 2.0f * p - 1.0f;
            case squareLfo:   return p < 0.5f ? 1.0f : -1.0f;
            case randomLfo:   return lfoHeld;
            default:          return std::sin (juce::MathConstants<float>::twoPi * p);
        }
    }

    float Vocoder::waveSample (int wave, float phase, float increment, float shape) noexcept
    {
        const auto dt = juce::jlimit (1.0e-6f, 0.45f, increment);
        shape = juce::jlimit (0.0f, 1.0f, shape);
        switch (wave)
        {
            case square:
            {
                // SHAPE narrows the pulse: half the cycle down to a twentieth.
                const auto width = 0.5f - 0.45f * shape;
                auto value = phase < width ? 1.0f : -1.0f;
                value += blep (phase, dt) - blep (wrap (phase - width + 1.0f), dt);
                return value;
            }
            case triangle:
            {
                // SHAPE moves the peak from the middle toward the end: a saw.
                const auto peak = 0.5f + 0.49f * shape;
                return phase < peak ? -1.0f + 2.0f * phase / peak : 1.0f - 2.0f * (phase - peak) / (1.0f - peak);
            }
            default:
            {
                // SAW: SHAPE shortens the rise, which then waits at the top (a pulse-width saw).
                const auto rise = 1.0f - 0.9f * shape;
                auto value = phase < rise ? -1.0f + 2.0f * phase / rise : 1.0f;
                value -= blep (phase, dt);
                return value;
            }
        }
    }

    float Vocoder::renderSynth (const Settings& settings, float lfo, float glide, float attack, float release) noexcept
    {
        auto total = 0.0f, loudest = 0.0f;
        auto firstNote = -1.0f;
        std::array<float, 2> oscGains { settings.osc[0].on ? gainOf (settings.osc[0].gainDb) : 0.0f,
                                        settings.osc[1].on ? gainOf (settings.osc[1].gainDb) : 0.0f };
        for (auto& voice : voices)
        {
            voice.gain += ((voice.gate ? 1.0f : 0.0f) - voice.gain) * (voice.gate ? attack : release);
            if (voice.gain < 1.0e-5f && ! voice.gate)
            {
                voice.gain = 0.0f;
                continue;
            }
            voice.currentNote += (voice.targetNote - voice.currentNote) * glide;
            if (firstNote < 0.0f)
                firstNote = voice.currentNote;
            loudest = juce::jmax (loudest, voice.gain);
            auto sum = 0.0f;
            for (size_t o = 0; o < 2; ++o)
            {
                if (oscGains[o] <= 0.0f)
                    continue;
                const auto& osc = settings.osc[o];
                const auto note = voice.currentNote + osc.coarse + osc.fineCents * 0.01f + osc.pitchMod * lfo * pitchModSemitones;
                const auto increment = juce::jlimit (1.0e-6f, 0.45f, (float) (440.0 * std::exp2 ((note - 69.0f) / 12.0f) / sampleRate));
                const auto shape = juce::jlimit (0.0f, 1.0f, osc.shape + osc.shapeMod * lfo * 0.5f);
                auto& phases = voice.phases[o];
                auto value = 0.0f;
                if (osc.wave == saws)
                {
                    // Seven saws, up to 40 cents apart either side (SHAPE).
                    const auto cents = 4.0f + 36.0f * shape;
                    for (size_t s = 0; s < 7; ++s)
                    {
                        const auto dt = increment * std::exp2 (sawsSpread[s] * cents / 1200.0f);
                        value += 2.0f * phases[s] - 1.0f - blep (phases[s], dt);
                        phases[s] = wrap (phases[s] + dt);
                    }
                    value *= 0.38f;
                }
                else if (osc.wave == syncSaw)
                {
                    // A second saw, 1 to 5 times as fast (SHAPE), restarted with the first.
                    const auto ratio = 1.0f + 4.0f * shape;
                    auto& slave = voice.syncPhase[o];
                    value = 2.0f * slave - 1.0f - blep (slave, juce::jmin (0.45f, increment * ratio));
                    slave += increment * ratio;
                    if (slave >= 1.0f)
                        slave -= 1.0f;
                    phases[0] += increment;
                    if (phases[0] >= 1.0f)
                    {
                        phases[0] -= 1.0f;
                        slave = phases[0] * ratio;
                    }
                }
                else
                {
                    value = waveSample (osc.wave, phases[0], increment, shape);
                    phases[0] = wrap (phases[0] + increment);
                }
                sum += value * oscGains[o];
            }
            total += sum * voice.gain * voice.level;
        }

        // NOISE: held for a period of the note at CRUSH 0 (a buzz at its
        // pitch), for a sample at 1 (white).
        if (settings.noise.on && loudest > 0.0f)
        {
            const auto pitchValue = firstNote >= 0.0f ? (firstNote - 60.0f) / 24.0f : 0.0f;
            const auto crush = juce::jlimit (0.0f, 1.0f, settings.noise.crush + settings.noise.crushMod * (settings.noise.modSource == 0 ? lfo : pitchValue));
            const auto period = (float) (sampleRate / (440.0 * std::exp2 (((firstNote >= 0.0f ? firstNote : 48.0f) - 69.0f) / 12.0f)));
            const auto hold = juce::jmax (1.0f, std::pow (juce::jmax (1.0f, period), 1.0f - crush));
            noiseCount += 1.0f;
            if (noiseCount >= hold)
            {
                noiseCount -= hold;
                noiseHeld = random.nextFloat() * 2.0f - 1.0f;
            }
            total += noiseHeld * gainOf (settings.noise.gainDb) * loudest;
        }
        return total * 0.5f;
    }

    void Vocoder::process (juce::dsp::AudioBlock<float> block, const float* modulator, const float* clean, const Settings& settings) noexcept
    {
        dry.setTargetValue (gainOf (settings.dryDb));
        level.setTargetValue (gainOf (settings.levelDb));
        bypass.process (block, settings.on, [&] (juce::dsp::AudioBlock<float> wet)
        {
            const auto numSamples = juce::jmin ((int) wet.getNumSamples(), (int) left.size());
            if (settings.carrier == Carrier::autoPitch)
                readPitch (clean, numSamples);
            setBands (settings);
            setVoices (settings);

            // SCALE: the voice's band followers, slow and soft to fast and crisp.
            const auto bandAttack = coefficient (between (20.0f, 1.0f, settings.scale), sampleRate);
            const auto bandRelease = coefficient (between (150.0f, 10.0f, settings.scale), sampleRate);
            const auto synthAttack = coefficient (synthAttackMs, sampleRate), synthRelease = coefficient (synthReleaseMs, sampleRate);
            const auto voiceAttack = coefficient (voiceAttackMs, sampleRate), voiceRelease = coefficient (voiceReleaseMs, sampleRate);
            const auto glide = settings.carrier == Carrier::autoPitch
                                   ? coefficient (autoGlideMs, sampleRate)
                                   : settings.voicing == Voicing::mono ? coefficient (juce::jmax (0.5f, settings.glideMs / 3.0f), sampleRate) : 1.0f;
            // Measured: level with the voice (a sung vowel) at each MODE.
            constexpr float modeGains[] = { 2.17f, 2.0f, 1.98f };
            const auto bandsGain = modeGains[(int) bandsMode];

            for (int i = 0; i < numSamples; ++i)
            {
                const auto lfo = nextLfo (settings);
                const auto synth = renderSynth (settings, lfo, glide, voiceAttack, voiceRelease);
                const auto voice = modulator != nullptr ? modulator[i] : 0.0f;
                auto outLeft = 0.0f, outRight = 0.0f;
                for (int b = 0; b < numBands; ++b)
                {
                    auto& band = bands[(size_t) b];
                    const auto heard = band.voice[1].process (band.voice[0].process (voice));
                    const auto heardLevel = std::abs (heard);
                    band.voiceLevel += (heardLevel - band.voiceLevel) * (heardLevel > band.voiceLevel ? bandAttack : bandRelease);
                    const auto played = band.synth[1].process (band.synth[0].process (synth));
                    const auto playedLevel = std::abs (played);
                    band.synthLevel += (playedLevel - band.synthLevel) * (playedLevel > band.synthLevel ? synthAttack : synthRelease);
                    // The synth's band levelled, then given the voice's level.
                    const auto gain = juce::jmin (maxBandBoost, band.voiceLevel / juce::jmax (1.0e-5f, band.synthLevel));
                    const auto out = played * gain;
                    outLeft += out * band.gainLeft;
                    outRight += out * band.gainRight;
                }
                left[(size_t) i] = outLeft * bandsGain;
                right[(size_t) i] = outRight * bandsGain;
            }

            // MIX: PAN, then the low and high cuts.
            const auto panLeft = juce::jmin (1.0f, 1.0f - settings.pan), panRight = juce::jmin (1.0f, 1.0f + settings.pan);
            const auto useLowCut = settings.lowCutHz > 21.0f, useHighCut = settings.highCutHz < 19900.0f;
            lowCut.setCutoffFrequency (juce::jlimit (20.0f, 20000.0f, settings.lowCutHz));
            highCut.setCutoffFrequency (juce::jlimit (20.0f, (float) (0.45 * sampleRate), settings.highCutHz));
            for (int i = 0; i < numSamples; ++i)
            {
                auto l = left[(size_t) i] * panLeft, r = right[(size_t) i] * panRight;
                if (useLowCut)
                {
                    l = lowCut.processSample (0, l);
                    r = lowCut.processSample (1, r);
                }
                if (useHighCut)
                {
                    l = highCut.processSample (0, l);
                    r = highCut.processSample (1, r);
                }
                left[(size_t) i] = l;
                right[(size_t) i] = r;
            }

            // What came in at DRY, the vocoder at LEVEL.
            const auto stereo = wet.getNumChannels() > 1;
            auto* outL = wet.getChannelPointer (0);
            auto* outR = stereo ? wet.getChannelPointer (1) : nullptr;
            for (int i = 0; i < numSamples; ++i)
            {
                const auto d = dry.getNextValue(), g = level.getNextValue();
                if (stereo)
                {
                    outL[i] = outL[i] * d + left[(size_t) i] * g;
                    outR[i] = outR[i] * d + right[(size_t) i] * g;
                }
                else
                    outL[i] = outL[i] * d + 0.5f * (left[(size_t) i] + right[(size_t) i]) * g;
            }

            for (int b = 0; b < maxBands; ++b)
                shownBands[(size_t) b] = b < numBands ? bands[(size_t) b].voiceLevel : 0.0f;
            shownNumBands = numBands;
            for (size_t v = 0; v < voices.size(); ++v)
                shownNotes[v] = voices[v].gain > 0.01f ? voices[v].currentNote : -1.0f;
        });
        if (bypass.isSilent())
        {
            for (auto& shown : shownBands)
                shown = 0.0f;
            for (auto& shown : shownNotes)
                shown = -1.0f;
        }
    }
}
