#include "Cyclone.h"

namespace effects
{
    namespace
    {
        constexpr float twoPi = juce::MathConstants<float>::twoPi;
        constexpr float speedOfSound = 343.0f;   // m/s

        float decibels (float dB) noexcept { return std::pow (10.0f, dB / 20.0f); }

        // RBJ cookbook biquads, normalised so a0 = 1.
        template <typename Biquad>
        void setCoefficients (Biquad& f, double b0, double b1, double b2, double a0, double a1, double a2) noexcept
        {
            f.b0 = (float) (b0 / a0); f.b1 = (float) (b1 / a0); f.b2 = (float) (b2 / a0);
            f.a1 = (float) (a1 / a0); f.a2 = (float) (a2 / a0);
        }
        template <typename Biquad>
        void setLowPass (Biquad& f, double rate, double hz, double q) noexcept
        {
            const auto w = juce::MathConstants<double>::twoPi * hz / rate, c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
            setCoefficients (f, (1.0 - c) / 2.0, 1.0 - c, (1.0 - c) / 2.0, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
        }
        template <typename Biquad>
        void setHighPass (Biquad& f, double rate, double hz, double q) noexcept
        {
            const auto w = juce::MathConstants<double>::twoPi * hz / rate, c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
            setCoefficients (f, (1.0 + c) / 2.0, -(1.0 + c), (1.0 + c) / 2.0, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
        }
        template <typename Biquad>
        void setPeak (Biquad& f, double rate, double hz, double q, double dB) noexcept
        {
            const auto a = std::pow (10.0, dB / 40.0);
            const auto w = juce::MathConstants<double>::twoPi * hz / rate, c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
            setCoefficients (f, 1.0 + alpha * a, -2.0 * c, 1.0 - alpha * a, 1.0 + alpha / a, -2.0 * c, 1.0 - alpha / a);
        }
        template <typename Biquad>
        void setHighShelf (Biquad& f, double rate, double hz, double dB) noexcept
        {
            const auto a = std::pow (10.0, dB / 40.0);
            const auto w = juce::MathConstants<double>::twoPi * hz / rate, c = std::cos (w);
            const auto alpha = std::sin (w) / 2.0 * std::sqrt (2.0);   // shelf slope 1
            const auto root = 2.0 * std::sqrt (a) * alpha;
            setCoefficients (f, a * ((a + 1.0) + (a - 1.0) * c + root), -2.0 * a * ((a - 1.0) + (a + 1.0) * c),
                             a * ((a + 1.0) + (a - 1.0) * c - root),
                             (a + 1.0) - (a - 1.0) * c + root, 2.0 * ((a - 1.0) - (a + 1.0) * c), (a + 1.0) - (a - 1.0) * c - root);
        }

        // The cabinet's valve amp, overdriven: DRIVE's gain into an uneven
        // (valve-like, so even harmonics too) soft clip, scaled so small
        // signals come out at the level they went in. Our guess at a
        // plausible curve: the manual says only that it simulates the
        // amp's power valves saturating, level-compensated.
        constexpr float driveRangeDb = 36.0f, valveBias = 0.25f;
        float shaped (float x, float gain) noexcept
        {
            const auto t = std::tanh (valveBias);
            return (std::tanh (gain * x + valveBias) - t) / (gain * (1.0f - t * t));
        }
        float driveGain (float drive) noexcept { return decibels (drive / 10.0f * driveRangeDb); }

        // Each rotor's sound, as it turns to face away from a mic: how much
        // quieter it gets with the mic close (MIX/DIST at 5), and how far
        // the horn's top end falls. Our guesses: the horn's sound beams
        // forward (about 15 dB down at the back), the drum's spreads more.
        constexpr float hornSwing = 0.82f, drumSwing = 0.5f;
        constexpr float hornBrightHz = 13000.0f, hornDullHz = 2500.0f;
        // Moving the mics all the way back (MIX/DIST at 10) leaves this much of the swing.
        constexpr float farSwing = 0.25f;
        // The horn's stereo pair: a quarter-turn apart, either side of the front (our guess).
        constexpr float micTurn = 0.125f;
        constexpr float baseDelayMs = 1.0f;
        // BALANCE at either end: this much cut on the other rotor (our guess).
        constexpr float balanceRangeDb = 15.0f;
        // LEVEL: 5 is about the bypassed level; each step is this many dB (our guess).
        constexpr float levelDbPerStep = 2.4f;
        // STOP: below this speed the rotors ease into their forward-facing rest.
        constexpr float parkHz = 0.6f;

        // Mixing and mic distance from a MIX/DIST knob: up to 5 the rotor
        // fades in over the unspun signal; above 5 it's all rotor and the
        // mics move back.
        float rotorMix (float knob) noexcept { return juce::jlimit (0.0f, 1.0f, knob / 5.0f); }
        float nearness (float knob) noexcept { return 1.0f - (1.0f - farSwing) * juce::jlimit (0.0f, 1.0f, (knob - 5.0f) / 5.0f); }
    }

    Cyclone::Ramps Cyclone::rampSeconds (float acceleration) noexcept
    {
        // The manual gives each rotor's time at four points of the knob
        // (fastest, then on to slowest); at 1 o'clock (6 here) it gives the
        // average 122's times: drum 5.5 s both ways, horn 1 s up and 1.6 s
        // down. Placing the points at 0, 5, 8 and 10 lands exactly on those.
        static constexpr float points[] = { 0.0f, 5.0f, 8.0f, 10.0f };
        constexpr float drumUp[] = { 0.5f, 4.2f, 8.0f, 8.0f }, hornUp[] = { 0.5f, 0.5f, 2.0f, 8.0f };
        constexpr float drumDown[] = { 0.8f, 4.2f, 8.0f, 8.0f }, hornDown[] = { 0.8f, 0.8f, 3.2f, 8.0f };
        const auto at = [knob = juce::jlimit (0.0f, 10.0f, acceleration)] (const float (&values)[4])
        {
            for (int i = 1; i < 4; ++i)
                if (knob <= points[i])
                    return juce::jmap (knob, points[i - 1], points[i], values[i - 1], values[i]);
            return values[3];
        };
        return { at (drumUp), at (hornUp), at (drumDown), at (hornDown) };
    }

    void Cyclone::Rotor::advance (float targetHz, float upCoefficient, float downCoefficient, bool stopping, double rate) noexcept
    {
        if (! stopping)
        {
            parked = false;
            hz += (targetHz - hz) * (targetHz > hz ? upCoefficient : downCoefficient);
        }
        else if (parked)
        {
            hz = 0.0f;
            return;
        }
        else
        {
            // Braking, then easing round to face forward: the closer to the
            // front, the slower, until it settles there.
            hz += (0.0f - hz) * downCoefficient;
            if (hz < parkHz)
            {
                const auto remaining = 1.0f - turn;
                hz = juce::jlimit (0.04f, juce::jmax (0.04f, hz), remaining * 2.0f);
                if (remaining < 1.0e-4f || turn + hz / (float) rate >= 1.0f)
                {
                    turn = 0.0f;
                    hz = 0.0f;
                    parked = true;
                    return;
                }
            }
        }
        turn += hz / (float) rate;
        turn -= std::floor (turn);
    }

    Cyclone::FromOldRotary Cyclone::fromOldRotary (float rateHz, float depth, float mix) noexcept
    {
        // Above 2 Hz the old one was set for fast (its fastest factory rig
        // spun at 6.5 Hz, its slow ones under 1 Hz).
        const auto fast = rateHz >= 2.0f;
        const auto mixDist = mix < 0.99f ? 5.0f * juce::jlimit (0.0f, 1.0f, mix)                          // part dry: the mix half
                                         : 5.0f + 5.0f * (1.0f - juce::jlimit (0.0f, 1.0f, depth));   // all wet: DEPTH as how close the mics are
        return { fast,
                 fast ? juce::jlimit (fastMinHz, fastMaxHz, rateHz) : fastDefaultHz,
                 fast ? slowDefaultHz : juce::jlimit (slowMinHz, slowMaxHz, rateHz),
                 mixDist, Mode::guitar1 };
    }

    Cyclone::Cyclone()
        : hornDelay (2048), drumDelay (2048),
          oversampling (1, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true)   // 4x: DRIVE's clipping
    {
    }

    void Cyclone::prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        const juce::dsp::ProcessSpec monoSpec { sampleRate, (juce::uint32) maxBlockSize, 1 };
        crossover.setCutoffFrequency (800.0f);   // the 122's crossover (manual)
        crossover.prepare (monoSpec);
        for (auto* rotor : { &hornDelay, &drumDelay })
        {
            rotor->setMaximumDelayInSamples ((int) std::ceil (0.004 * sampleRate) + 4);
            rotor->prepare (monoSpec);
        }
        mono.setSize (1, maxBlockSize);
        oversampling.initProcessing ((size_t) maxBlockSize);

        // DRIVE's level matching: each step scaled so a guitar-level sine
        // (peak 0.25) comes out as loud as it went in, as on the pedal
        // ("the output level does not change much as you turn up the Drive knob").
        for (size_t step = 0; step < driveMakeUp.size(); ++step)
        {
            double in = 0.0, out = 0.0;
            const auto gain = driveGain ((float) step);
            for (int i = 0; i < 480; ++i)
            {
                const auto x = 0.25f * std::sin (twoPi * (float) i / 480.0f);
                in += x * x;
                const auto y = shaped (x, gain);
                out += y * y;
            }
            driveMakeUp[step] = (float) std::sqrt (in / out);
        }

        for (auto* value : { &hornMix, &drumMix, &hornNear, &drumNear, &hornGain, &drumGain, &outputGain, &wet })
            value->reset (sampleRate, 0.05);
        driveSmoothed.reset (sampleRate * 4.0, 0.05);
        currentMode = (Mode) -1;
        reset();
    }

    void Cyclone::reset() noexcept
    {
        crossover.reset();
        hornDelay.reset();
        drumDelay.reset();
        oversampling.reset();
        std::fill (std::begin (hornTone), std::end (hornTone), 0.0f);
        drumTone = 0.0f;
        for (auto& f : hornSpeaker) f.clear();
        for (auto& f : drumSpeaker) f.clear();
        for (auto& channel : lift)
            for (auto& f : channel)
                f.clear();
        // The next block starts the rotors at their speed, without spinning up.
        horn = {};
        drum = {};
        horn.hz = drum.hz = -1.0f;
    }

    void Cyclone::updateModeFilters (Mode mode) noexcept
    {
        if (mode == currentMode)
            return;
        currentMode = mode;
        // KEY: the 122's speakers. The horn driver's top end falls away
        // above about 6 kHz, with the horn's honk around 2.3 kHz; the drum's
        // 15" woofer has little below 45 Hz and a warm bump around 120 Hz.
        // GIT 2: GIT 1 with the mids and highs brought forward. Our guesses
        // at both: the manual names them but gives no curves.
        speakerOn = mode == Mode::keys;
        liftOn = mode == Mode::guitar2;
        setLowPass (hornSpeaker[0], sampleRate, 6000.0, 0.7071);
        setPeak (hornSpeaker[1], sampleRate, 2300.0, 1.0, 3.0);
        setHighPass (drumSpeaker[0], sampleRate, 45.0, 0.7071);
        setPeak (drumSpeaker[1], sampleRate, 120.0, 0.9, 2.0);
        for (auto& channel : lift)
        {
            setPeak (channel[0], sampleRate, 1600.0, 0.8, 3.0);
            setHighShelf (channel[1], sampleRate, 4000.0, 3.0);
        }
    }

    float Cyclone::driveSample (float x, float drive) noexcept
    {
        if (drive <= 0.0f)
            return x;
        const auto step = juce::jlimit (0.0f, 10.0f, drive);
        const auto low = (size_t) std::floor (step);
        const auto high = juce::jmin ((size_t) 10, low + 1);
        const auto makeUp = juce::jmap (step - (float) low, driveMakeUp[low], driveMakeUp[high]);
        const auto clipped = shaped (x, driveGain (step)) * makeUp;
        return x + (clipped - x) * juce::jmin (1.0f, step);   // from clean at 0, into the curve by 1
    }

    void Cyclone::process (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        const auto numSamples = (int) block.getNumSamples();
        const auto numChannels = (int) block.getNumChannels();
        if (numSamples == 0 || numChannels == 0)
            return;

        updateModeFilters (settings.mode);

        // One mono signal in (both sides carry the same here).
        auto* in = mono.getWritePointer (0);
        for (int i = 0; i < numSamples; ++i)
        {
            float sum = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                sum += block.getChannelPointer ((size_t) ch)[i];
            in[i] = sum / (float) numChannels;
        }

        // DRIVE, oversampled so the clipping doesn't alias.
        driveSmoothed.setTargetValue (settings.drive);
        {
            auto monoBlock = juce::dsp::AudioBlock<float> (mono).getSubBlock (0, (size_t) numSamples);
            auto up = oversampling.processSamplesUp (monoBlock);
            auto* samples = up.getChannelPointer (0);
            for (size_t i = 0; i < up.getNumSamples(); ++i)
                samples[i] = driveSample (samples[i], driveSmoothed.getNextValue());
            oversampling.processSamplesDown (monoBlock);
        }

        // The rotors' targets: the pedal's speed, or SLOW / FAST.
        const auto fastHz = juce::jlimit (fastMinHz, fastMaxHz, settings.fastHz);
        const auto slowHz = juce::jlimit (slowMinHz, slowMaxHz, settings.slowHz);
        const auto usePedal = settings.pedal >= 0.0f;
        // The pedal sweeps evenly by ear from SLOW SPEED to FAST SPEED (our guess at its curve).
        const auto hornTarget = usePedal ? slowHz * std::pow (fastHz / slowHz, juce::jlimit (0.0f, 1.0f, settings.pedal))
                                         : (settings.fast ? fastHz : slowHz);
        const auto stopping = settings.stop && ! usePedal;   // as on the pedal, STOP does nothing in SPEED mode
        const auto ramps = rampSeconds (settings.acceleration);
        // Each rotor gets about 95% of the way in its ramp time (three time constants).
        const auto coefficient = [this] (float seconds) { return 1.0f - std::exp (-3.0f / (seconds * (float) sampleRate)); };
        const auto hornUp = coefficient (ramps.hornUp), hornDown = coefficient (ramps.hornDown);
        const auto drumUp = coefficient (ramps.drumUp), drumDown = coefficient (ramps.drumDown);
        if (horn.hz < 0.0f)   // just started: already at speed (or stopped)
        {
            horn.hz = stopping ? 0.0f : hornTarget;
            drum.hz = stopping ? 0.0f : hornTarget * drumRatio;
            horn.parked = drum.parked = stopping;
        }

        hornMix.setTargetValue (rotorMix (settings.mixDistHigh));
        drumMix.setTargetValue (rotorMix (settings.mixDistLow));
        hornNear.setTargetValue (nearness (settings.mixDistHigh));
        drumNear.setTargetValue (nearness (settings.mixDistLow));
        const auto tilt = juce::jlimit (-1.0f, 1.0f, (settings.balance - 5.0f) / 5.0f);
        drumGain.setTargetValue (tilt > 0.0f ? decibels (-balanceRangeDb * tilt) : 1.0f);
        hornGain.setTargetValue (tilt < 0.0f ? decibels (balanceRangeDb * tilt) : 1.0f);
        outputGain.setTargetValue (decibels ((settings.level - 5.0f) * levelDbPerStep));
        wet.setTargetValue (juce::jlimit (0.0f, 1.0f, settings.morph));

        const auto msToSamples = (float) sampleRate * 0.001f;
        const auto hornSwingMs = 1000.0f * hornRadiusMetres / speedOfSound;
        const auto drumSwingMs = 1000.0f * drumRadiusMetres / speedOfSound;
        const auto dullToBright = std::log (hornBrightHz / hornDullHz);
        const auto outChannels = juce::jmin (numChannels, maxChannels);

        for (int i = 0; i < numSamples; ++i)
        {
            horn.advance (hornTarget, hornUp, hornDown, stopping, sampleRate);
            drum.advance (hornTarget * drumRatio, drumUp, drumDown, stopping, sampleRate);

            float bass = 0.0f, treble = 0.0f;
            crossover.processSample (0, in[i], bass, treble);

            // Into each rotor's speaker (KEY: the 122's own response).
            auto hornIn = treble, drumIn = bass;
            if (speakerOn)
            {
                hornIn = hornSpeaker[1].process (hornSpeaker[0].process (hornIn));
                drumIn = drumSpeaker[1].process (drumSpeaker[0].process (drumIn));
            }
            hornDelay.pushSample (0, hornIn);
            drumDelay.pushSample (0, drumIn);

            const auto hornNearNow = hornNear.getNextValue(), drumNearNow = drumNear.getNextValue();
            const auto hornDepth = hornSwing * hornNearNow, drumDepth = drumSwing * drumNearNow;
            const auto hornMixNow = hornMix.getNextValue(), drumMixNow = drumMix.getNextValue();
            const auto hornGainNow = hornGain.getNextValue(), drumGainNow = drumGain.getNextValue();
            const auto outputNow = outputGain.getNextValue(), wetNow = wet.getNextValue();

            // The drum, to one mic below, in front.
            const auto drumFacing = std::cos (twoPi * drum.turn);
            const auto drumSound = drumDelay.popSample (0, (baseDelayMs + drumSwingMs * drumFacing) * msToSamples, true);
            const auto drumLevel = (1.0f - drumDepth * (0.5f - 0.5f * drumFacing)) / (1.0f - 0.5f * drumDepth);   // kept as loud on average
            const auto drumOut = (bass * (1.0f - drumMixNow) + drumSound * drumLevel * drumMixNow) * drumGainNow;

            float out[maxChannels] {};
            for (int ch = 0; ch < outChannels; ++ch)
            {
                // The horn, to the left and right mics either side of the front.
                const auto mic = numChannels == 1 ? 0.0f : (ch == 0 ? -micTurn : micTurn);
                const auto facing = std::cos (twoPi * (horn.turn - mic));
                const auto sound = hornDelay.popSample (0, (baseDelayMs + hornSwingMs * facing) * msToSamples, ch == outChannels - 1);
                const auto towards = 0.5f + 0.5f * facing;
                const auto toneHz = hornDullHz * std::exp (dullToBright * towards);
                hornTone[ch] += (sound - hornTone[ch]) * (1.0f - std::exp (-twoPi * toneHz / (float) sampleRate));
                const auto level = (1.0f - hornDepth * (1.0f - towards)) / (1.0f - 0.5f * hornDepth);
                const auto hornOut = (treble * (1.0f - hornMixNow) + hornTone[ch] * level * hornMixNow) * hornGainNow;

                auto sample = hornOut + drumOut;
                if (liftOn)
                    sample = lift[ch][1].process (lift[ch][0].process (sample));
                out[ch] = sample * outputNow;
            }

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& sample = block.getChannelPointer ((size_t) ch)[i];
                sample += (out[juce::jmin (ch, outChannels - 1)] - sample) * wetNow;
            }
        }

        hornHzShown.store (horn.hz);
        drumHzShown.store (drum.hz);
        hornTurnShown.store (horn.turn);
        drumTurnShown.store (drum.turn);
    }
}
