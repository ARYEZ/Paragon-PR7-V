#include "Doubler.h"

namespace effects
{
    namespace
    {
        float gainFor (float db) noexcept { return db <= Doubler::minGainDb ? 0.0f : juce::Decibels::decibelsToGain (db); }

        // Centred, a voice is at full level on both sides; panned, it fades
        // from the far side (as HARMONY's voices are).
        std::pair<float, float> panned (float gain, float pan) noexcept
        {
            pan = juce::jlimit (-1.0f, 1.0f, pan);
            return { gain * juce::jmin (1.0f, 1.0f - pan), gain * juce::jmin (1.0f, 1.0f + pan) };
        }
    }

    void Doubler::Wander::prepare (double sampleRate, int seed, float glideSeconds)
    {
        random.setSeed (seed);
        rate = (float) sampleRate;
        glide = 1.0f - std::exp (-1.0f / (glideSeconds * rate));
        goal = first = value = 0.0f;
        countdown = 0;
    }

    float Doubler::Wander::next() noexcept
    {
        if (--countdown <= 0)
        {
            goal = random.nextFloat() * 2.0f - 1.0f;
            countdown = (int) (rate * (0.5f + random.nextFloat()));
        }
        first += (goal - first) * glide;
        value += (first - value) * glide;
        return value;
    }

    void Doubler::prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        const juce::dsp::ProcessSpec mono { sampleRate, (juce::uint32) maxBlockSize, 1 };
        for (auto& voice : voices)
        {
            voice.delay.setMaximumDelayInSamples ((int) std::ceil ((maxDelayMs + maxDriftMs) * 0.001 * sampleRate) + 8);
            voice.delay.prepare (mono);
            voice.shifter.prepare (sampleRate);
            voice.feedbackFilter.prepare (mono);
            voice.feedbackFilter.setType (juce::dsp::StateVariableTPTFilterType::highpass);
            voice.feedbackFilter.setResonance (1.0f / juce::MathConstants<float>::sqrt2);
            voice.feedbackFilter.setCutoffFrequency (16.0f);
            for (auto* smoothed : { &voice.delaySamples, &voice.left, &voice.right })
                smoothed->reset (sampleRate, 0.05);
            voice.delaySamples.setCurrentAndTargetValue (0.0f);
            voice.left.setCurrentAndTargetValue (0.0f);
            voice.right.setCurrentAndTargetValue (0.0f);
            voice.lfoPhase = voice.lastOut = 0.0f;
            // Each voice wanders its own way (and the same way every time, for
            // tests). The timing glides slowly: the faster a delay moves, the
            // more it bends the pitch, and 0.7 s keeps that to a few cents.
            const auto index = (int) (&voice - voices.data());
            voice.timing.prepare (sampleRate, 101 + 2 * index, 0.7f);
            voice.pitch.prepare (sampleRate, 202 + 2 * index, 0.25f);
        }
        nominalDelayMs = (float) (voices[0].shifter.getRestingDelay() / sampleRate * 1000.0);

        for (auto* filter : { &lowShelf, &highShelf })
            for (auto& channel : *filter)
            {
                channel.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowShelf (sampleRate, 100.0, 0.707, 1.0f);
                channel.prepare (mono);
            }
        shelfSettings[0] = 1.0e9f;   // set them on the first block

        for (auto* smoothed : { &directLeft, &directRight, &output })
        {
            smoothed->reset (sampleRate, 0.05);
            smoothed->setCurrentAndTargetValue (1.0f);
        }
        bypass.prepare (sampleRate, maxBlockSize);
    }

    void Doubler::updateShelves (const Settings& settings) noexcept
    {
        // Retune only when a setting changes (in place: no allocation).
        const float now[4] { settings.lowShelfOn ? settings.lowShelfDb : 0.0f, settings.lowShelfHz,
                             settings.highShelfOn ? settings.highShelfDb : 0.0f, settings.highShelfHz };
        if (std::equal (std::begin (now), std::end (now), std::begin (shelfSettings)))
            return;
        std::copy (std::begin (now), std::end (now), std::begin (shelfSettings));
        const auto nyquistSafe = [this] (float hz) { return juce::jlimit (16.0f, (float) sampleRate * 0.45f, hz); };
        const auto low = juce::dsp::IIR::ArrayCoefficients<float>::makeLowShelf (sampleRate, nyquistSafe (now[1]), 0.707f,
                                                                                   juce::Decibels::decibelsToGain (now[0]));
        const auto high = juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (sampleRate, nyquistSafe (now[3]), 0.707f,
                                                                                    juce::Decibels::decibelsToGain (now[2]));
        for (auto& channel : lowShelf)
            *channel.coefficients = low;
        for (auto& channel : highShelf)
            *channel.coefficients = high;
    }

    void Doubler::process (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        if (resetRequested.exchange (false))
            for (auto& voice : voices)
                voice.lfoPhase = 0.0f;

        bypass.process (block, settings.on, [&] (juce::dsp::AudioBlock<float> wetBlock)
        {
            updateShelves (settings);

            const auto shifterDelay = (float) voices[0].shifter.getRestingDelay();
            for (size_t v = 0; v < voices.size(); ++v)
            {
                const auto& setting = settings.voices[v];
                auto& voice = voices[v];
                // The detuning already delays the voice by about its resting
                // delay: the delay line adds the rest of DELAY.
                const auto wanted = setting.delayMs * 0.001f * (float) sampleRate;
                voice.delaySamples.setTargetValue (juce::jmax (0.0f, wanted - shifterDelay));
                const auto [left, right] = panned (setting.on ? gainFor (setting.gainDb) * settings.wet : 0.0f, setting.pan);
                voice.left.setTargetValue (left);
                voice.right.setTargetValue (right);
                voice.feedbackFilter.setCutoffFrequency (juce::jlimit (16.0f, (float) sampleRate * 0.45f, settings.feedbackHighPassHz));
            }
            const auto [directL, directR] = panned (settings.directOn ? gainFor (settings.directGainDb) : 0.0f, settings.directPan);
            directLeft.setTargetValue (directL);
            directRight.setTargetValue (directR);
            output.setTargetValue (juce::Decibels::decibelsToGain (settings.outputDb));

            const auto stereo = wetBlock.getNumChannels() > 1;
            auto* outLeft = wetBlock.getChannelPointer (0);
            auto* outRight = stereo ? wetBlock.getChannelPointer (1) : nullptr;

            for (size_t i = 0; i < wetBlock.getNumSamples(); ++i)
            {
                const auto inLeft = outLeft[i], inRight = stereo ? outRight[i] : outLeft[i];
                const auto mono = 0.5f * (inLeft + inRight);

                auto wetLeft = 0.0f, wetRight = 0.0f, wetMono = 0.0f;
                for (size_t v = 0; v < voices.size(); ++v)
                {
                    const auto& setting = settings.voices[v];
                    auto& voice = voices[v];
                    const auto leftGain = voice.left.getNextValue(), rightGain = voice.right.getNextValue();
                    const auto drift = juce::jlimit (0.0f, 1.0f, setting.drift);
                    const auto timingWander = voice.timing.next(), pitchWander = voice.pitch.next();
                    const auto delay = juce::jlimit (0.0f, (float) maxDelayMs * 0.001f * (float) sampleRate + maxDriftMs * 0.001f * (float) sampleRate,
                                                     voice.delaySamples.getNextValue()
                                                         + drift * timingWander * maxDriftMs * 0.001f * (float) sampleRate);
                    if (leftGain <= 0.0f && rightGain <= 0.0f && ! voice.left.isSmoothing())
                        continue;   // silent: save the work

                    // The wobble: a sine around DETUNE; a positive DEPTH starts
                    // upwards. DRIFT wanders around that too.
                    voice.lfoPhase += setting.rateHz / (float) sampleRate;
                    voice.lfoPhase -= std::floor (voice.lfoPhase);
                    const auto cents = setting.detuneCents + setting.depthCents * std::sin (juce::MathConstants<float>::twoPi * voice.lfoPhase)
                                     + drift * pitchWander * maxDriftCents;
                    const auto ratio = std::pow (2.0f, cents / 1200.0f) * (setting.octaveDown ? 0.5f : 1.0f);

                    // Feedback: some of the voice back into its own delay, its lows cut.
                    voice.delay.pushSample (0, mono + setting.feedback * voice.feedbackFilter.processSample (0, voice.lastOut));
                    const auto delayed = voice.delay.popSample (0, delay);
                    const auto out = voice.shifter.processSample (delayed, ratio);
                    voice.lastOut = juce::jlimit (-4.0f, 4.0f, out);   // keep runaway feedback in bounds

                    wetLeft += leftGain * out;
                    wetRight += rightGain * out;
                    wetMono += juce::jmax (leftGain, rightGain) * out;   // mono: at its full GAIN, whatever its PAN
                }

                const auto gain = output.getNextValue();
                const auto directL = directLeft.getNextValue(), directR = directRight.getNextValue();
                if (stereo)
                {
                    // The shelves shape the voices, not the direct voice.
                    wetLeft = highShelf[0].processSample (lowShelf[0].processSample (wetLeft));
                    wetRight = highShelf[1].processSample (lowShelf[1].processSample (wetRight));
                    outLeft[i] = gain * (directL * inLeft + wetLeft);
                    outRight[i] = gain * (directR * inRight + wetRight);
                }
                else
                {
                    wetMono = highShelf[0].processSample (lowShelf[0].processSample (wetMono));
                    outLeft[i] = gain * (juce::jmax (directL, directR) * inLeft + wetMono);
                }
            }
        });
    }
}
