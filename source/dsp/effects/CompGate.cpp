#include "CompGate.h"

namespace effects
{
    namespace
    {
        constexpr float sidechainCutHz = 100.0f;   // the detector ignores the bass below this
        constexpr float makeUpReferenceDb = -12.0f;   // Daybreak's make-up restores part of what it takes at this level

        struct Curve
        {
            float thresholdDb, ratio, kneeDb;
        };

        // COMP turns the threshold down (from -8 to -40 dB) and the ratio up
        // together. INTENSITY off: a gentle ratio (1.5 to 6) and a wide, soft
        // knee; on: a hard ratio (4 to 20), a narrow knee, and a threshold
        // 6 dB lower, so it grabs earlier as well as harder.
        Curve daybreakCurve (float amount, bool intensity) noexcept
        {
            amount = juce::jlimit (0.0f, 10.0f, amount);
            return { -8.0f - 3.2f * amount - (intensity ? 6.0f : 0.0f),
                     intensity ? 4.0f + 1.6f * amount : 1.5f + 0.45f * amount,
                     intensity ? 3.0f : 10.0f };
        }

        // The gain change (0 or less, in dB) for an input level, with a soft knee.
        float gainFor (float inputDb, const Curve& curve) noexcept
        {
            const auto over = inputDb - curve.thresholdDb;
            const auto slope = 1.0f / curve.ratio - 1.0f;
            if (2.0f * over < -curve.kneeDb)
                return 0.0f;
            if (2.0f * over <= curve.kneeDb)
                return slope * juce::square (over + curve.kneeDb * 0.5f) / (2.0f * curve.kneeDb);
            return slope * over;
        }

        float coefficientFor (double sampleRate, float seconds) noexcept
        {
            return 1.0f - std::exp (-1.0f / (seconds * (float) sampleRate));
        }
    }

    namespace
    {
        // Revival's COMPRESSION fader: down from 0 to -36 dB lowers the
        // threshold to match and raises the ratio from 1:1 (nothing at all)
        // to 4:1, with a 6 dB soft knee. Our own choice of curve: the guide
        // says only that the one fader sets both.
        Curve revivalCurve (float compressionDb) noexcept
        {
            const auto depth = juce::jlimit (0.0f, 1.0f, compressionDb / CompGate::revivalMinDb);
            return { juce::jmin (0.0f, compressionDb), 1.0f + 3.0f * depth, 6.0f };
        }

        // The make-up gives back all it takes from a full-scale peak, so
        // pulling COMPRESSION down brings everything up towards full scale,
        // where the limiter takes over: "gain reduction increases, as does
        // output level".
        float revivalMakeUpDb (const Curve& curve) noexcept { return -gainFor (0.0f, curve); }

        constexpr float gateRatio = 2.0f;           // 1 dB below GATE: 1 dB more down. Gentle.
        constexpr float gateKneeDb = 10.0f;         // so it starts easing a little above GATE
        constexpr float gateRangeDb = 40.0f;        // the most it takes
        constexpr float limiterReleaseSeconds = 0.1f;
    }

    namespace
    {
        // Photon: a fixed threshold at 0 dB after INPUT; each RATIO button with
        // a knee that tightens as the ratio rises (our values).
        Curve photonButton (int ratio) noexcept
        {
            constexpr float ratios[] = { 4.0f, 8.0f, 12.0f, 20.0f };
            constexpr float knees[] = { 8.0f, 6.0f, 4.0f, 3.0f };
            const auto index = juce::jlimit (0, 3, ratio);
            return { 0.0f, ratios[index], knees[index] };
        }
        constexpr int photonAll = 4;
        // ALL: grabs 4 dB earlier at about 12:1, and from 10 dB over the
        // output falls as the input rises (the famous "negative ratio"):
        // the loudest peaks come out below quieter ones. Our curve.
        constexpr float allThresholdDb = -4.0f, allRatio = 12.0f, allTurnDb = 10.0f, allFall = 0.25f;

        // Abyss: PEAK REDUCTION lowers the threshold (0: -6 dB, 100: -48 dB).
        // COMPRESS about 3:1 with a wide knee; LIMIT about 10:1, narrower.
        Curve abyssCurve (float peakReduction, int mode) noexcept
        {
            const auto threshold = -6.0f - 0.42f * juce::jlimit (0.0f, 100.0f, peakReduction);
            return mode == 1 ? Curve { threshold, 10.0f, 6.0f } : Curve { threshold, 3.0f, 12.0f };
        }
    }

    float CompGate::photonAttackSeconds (float knob) noexcept
    {
        // 1 to 7 from 800 us to 20 us, evenly in ratio (as the original's
        // pot sweeps it).
        const auto t = (juce::jlimit (1.0f, 7.0f, knob) - 1.0f) / 6.0f;
        return 0.0008f * std::pow (0.02f / 0.8f, t);
    }

    float CompGate::photonReleaseSeconds (float knob) noexcept
    {
        const auto t = (juce::jlimit (1.0f, 7.0f, knob) - 1.0f) / 6.0f;
        return 1.1f * std::pow (0.05f / 1.1f, t);
    }

    float CompGate::photonCurveDb (float drivenDb, int ratio) noexcept
    {
        if (ratio < photonAll)
            return gainFor (drivenDb, photonButton (ratio));
        const auto over = drivenDb - allThresholdDb;
        if (over <= 0.0f)
            return gainFor (drivenDb, { allThresholdDb, allRatio, 2.0f });
        if (over <= allTurnDb)
            return gainFor (drivenDb, { allThresholdDb, allRatio, 2.0f });
        // Past the turn: the output goes down by allFall dB for each dB more in.
        const auto atTurn = allThresholdDb + allTurnDb / allRatio;
        const auto outDb = atTurn - allFall * (over - allTurnDb);
        return outDb - drivenDb;
    }

    float CompGate::abyssCurveDb (float inputDb, float peakReduction, int mode) noexcept
    {
        return gainFor (inputDb, abyssCurve (peakReduction, mode));
    }

    float CompGate::revivalCurveDb (float inputDb, float compressionDb) noexcept
    {
        const auto curve = revivalCurve (compressionDb);
        return gainFor (inputDb, curve) + revivalMakeUpDb (curve);
    }

    float CompGate::daybreakCurveDb (float inputDb, float amount, bool intensity) noexcept
    {
        return gainFor (inputDb, daybreakCurve (amount, intensity));
    }

    void CompGate::prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 1 };
        gate.prepare (spec);
        compressor.prepare (spec);

        // Fixed ballistics that suit guitar: the gate opens instantly and
        // closes gently so notes don't get chopped; the compressor lets the
        // pick attack through before it clamps down.
        gate.setRatio (10.0f);
        gate.setAttack (1.0f);
        gate.setRelease (120.0f);
        compressor.setAttack (8.0f);
        compressor.setRelease (150.0f);

        makeUpGain.reset (sampleRate, 0.02);
        mix.reset (sampleRate, 0.05);
        daybreakMakeUp.reset (sampleRate, 0.05);
        envelope = sidechainState = 0.0f;
        gateLevel = gateGainDb = sustainedDb = limiterEnvelope = 0.0f;
        compLevelDb = -120.0f;
        revivalThreshold.reset (sampleRate, 0.05);
        revivalCeiling.reset (sampleRate, 0.05);
        revivalThreshold.setCurrentAndTargetValue (-12.0f);
        revivalCeiling.setCurrentAndTargetValue (1.0f);
        photonLevelDb = -120.0f;
        photonColourPeak = 0.0f;
        photonIn.reset (sampleRate, 0.03);
        photonOut.reset (sampleRate, 0.03);
        abyssGain.reset (sampleRate, 0.03);
        photonIn.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (18.0f));
        photonOut.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (-8.0f));
        abyssGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (6.0f));
        abyssFastDb = abyssSlowDb = abyssMemory = abyssPeak = emphasisState = 0.0f;
        bypass.prepare (sampleRate, maxBlockSize);
    }

    void CompGate::process (juce::dsp::AudioBlock<float> monoBlock, const Settings& settings) noexcept
    {
        if (! settings.on)
            gainReductionDb.store (0.0f);

        bypass.process (monoBlock, settings.on, [&] (juce::dsp::AudioBlock<float> block)
        {
            juce::dsp::ProcessContextReplacing<float> context (block);

            if (settings.type == Type::revival)
            {
                // Its own gate and output stage: no noise gate, no LEVEL.
                processRevival (block, settings);
                return;
            }

            if (settings.gateThresholdDb > gateOffDb)
            {
                gate.setThreshold (settings.gateThresholdDb);
                gate.process (context);
            }

            // Photon and Abyss: after the noise gate, with their own OUTPUT / GAIN (no LEVEL).
            if (settings.type == Type::photon || settings.type == Type::abyss)
            {
                if (settings.type == Type::photon)
                    processPhoton (block, settings);
                else
                    processAbyss (block, settings);
                return;
            }

            if (settings.type == Type::daybreak)
            {
                processDaybreak (block, settings);
            }
            else
            {
                // The meter: how much quieter the compressor made this block.
                const auto* samples = block.getChannelPointer (0);
                const auto numSamples = (int) block.getNumSamples();
                double inPower = 0.0, outPower = 0.0;
                for (int i = 0; i < numSamples; ++i)
                    inPower += (double) samples[i] * samples[i];

                compressor.setThreshold (settings.thresholdDb);
                compressor.setRatio (settings.ratio);
                compressor.process (context);

                for (int i = 0; i < numSamples; ++i)
                    outPower += (double) samples[i] * samples[i];
                gainReductionDb.store (inPower > 1.0e-9 ? (float) juce::jmin (0.0, 10.0 * std::log10 ((outPower + 1.0e-12) / inPower)) : 0.0f);
            }

            makeUpGain.setTargetValue (juce::Decibels::decibelsToGain (settings.levelDb));
            makeUpGain.applyGain (block.getChannelPointer (0), (int) block.getNumSamples());
        });
    }

    void CompGate::processDaybreak (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        const auto curve = daybreakCurve (settings.amount, settings.intensity);

        // DETAIL is the attack: at 0 it clamps in half a millisecond and
        // flattens the pick; at 10 it waits 30 ms, so the pick snaps through.
        // INTENSITY also lets go faster, so it grabs every note.
        const auto attackSeconds = 0.0005f * std::pow (60.0f, juce::jlimit (0.0f, 10.0f, settings.detail) / 10.0f);
        const auto attack = coefficientFor (sampleRate, attackSeconds);
        const auto release = coefficientFor (sampleRate, settings.intensity ? 0.09f : 0.25f);
        const auto sidechainCut = coefficientFor (sampleRate, 1.0f / (juce::MathConstants<float>::twoPi * sidechainCutHz));

        // Automatic make-up: give back most of what it takes from a firmly
        // played note, so turning COMP up squeezes rather than just quietens.
        daybreakMakeUp.setTargetValue (juce::Decibels::decibelsToGain (-0.6f * gainFor (makeUpReferenceDb, curve)));
        mix.setTargetValue (juce::jlimit (0.0f, 1.0f, settings.mix));

        auto* samples = block.getChannelPointer (0);
        auto mostReduction = 0.0f;
        for (size_t i = 0; i < block.getNumSamples(); ++i)
        {
            const auto dry = samples[i];

            // The detector hears the guitar without its lowest bass.
            sidechainState += (dry - sidechainState) * sidechainCut;
            const auto detected = std::abs (dry - sidechainState);
            envelope += (detected - envelope) * (detected > envelope ? attack : release);

            const auto reductionDb = gainFor (juce::Decibels::gainToDecibels (envelope, -120.0f), curve);
            mostReduction = juce::jmin (mostReduction, reductionDb);

            const auto compressed = dry * juce::Decibels::decibelsToGain (reductionDb) * daybreakMakeUp.getNextValue();
            const auto mixNow = mix.getNextValue();
            samples[i] = dry * (1.0f - mixNow) + compressed * mixNow;
        }
        gainReductionDb.store (mostReduction);
    }

    void CompGate::processRevival (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        const auto gateOn = settings.revivalGateDb > revivalGateOffDb;
        const auto compressionDb = juce::jlimit (revivalMinDb, 0.0f, settings.revivalCompDb);
        revivalThreshold.setTargetValue (compressionDb);
        revivalCeiling.setTargetValue (juce::Decibels::decibelsToGain (juce::jlimit (revivalMinDb, 0.0f, settings.revivalGainDb)));

        // The gate follows the voice's peaks (the guide sets GATE by the
        // input meter's peaks), held over 50 ms so a low note's waves don't
        // flutter it. It opens in 2 ms and closes over 150 ms, so word
        // endings fade rather than cut.
        const auto gateDetector = std::exp (-1.0f / (0.05f * (float) sampleRate));
        const auto gateOpen = coefficientFor (sampleRate, 0.002f);
        const auto gateClose = coefficientFor (sampleRate, 0.15f);
        // The compressor grabs in 3 ms. It lets go in 60 ms after a short
        // peak, slowing to 400 ms while it's been working for a while, so a
        // held note isn't pumped (our take on a program-dependent release).
        const auto compAttack = coefficientFor (sampleRate, 0.003f);
        const auto sustainedRise = coefficientFor (sampleRate, 0.5f);
        const auto limiterRelease = std::exp (-1.0f / (limiterReleaseSeconds * (float) sampleRate));

        auto* samples = block.getChannelPointer (0);
        auto mostReduction = 0.0f;
        for (size_t i = 0; i < block.getNumSamples(); ++i)
        {
            auto x = samples[i];

            // GATE: downward expansion below it, with a soft knee.
            auto gateTargetDb = 0.0f;
            gateLevel = juce::jmax (std::abs (x), gateLevel * gateDetector);
            if (gateOn)
            {
                const auto levelDb = juce::Decibels::gainToDecibels (gateLevel, -120.0f);
                const auto under = settings.revivalGateDb - levelDb;   // positive below GATE
                const auto slope = gateRatio - 1.0f;
                if (2.0f * under > gateKneeDb)
                    gateTargetDb = -slope * under;
                else if (2.0f * under > -gateKneeDb)
                    gateTargetDb = -slope * juce::square (under + gateKneeDb * 0.5f) / (2.0f * gateKneeDb);
                gateTargetDb = juce::jmax (-gateRangeDb, gateTargetDb);
            }
            gateGainDb += (gateTargetDb - gateGainDb) * (gateTargetDb > gateGainDb ? gateOpen : gateClose);
            x *= juce::Decibels::decibelsToGain (gateGainDb);

            // COMPRESSION, after the gate.
            const auto curve = revivalCurve (revivalThreshold.getNextValue());
            const auto inDb = juce::Decibels::gainToDecibels (std::abs (x), -120.0f);
            const auto release = coefficientFor (sampleRate, 0.06f + 0.34f * juce::jlimit (0.0f, 1.0f, sustainedDb / 6.0f));
            compLevelDb += (inDb - compLevelDb) * (inDb > compLevelDb ? compAttack : release);
            const auto compDb = gainFor (compLevelDb, curve);
            sustainedDb += (-compDb - sustainedDb) * sustainedRise;
            x *= juce::Decibels::decibelsToGain (compDb + revivalMakeUpDb (curve));

            mostReduction = juce::jmin (mostReduction, gateGainDb + compDb);

            // GAIN: a peak limiter at the ceiling. It turns down at once and
            // lets go over 100 ms; no look-ahead, so no latency.
            const auto ceiling = revivalCeiling.getNextValue();
            limiterEnvelope = juce::jmax (std::abs (x), limiterEnvelope * limiterRelease);
            if (limiterEnvelope > ceiling)
                x *= ceiling / limiterEnvelope;

            samples[i] = x;
        }
        gainReductionDb.store (mostReduction);
    }

    void CompGate::processPhoton (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        // The FET: INPUT into a fixed threshold, a fast peak detector at
        // ATTACK and RELEASE, the gain change by RATIO, the FET's colour,
        // then OUTPUT.
        const auto all = settings.photonRatio >= photonAll;
        const auto attack = coefficientFor (sampleRate, photonAttackSeconds (settings.photonAttack) * (all ? 0.7f : 1.0f));
        const auto release = coefficientFor (sampleRate, photonReleaseSeconds (settings.photonRelease) * (all ? 0.7f : 1.0f));
        photonIn.setTargetValue (juce::Decibels::decibelsToGain (juce::jlimit (0.0f, 40.0f, settings.photonInputDb)));
        photonOut.setTargetValue (juce::Decibels::decibelsToGain (settings.photonOutputDb));
        // The colour: drive that grows with the gain reduction (ALL: three
        // times as much). Our curve: gentle until it's working hard.
        const auto colourPerDb = all ? 0.05f : 0.02f;
        const auto colourPeakHold = std::exp (-1.0f / (0.01f * (float) sampleRate));

        auto* samples = block.getChannelPointer (0);
        auto mostReduction = 0.0f;
        for (size_t i = 0; i < block.getNumSamples(); ++i)
        {
            const auto driven = samples[i] * photonIn.getNextValue();
            const auto inDb = juce::Decibels::gainToDecibels (std::abs (driven), -120.0f);
            photonLevelDb += (inDb - photonLevelDb) * (inDb > photonLevelDb ? attack : release);
            const auto reductionDb = photonCurveDb (photonLevelDb, settings.photonRatio);
            mostReduction = juce::jmin (mostReduction, reductionDb);
            auto y = driven * juce::Decibels::decibelsToGain (reductionDb);
            // The colour: the wave measured against its own peak level (held
            // 10 ms, and never below the wave itself, so the start of a note
            // isn't over-scaled), shaped by an amount that grows only with
            // the gain reduction, then put back at its level. Loud or quiet,
            // the grit follows the squeeze.
            photonColourPeak = juce::jmax (std::abs (y), photonColourPeak * colourPeakHold);
            const auto peak = juce::jmax (1.0e-6f, photonColourPeak);
            const auto drive = 0.05f + colourPerDb * -reductionDb;
            y = peak * std::tanh (drive * y / peak) / drive;
            samples[i] = y * photonOut.getNextValue();
        }
        gainReductionDb.store (mostReduction);
    }

    void CompGate::processAbyss (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        // The optical cell: the voice (brighter with EMPHASIS) lights a lamp;
        // the cell darkens in about 10 ms and lets go in two parts, half
        // quickly (60 ms) and half slowly (1 s, up to 5 s after working hard).
        const auto curve = abyssCurve (settings.abyssPeakReduction, settings.abyssMode);
        const auto attack = coefficientFor (sampleRate, 0.01f);
        const auto fastRelease = coefficientFor (sampleRate, 0.06f);
        const auto peakRelease = std::exp (-1.0f / (0.002f * (float) sampleRate));
        const auto memoryRise = coefficientFor (sampleRate, 3.0f), memoryFall = coefficientFor (sampleRate, 10.0f);
        const auto emphasisFilter = coefficientFor (sampleRate, 1.0f / (juce::MathConstants<float>::twoPi * 2000.0f));
        const auto emphasis = 3.0f * juce::jlimit (0.0f, 1.0f, settings.abyssEmphasis);   // up to +12 dB above 2 kHz in the detector
        abyssGain.setTargetValue (juce::Decibels::decibelsToGain (settings.abyssGainDb));

        auto* samples = block.getChannelPointer (0);
        auto mostReduction = 0.0f;
        for (size_t i = 0; i < block.getNumSamples(); ++i)
        {
            const auto x = samples[i];
            emphasisState += (x - emphasisState) * emphasisFilter;
            const auto heard = x + emphasis * (x - emphasisState);   // the highs, louder to the detector
            abyssPeak = juce::jmax (std::abs (heard), abyssPeak * peakRelease);
            const auto wantedDb = gainFor (juce::Decibels::gainToDecibels (abyssPeak, -120.0f), curve);

            const auto slowRelease = coefficientFor (sampleRate, 1.0f + 4.0f * abyssMemory);
            abyssFastDb += (wantedDb - abyssFastDb) * (wantedDb < abyssFastDb ? attack : fastRelease);
            abyssSlowDb += (wantedDb - abyssSlowDb) * (wantedDb < abyssSlowDb ? attack : slowRelease);
            const auto reductionDb = 0.5f * (abyssFastDb + abyssSlowDb);
            const auto working = juce::jlimit (0.0f, 1.0f, -reductionDb / 10.0f);
            abyssMemory += (working - abyssMemory) * (working > abyssMemory ? memoryRise : memoryFall);
            mostReduction = juce::jmin (mostReduction, reductionDb);

            // The valve output stage: a touch of warmth (our amount).
            auto y = x * juce::Decibels::decibelsToGain (reductionDb) * abyssGain.getNextValue();
            y = std::tanh (1.2f * y) / 1.2f;
            samples[i] = y;
        }
        gainReductionDb.store (mostReduction);
    }
}
