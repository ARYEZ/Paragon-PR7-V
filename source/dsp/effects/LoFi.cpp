#include "LoFi.h"

namespace effects
{
    namespace
    {
        constexpr float twoPi = juce::MathConstants<float>::twoPi;

        // The band each speaker type lets through, from AMOUNT 0 to AMOUNT 10
        // (it narrows as AMOUNT rises), its resonant bump, and how hard it's driven.
        struct Speaker
        {
            float lowHz0, lowHz10, highHz0, highHz10;
            float bumpHz, bumpQ, bumpGain0, bumpGain10;
            float drive0, drive10;
            bool mono;
        };
        constexpr Speaker telephone { 250.0f, 500.0f, 3800.0f, 2400.0f, 1600.0f, 1.0f, 0.0f, 0.4f, 1.0f, 4.0f, true };
        constexpr Speaker amRadio   { 100.0f, 280.0f, 5500.0f, 3200.0f, 1400.0f, 1.2f, 0.3f, 1.0f, 1.0f, 2.5f, true };
        constexpr Speaker megaphone { 400.0f, 700.0f, 3200.0f, 2200.0f, 1100.0f, 2.5f, 0.8f, 2.5f, 3.0f, 14.0f, true };
        // 0.36.0's: a phone's band (the codec does the rest), a handheld
        // radio (narrow, driven hard), a gramophone's horn (with a second
        // resonance higher up, `horn`).
        constexpr Speaker cellPhone    { 300.0f, 400.0f, 3400.0f, 3000.0f, 1800.0f, 1.0f, 0.0f, 0.3f, 1.0f, 2.0f, true };
        constexpr Speaker walkieTalkie { 500.0f, 750.0f, 2800.0f, 2100.0f, 1500.0f, 1.8f, 0.6f, 1.8f, 5.0f, 18.0f, true };
        constexpr Speaker gramophone   { 220.0f, 380.0f, 3200.0f, 2200.0f, 850.0f, 3.0f, 1.2f, 2.5f, 1.3f, 2.5f, true };

        const Speaker& speakerFor (LoFi::Type type)
        {
            switch (type)
            {
                case LoFi::Type::amRadio:      return amRadio;
                case LoFi::Type::megaphone:    return megaphone;
                case LoFi::Type::cellPhone:    return cellPhone;
                case LoFi::Type::walkieTalkie: return walkieTalkie;
                case LoFi::Type::gramophone:   return gramophone;
                case LoFi::Type::telephone:
                case LoFi::Type::cassette:
                case LoFi::Type::bitcrusher:   break;
            }
            return telephone;
        }

        // A phone codec's companding (the mu-law curve): quiet sounds get
        // more of the steps, so it stays clear but grainy. `steps` levels each side.
        float muLaw (float x, float steps) noexcept
        {
            constexpr float mu = 255.0f;
            const auto magnitude = juce::jlimit (0.0f, 1.0f, std::abs (x));
            auto y = std::log1p (mu * magnitude) / std::log1p (mu);
            y = std::round (y * steps) / steps;
            const auto back = (std::pow (1.0f + mu, y) - 1.0f) / mu;
            return x < 0.0f ? -back : back;
        }

        float lerp (float a, float b, float t) { return a + (b - a) * t; }

        // Noise at NOISE 10, as a gain: about -34 dB, clearly audible hiss
        // under the playing without burying it.
        constexpr float maxNoise = 0.02f;
    }

    LoFi::LoFi() : tape (4096) {}

    float LoFi::saturate (float x, float driveAmount) noexcept
    {
        // tanh clips softly; dividing by the square root of the drive keeps
        // quiet signals a little louder and loud ones a little quieter, so
        // turning AMOUNT up adds grit without a big jump in volume.
        return std::tanh (driveAmount * x) / std::sqrt (driveAmount);
    }

    void LoFi::prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, (juce::uint32) maxChannels };
        for (auto* filter : { &lowCuts[0], &lowCuts[1] })
        {
            filter->prepare (spec);
            filter->setType (juce::dsp::StateVariableTPTFilterType::highpass);
        }
        for (auto* filter : { &highCuts[0], &highCuts[1] })
        {
            filter->prepare (spec);
            filter->setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        }
        resonance.prepare (spec);
        resonance.setType (juce::dsp::StateVariableTPTFilterType::bandpass);
        horn.prepare (spec);
        horn.setType (juce::dsp::StateVariableTPTFilterType::bandpass);
        horn.setCutoffFrequency (2300.0f);
        horn.setResonance (3.0f);
        for (auto& filter : codecFilter)
        {
            filter.prepare (spec);
            filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
            filter.setCutoffFrequency (3400.0f);
        }
        quietFor = 0;
        frameCountdown = 0;
        frameLost = false;
        lostGain = 1.0f;
        voiceLevel = 0.0f;
        talking = false;
        squelchLeft = 0;
        click = 0.0f;
        lastAmount = -1.0f;

        tape.setMaximumDelayInSamples ((int) std::ceil (0.02 * sampleRate) + 4);
        tape.prepare (spec);
        wowPhase = flutterPhase = drift = driftTarget = 0.0f;
        driftCountdown = 0;
        holdCountdown = 0;
        std::fill (std::begin (heldSample), std::end (heldSample), 0.0f);
        std::fill (std::begin (noiseTone), std::end (noiseTone), 0.0f);

        mix.reset (sampleRate, 0.05);
        level.reset (sampleRate, 0.05);
        bypass.prepare (sampleRate, maxBlockSize);
    }

    float LoFi::nextNoise() noexcept
    {
        return random.nextFloat() * 2.0f - 1.0f;
    }

    void LoFi::setUpFilters (Type type, float amount) noexcept
    {
        // Retuned only when TYPE or AMOUNT changes.
        if (type == lastType && juce::approximatelyEqual (amount, lastAmount))
            return;
        lastType = type;
        lastAmount = amount;

        const auto t = juce::jlimit (0.0f, 10.0f, amount) / 10.0f;
        if (type == Type::cassette)
        {
            // Tape: barely any low cut, and a top end that dulls from 12 kHz
            // (a good tape) to 4 kHz (a worn one).
            for (auto& filter : lowCuts)
                filter.setCutoffFrequency (35.0f);
            for (auto& filter : highCuts)
                filter.setCutoffFrequency (lerp (12000.0f, 4000.0f, t));
            resonanceGain = 0.0f;
            drive = lerp (1.0f, 3.0f, t);
            return;
        }

        const auto& speaker = speakerFor (type);
        const auto nyquistGuard = (float) sampleRate * 0.45f;
        for (auto& filter : lowCuts)
            filter.setCutoffFrequency (lerp (speaker.lowHz0, speaker.lowHz10, t));
        for (auto& filter : highCuts)
            filter.setCutoffFrequency (juce::jmin (nyquistGuard, lerp (speaker.highHz0, speaker.highHz10, t)));
        resonance.setCutoffFrequency (speaker.bumpHz);
        resonance.setResonance (speaker.bumpQ);
        resonanceGain = lerp (speaker.bumpGain0, speaker.bumpGain10, t) / speaker.bumpQ;   // JUCE's band-pass peaks at Q
        drive = lerp (speaker.drive0, speaker.drive10, t);
    }

    void LoFi::process (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        bypass.process (block, settings.on, [&] (juce::dsp::AudioBlock<float> wet)
        {
            mix.setTargetValue (settings.mix);
            level.setTargetValue (juce::Decibels::decibelsToGain (settings.levelDb));
            setUpFilters (settings.type, settings.amount);

            switch (settings.type)
            {
                case Type::telephone:
                case Type::amRadio:
                case Type::megaphone:   processSpeaker (wet, settings); break;
                case Type::cassette:    processCassette (wet, settings); break;
                case Type::bitcrusher:  processBitcrusher (wet, settings); break;
                case Type::cellPhone:    processCellPhone (wet, settings); break;
                case Type::walkieTalkie: processWalkieTalkie (wet, settings); break;
                case Type::gramophone:   processGramophone (wet, settings); break;
            }
        });
    }

    void LoFi::processSpeaker (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        // Mono, as phones, radios and megaphones are: the left and right are
        // summed first, then the speaker's sound is put on both sides.
        const auto numChannels = (int) block.getNumChannels();
        const auto noiseGain = maxNoise * juce::jlimit (0.0f, 10.0f, settings.noise) / 10.0f;

        for (size_t i = 0; i < block.getNumSamples(); ++i)
        {
            float input = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                input += block.getChannelPointer ((size_t) ch)[i];
            input /= (float) numChannels;

            // The line hiss goes through the speaker too, so it sounds like part of it.
            auto x = input + noiseGain * nextNoise();
            for (auto& filter : lowCuts)
                x = filter.processSample (0, x);
            x += resonance.processSample (0, x) * resonanceGain;
            x = saturate (x, drive);
            for (auto& filter : highCuts)
                x = filter.processSample (0, x);   // after the clipping, to tame its fizz as a small speaker does

            const auto mixNow = mix.getNextValue();
            const auto gain = level.getNextValue();
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& sample = block.getChannelPointer ((size_t) ch)[i];
                sample = (sample * (1.0f - mixNow) + x * mixNow) * gain;
            }
        }
    }

    void LoFi::processCassette (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        // Wow (a slow, seasick pitch drift from an uneven capstan), flutter
        // (a fast shimmer from the tape scraping the heads) and a random
        // wander, all as a delay that moves, so the pitch bends. Then tape
        // saturation, a dulled top end and hiss.
        const auto t = juce::jlimit (0.0f, 10.0f, settings.amount) / 10.0f;
        const auto msToSamples = (float) sampleRate * 0.001f;
        const auto wowMs = 1.6f * t, flutterMs = 0.08f * t, driftMs = 0.8f * t;
        const auto noiseGain = maxNoise * juce::jlimit (0.0f, 10.0f, settings.noise) / 10.0f;
        const auto driftSmoothing = 1.0f - std::exp (-1.0f / (0.3f * (float) sampleRate));
        const auto hissTone = 1.0f - std::exp (-twoPi * 6000.0f / (float) sampleRate);
        const auto numChannels = (int) block.getNumChannels();

        for (size_t i = 0; i < block.getNumSamples(); ++i)
        {
            wowPhase += 0.55f / (float) sampleRate;
            wowPhase -= std::floor (wowPhase);
            flutterPhase += 7.0f / (float) sampleRate;
            flutterPhase -= std::floor (flutterPhase);
            if (--driftCountdown <= 0)
            {
                driftTarget = nextNoise();
                driftCountdown = (int) (sampleRate * 0.4);
            }
            drift += (driftTarget - drift) * driftSmoothing;

            const auto delayMs = 6.0f + wowMs * std::sin (twoPi * wowPhase) + flutterMs * std::sin (twoPi * flutterPhase) + driftMs * drift;
            const auto mixNow = mix.getNextValue();
            const auto gain = level.getNextValue();

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& sample = block.getChannelPointer ((size_t) ch)[i];
                tape.pushSample (ch, sample);
                auto x = saturate (tape.popSample (ch, delayMs * msToSamples), drive);

                noiseTone[ch] += (nextNoise() - noiseTone[ch]) * hissTone;   // tape hiss is soft rather than harsh
                x += noiseGain * 1.5f * noiseTone[ch];

                for (auto& filter : lowCuts)
                    x = filter.processSample (ch, x);
                for (auto& filter : highCuts)
                    x = filter.processSample (ch, x);

                sample = (sample * (1.0f - mixNow) + x * mixNow) * gain;
            }
        }
    }

    void LoFi::processBitcrusher (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        // Fewer bits (from 12 at AMOUNT 0 down to 3 at 10) gives the grainy
        // fuzz of early samplers and games; holding each sample for longer
        // (an effective rate from the full rate down to 3 kHz) gives the
        // metallic, aliased ring. Both are what make it sound "digital".
        const auto t = juce::jlimit (0.0f, 10.0f, settings.amount) / 10.0f;
        const auto bits = lerp (12.0f, 3.0f, t);
        const auto steps = std::exp2 (bits - 1.0f);
        const auto targetRate = lerp ((float) sampleRate, 3000.0f, t * t);
        const auto holdFor = juce::jmax (1, juce::roundToInt ((float) sampleRate / targetRate));
        const auto noiseGain = maxNoise * juce::jlimit (0.0f, 10.0f, settings.noise) / 10.0f;
        const auto numChannels = juce::jmin ((int) block.getNumChannels(), maxChannels);

        for (size_t i = 0; i < block.getNumSamples(); ++i)
        {
            const auto takeNew = --holdCountdown <= 0;
            if (takeNew)
                holdCountdown = holdFor;
            const auto mixNow = mix.getNextValue();
            const auto gain = level.getNextValue();

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& sample = block.getChannelPointer ((size_t) ch)[i];
                if (takeNew)
                    heldSample[ch] = std::round (juce::jlimit (-1.0f, 1.0f, sample + noiseGain * nextNoise()) * steps) / steps;
                sample = (sample * (1.0f - mixNow) + heldSample[ch] * mixNow) * gain;
            }
        }
    }

    void LoFi::writeMono (juce::dsp::AudioBlock<float> block, size_t i, float x) noexcept
    {
        const auto mixNow = mix.getNextValue();
        const auto gain = level.getNextValue();
        for (size_t ch = 0; ch < block.getNumChannels(); ++ch)
        {
            auto& sample = block.getChannelPointer (ch)[i];
            sample = (sample * (1.0f - mixNow) + x * mixNow) * gain;
        }
    }

    void LoFi::processCellPhone (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        // The phone band, then the codec: an 8 kHz rate (4 kHz at AMOUNT 10)
        // with 8-bit companded steps (4 bits at 10), then the band again to
        // smooth the steps. NOISE: a bad signal, whole 20 ms moments lost
        // (up to one in six at NOISE 10), faded in 3 ms so they don't click.
        const auto t = juce::jlimit (0.0f, 10.0f, settings.amount) / 10.0f;
        const auto steps = std::exp2 (lerp (7.0f, 3.0f, t));
        const auto holdFor = juce::jmax (1, juce::roundToInt ((float) sampleRate / lerp (8000.0f, 4000.0f, t)));
        const auto lossChance = 0.16f * juce::jlimit (0.0f, 10.0f, settings.noise) / 10.0f;
        const auto frameLength = juce::jmax (1, (int) (frameSeconds * sampleRate));
        const auto fade = 1.0f - std::exp (-1.0f / (0.003f * (float) sampleRate));
        const auto numChannels = (int) block.getNumChannels();
        for (auto& filter : codecFilter)
            filter.setCutoffFrequency (juce::jmin (3400.0f, 0.42f * lerp (8000.0f, 4000.0f, t)));

        for (size_t i = 0; i < block.getNumSamples(); ++i)
        {
            auto input = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                input += block.getChannelPointer ((size_t) ch)[i];
            input /= (float) juce::jmax (1, numChannels);

            if (--frameCountdown <= 0)
            {
                frameCountdown = frameLength;
                frameLost = random.nextFloat() < lossChance;
            }
            lostGain += ((frameLost ? 0.0f : 1.0f) - lostGain) * fade;

            // The codec hears only the phone band (up to 3.4 kHz, lower as
            // its rate drops), so nothing above it folds down when sampled.
            auto x = input;
            for (auto& filter : lowCuts)
                x = filter.processSample (0, x);
            for (auto& filter : codecFilter)
                x = filter.processSample (0, x);
            if (--holdCountdown <= 0)
            {
                holdCountdown = holdFor;
                heldSample[0] = muLaw (saturate (x, drive), steps);
            }
            x = heldSample[0];
            for (auto& filter : highCuts)
                x = filter.processSample (0, x);
            writeMono (block, i, x * lostGain);
        }
    }

    void LoFi::processWalkieTalkie (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        // Narrow and driven hard. While the voice is there, static under it
        // (NOISE); when it stops, the squelch: a 140 ms burst of hiss
        // through the same little speaker, as the receiver closes.
        const auto noise = juce::jlimit (0.0f, 10.0f, settings.noise) / 10.0f;
        const auto staticGain = maxNoise * 1.5f * noise;
        const auto burstGain = 0.08f * (0.4f + 0.6f * noise);
        const auto squelchLength = (int) (squelchSeconds * sampleRate);
        const auto attack = 1.0f - std::exp (-1.0f / (0.005f * (float) sampleRate));
        const auto release = 1.0f - std::exp (-1.0f / (0.03f * (float) sampleRate));
        const auto hangOn = (int) (0.15 * sampleRate);       // a gap between words doesn't close it
        constexpr float openAt = 0.01f, closeAt = 0.004f;   // about -40 and -48 dB
        // Driven hard, then brought back so a sung line is about as loud as
        // it went in: the loudest parts squashed, as a handheld radio does.
        const auto makeUp = std::pow (drive, -0.4f);
        const auto numChannels = (int) block.getNumChannels();

        for (size_t i = 0; i < block.getNumSamples(); ++i)
        {
            auto input = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                input += block.getChannelPointer ((size_t) ch)[i];
            input /= (float) juce::jmax (1, numChannels);

            const auto heard = std::abs (input);
            voiceLevel += (heard - voiceLevel) * (heard > voiceLevel ? attack : release);
            quietFor = voiceLevel < closeAt ? quietFor + 1 : 0;
            if (! talking && voiceLevel > openAt)
            {
                talking = true;
                squelchLeft = 0;
            }
            else if (talking && quietFor > hangOn)
            {
                talking = false;
                squelchLeft = squelchLength;
            }

            auto x = input;
            if (talking)
                x += staticGain * nextNoise();
            if (squelchLeft > 0)
            {
                const auto left = (float) squelchLeft / (float) squelchLength;
                x += burstGain * nextNoise() * std::sqrt (left);
                --squelchLeft;
            }
            for (auto& filter : lowCuts)
                x = filter.processSample (0, x);
            x += resonance.processSample (0, x) * resonanceGain;
            x = saturate (x, drive) * makeUp;
            for (auto& filter : highCuts)
                x = filter.processSample (0, x);
            writeMono (block, i, x);
        }
    }

    void LoFi::processGramophone (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        // A slow warble (the record a little off-centre), the horn's two
        // resonances, gentle saturation, the narrow band; NOISE adds the
        // surface: crackle (random clicks, up to about 25 a second) and hiss.
        const auto t = juce::jlimit (0.0f, 10.0f, settings.amount) / 10.0f;
        const auto noise = juce::jlimit (0.0f, 10.0f, settings.noise) / 10.0f;
        const auto msToSamples = (float) sampleRate * 0.001f;
        const auto warbleMs = 0.3f + 0.9f * t;
        const auto clickChance = 25.0f * noise / (float) sampleRate;
        const auto clickDecay = std::exp (-1.0f / (0.0004f * (float) sampleRate));
        const auto hissGain = maxNoise * 0.6f * noise;
        // The horn's peaks and the saturation make it louder: brought back level (measured, on the voice band).
        const auto hornMakeUp = 0.45f / std::sqrt (drive);
        const auto numChannels = (int) block.getNumChannels();

        for (size_t i = 0; i < block.getNumSamples(); ++i)
        {
            auto input = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                input += block.getChannelPointer ((size_t) ch)[i];
            input /= (float) juce::jmax (1, numChannels);

            wowPhase += 0.75f / (float) sampleRate;   // 45 rpm
            wowPhase -= std::floor (wowPhase);
            tape.pushSample (0, input);
            auto x = tape.popSample (0, (4.0f + warbleMs * std::sin (twoPi * wowPhase)) * msToSamples);

            if (random.nextFloat() < clickChance)
                click = (random.nextFloat() * 2.0f - 1.0f) * 0.35f * (0.3f + random.nextFloat());
            click *= clickDecay;
            x += click + hissGain * nextNoise();

            for (auto& filter : lowCuts)
                x = filter.processSample (0, x);
            x += resonance.processSample (0, x) * resonanceGain + horn.processSample (0, x) * resonanceGain * 0.5f;
            x = saturate (x, drive) * hornMakeUp;
            for (auto& filter : highCuts)
                x = filter.processSample (0, x);
            writeMono (block, i, x);
        }
    }
}
