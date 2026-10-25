#include "Drive.h"

namespace effects
{
    namespace
    {
        constexpr float maxDriveDb = 40.0f;
        constexpr float lowCutHz = 120.0f;

        // Shifting the curve slightly off-centre clips the two halves of the
        // wave differently, which adds the even harmonics that make tube-style
        // overdrive sound warm rather than fizzy.
        constexpr float bias = 0.15f;
        const float biasOffset = std::tanh (bias);

        // Turning the drive up pushes the clipped signal towards full scale.
        // Taking back half the drive gain keeps the pedal at roughly the
        // guitar's own level at any drive setting, so Level 0 dB is unity
        // and the Level knob is a real boost or cut into the amp.
        constexpr float driveCompensation = 0.5f;

        // TS808 component values: the op-amp's gain is 1 + (51k + DRIVE pot)
        // / 4.7k above the 4.7k / 0.047 uF corner (720 Hz); the 51 pF across
        // the feedback softens the very top; the 1k / 0.22 uF after the
        // diodes is the fixed treble cut the TONE knob works against.
        constexpr float screamerLowCutHz = 720.0f;
        constexpr float feedbackR = 51.0e3f, drivePotR = 500.0e3f, groundR = 4.7e3f, feedbackC = 51.0e-12f;
        constexpr float toneCornerHz = 723.0f;
        constexpr float diodeVolts = 0.6f;         // about where a silicon diode starts conducting
        constexpr float screamerOutputScale = 0.35f;   // so LEVEL 0 dB is roughly the guitar's own level
        constexpr float muffOutputScale = 0.82f;       // the same for the fuzzes
        constexpr float faceOutputScale = 0.29f;

        float onePoleCoefficient (double rate, float hz)
        {
            return 1.0f - std::exp (-juce::MathConstants<float>::twoPi * juce::jmin (hz, (float) rate * 0.45f) / (float) rate);
        }
    }

    Drive::Drive()
        : oversampling (1, 3, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true),   // 3 stages = 8x, steep filters
          screamerOversampling (2, 3, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true)
    {
    }

    float Drive::diodeClip (float x) noexcept
    {
        // Diodes in the feedback loop round the peaks off gradually rather
        // than flattening them (softer than tanh), levelling out at about 0.6 V.
        const auto ratio = std::abs (x) / diodeVolts;
        return x / std::pow (1.0f + std::pow (ratio, 2.5f), 0.4f);
    }

    float Drive::shape (float x) noexcept
    {
        return std::tanh (x + bias) - biasOffset;
    }

    void Drive::prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 1 };

        oversampling.initProcessing ((size_t) maxBlockSize);
        oversampling.reset();
        screamerOversampling.initProcessing ((size_t) maxBlockSize);
        screamerOversampling.reset();
        screamerPaths.setSize (2, maxBlockSize);
        screamerLowCut.coefficients = juce::dsp::IIR::Coefficients<float>::makeFirstOrderHighPass (sampleRate, screamerLowCutHz);
        screamerLowCut.prepare (spec);
        clipLowPassState = toneState = outputCouplingState = 0.0f;
        screamerGain.reset (sampleRate, 0.02);
        screamerTone.reset (sampleRate, 0.02);
        screamerLevel.reset (sampleRate, 0.02);
        fuzzStates.fill (0.0f);
        fuzzGain.reset (sampleRate, 0.02);
        fuzzTone.reset (sampleRate, 0.02);
        fuzzLevel.reset (sampleRate, 0.02);

        lowCut.coefficients = juce::dsp::IIR::Coefficients<float>::makeFirstOrderHighPass (sampleRate, lowCutHz);
        toneFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, 4000.0f);
        lowCut.prepare (spec);
        toneFilter.prepare (spec);
        lastTone = -1.0f;

        inputGain.reset (sampleRate, 0.02);
        outputGain.reset (sampleRate, 0.02);
        bypass.prepare (sampleRate, maxBlockSize);
    }

    void Drive::process (juce::dsp::AudioBlock<float> monoBlock, const Settings& settings) noexcept
    {
        bypass.process (monoBlock, settings.on, [&] (juce::dsp::AudioBlock<float> block)
        {
            // Switching model: start the other model's filters from silence
            // rather than from whatever they last held.
            if (settings.model != lastModel)
            {
                lastModel = settings.model;
                screamerLowCut.reset();
                screamerOversampling.reset();
                oversampling.reset();
                lowCut.reset();
                toneFilter.reset();
                clipLowPassState = toneState = outputCouplingState = 0.0f;
                fuzzStates.fill (0.0f);
            }

            switch (settings.model)
            {
                case Model::screamer: processScreamer (block, settings); break;
                case Model::muff:     processMuff (block, settings); break;
                case Model::face:     processFace (block, settings); break;
                case Model::classic:  processClassic (block, settings); break;
                case Model::mars:     break;   // run by effects::Mars instead
            }
        });
    }

    void Drive::processClassic (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        // Tone: a high cut from 1 kHz (dark) to 12 kHz (bright). Retuned
        // in place only when the knob moves: no allocation.
        if (! juce::approximatelyEqual (settings.tone, lastTone))
        {
            *toneFilter.coefficients = juce::dsp::IIR::ArrayCoefficients<float>::makeLowPass (
                sampleRate, knobToFrequency (settings.tone, 1000.0f, 12000.0f), 0.707f);
            lastTone = settings.tone;
        }

        juce::dsp::ProcessContextReplacing<float> context (block);
        lowCut.process (context);

        const auto driveDb = maxDriveDb * juce::jlimit (0.0f, 10.0f, settings.drive) / 10.0f;
        inputGain.setTargetValue (juce::Decibels::decibelsToGain (driveDb));
        inputGain.applyGain (block.getChannelPointer (0), (int) block.getNumSamples());

        auto upsampled = oversampling.processSamplesUp (block);
        auto* samples = upsampled.getChannelPointer (0);
        for (size_t i = 0; i < upsampled.getNumSamples(); ++i)
            samples[i] = shape (samples[i]);
        oversampling.processSamplesDown (block);

        toneFilter.process (context);

        outputGain.setTargetValue (juce::Decibels::decibelsToGain (settings.levelDb - driveDb * driveCompensation));
        outputGain.applyGain (block.getChannelPointer (0), (int) block.getNumSamples());
    }

    void Drive::processScreamer (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        const auto numSamples = (int) block.getNumSamples();
        auto* samples = block.getChannelPointer (0);

        // DRIVE is a 500k audio-taper (logarithmic) pot, as on the pedal.
        const auto knob = juce::jlimit (0.0f, 10.0f, settings.drive) / 10.0f;
        const auto potR = drivePotR * (std::pow (10.0f, 2.0f * knob) - 1.0f) / 99.0f;
        screamerGain.setTargetValue ((feedbackR + potR) / groundR);   // the part above unity: what reaches the diodes
        const auto feedbackHz = 1.0f / (juce::MathConstants<float>::twoPi * (feedbackR + potR) * feedbackC);

        // Channel 0: the clean guitar. Channel 1: the low-cut, amplified
        // signal the diodes will clip.
        screamerPaths.copyFrom (0, 0, samples, numSamples);
        screamerPaths.copyFrom (1, 0, samples, numSamples);
        auto paths = juce::dsp::AudioBlock<float> (screamerPaths).getSubBlock (0, (size_t) numSamples);
        auto clipPathBlock = paths.getSingleChannelBlock (1);
        juce::dsp::ProcessContextReplacing<float> clipContext (clipPathBlock);
        screamerLowCut.process (clipContext);
        auto* clipPath = paths.getChannelPointer (1);
        for (int i = 0; i < numSamples; ++i)
            clipPath[i] *= screamerGain.getNextValue();

        // At 8x the rate: soften the top (51 pF), clip, and add to the clean guitar.
        auto upsampled = screamerOversampling.processSamplesUp (paths);
        const auto fastRate = sampleRate * (double) screamerOversampling.getOversamplingFactor();
        const auto softening = onePoleCoefficient (fastRate, feedbackHz);
        auto* clean = upsampled.getChannelPointer (0);
        auto* driven = upsampled.getChannelPointer (1);
        for (size_t i = 0; i < upsampled.getNumSamples(); ++i)
        {
            clipLowPassState += (driven[i] - clipLowPassState) * softening;
            clean[i] += diodeClip (clipLowPassState);
        }
        screamerOversampling.processSamplesDown (paths);

        // TONE: the fixed 723 Hz treble cut, with the TONE stage adding the
        // treble back: from a little (dark) to a little more than flat (bright).
        const auto toneCut = onePoleCoefficient (sampleRate, toneCornerHz);
        const auto coupling = onePoleCoefficient (sampleRate, 20.0f);   // the output capacitor: blocks any DC from the clipping
        screamerTone.setTargetValue (0.03f + 1.37f * std::pow (juce::jlimit (0.0f, 10.0f, settings.tone) / 10.0f, 1.5f));
        screamerLevel.setTargetValue (screamerOutputScale * juce::Decibels::decibelsToGain (settings.levelDb));

        const auto* summed = paths.getChannelPointer (0);
        for (int i = 0; i < numSamples; ++i)
        {
            toneState += (summed[i] - toneState) * toneCut;
            const auto toned = toneState + screamerTone.getNextValue() * (summed[i] - toneState);
            outputCouplingState += (toned - outputCouplingState) * coupling;
            samples[i] = (toned - outputCouplingState) * screamerLevel.getNextValue();
        }
    }

    void Drive::processMuff (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        const auto numSamples = (int) block.getNumSamples();
        auto* samples = block.getChannelPointer (0);
        auto& [inputCut, stage1Top, coupling, stage2Top, toneLow, toneHigh, outputCoupling, unused] = fuzzStates;
        juce::ignoreUnused (unused);

        // SUSTAIN: each stage's gain, from about 12 to 34 dB.
        const auto knob = juce::jlimit (0.0f, 10.0f, settings.drive) / 10.0f;
        fuzzGain.setTargetValue (juce::Decibels::decibelsToGain (12.0f + 22.0f * knob));

        // The input coupling: a low cut at 100 Hz, so low notes don't flub.
        const auto inputCutCoefficient = onePoleCoefficient (sampleRate, 100.0f);
        for (int i = 0; i < numSamples; ++i)
        {
            inputCut += (samples[i] - inputCut) * inputCutCoefficient;
            samples[i] -= inputCut;
        }

        // At 8x the rate: two stages of gain and diode clipping, each with its
        // top softened (as the transistors' capacitors do), coupled between
        // them by a low cut that keeps the second stage from farting out.
        auto upsampled = oversampling.processSamplesUp (block);
        const auto fastRate = sampleRate * (double) oversampling.getOversamplingFactor();
        const auto top1 = onePoleCoefficient (fastRate, 5000.0f), top2 = onePoleCoefficient (fastRate, 3500.0f);
        const auto couplingCut = onePoleCoefficient (fastRate, 80.0f);
        auto* fast = upsampled.getChannelPointer (0);
        for (size_t i = 0; i < upsampled.getNumSamples(); ++i)
        {
            const auto gain = fuzzGain.getNextValue();
            stage1Top += (fast[i] * gain - stage1Top) * top1;
            auto x = diodeClip (stage1Top);
            coupling += (x - coupling) * couplingCut;
            x = (x - coupling) * gain;
            stage2Top += (x - stage2Top) * top2;
            fast[i] = diodeClip (stage2Top);
        }
        oversampling.processSamplesDown (block);

        // The tone stack: a low-pass (400 Hz) and a high-pass (1.1 kHz) blended
        // by TONE, so it runs from warm and woolly through a mid scoop to bright.
        const auto lowCoefficient = onePoleCoefficient (sampleRate, 400.0f), highCoefficient = onePoleCoefficient (sampleRate, 1100.0f);
        const auto outputCouplingCoefficient = onePoleCoefficient (sampleRate, 20.0f);
        fuzzTone.setTargetValue (juce::jlimit (0.0f, 10.0f, settings.tone) / 10.0f);
        fuzzLevel.setTargetValue (muffOutputScale * juce::Decibels::decibelsToGain (settings.levelDb));
        for (int i = 0; i < numSamples; ++i)
        {
            toneLow += (samples[i] - toneLow) * lowCoefficient;
            toneHigh += (samples[i] - toneHigh) * highCoefficient;
            const auto tone = fuzzTone.getNextValue();
            const auto mixed = toneLow * (1.0f - tone) + (samples[i] - toneHigh) * tone * 1.6f;   // the high path is the quieter one
            outputCoupling += (mixed - outputCoupling) * outputCouplingCoefficient;
            samples[i] = (mixed - outputCoupling) * fuzzLevel.getNextValue();
        }
    }

    void Drive::processFace (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept
    {
        const auto numSamples = (int) block.getNumSamples();
        auto* samples = block.getChannelPointer (0);
        auto& [loading, inputCut, transistorTop, outputCoupling, toneState2, unused1, unused2, unused3] = fuzzStates;
        juce::ignoreUnused (unused1, unused2, unused3);

        // FUZZ: the gain into the clipping, from about 8 to 42 dB.
        const auto knob = juce::jlimit (0.0f, 10.0f, settings.drive) / 10.0f;
        fuzzGain.setTargetValue (juce::Decibels::decibelsToGain (8.0f + 34.0f * knob));

        // The Fuzz Face's low input impedance loads the pickup: a soft treble
        // cut before it even starts. And a low cut at 40 Hz (its input capacitor).
        const auto loadingCoefficient = onePoleCoefficient (sampleRate, 6000.0f);
        const auto inputCutCoefficient = onePoleCoefficient (sampleRate, 40.0f);
        for (int i = 0; i < numSamples; ++i)
        {
            loading += (samples[i] - loading) * loadingCoefficient;
            inputCut += (loading - inputCut) * inputCutCoefficient;
            samples[i] = loading - inputCut;
        }

        // At 8x the rate: gain, then a clip that's softer on top than below
        // (the transistors saturate one way and cut off the other), and the
        // transistors' own treble roll-off.
        auto upsampled = oversampling.processSamplesUp (block);
        const auto fastRate = sampleRate * (double) oversampling.getOversamplingFactor();
        const auto topCoefficient = onePoleCoefficient (fastRate, 7000.0f);
        auto* fast = upsampled.getChannelPointer (0);
        for (size_t i = 0; i < upsampled.getNumSamples(); ++i)
        {
            const auto x = fast[i] * fuzzGain.getNextValue();
            const auto clipped = x >= 0.0f ? std::tanh (x) : 0.6f * std::tanh (x / 0.6f);
            transistorTop += (clipped - transistorTop) * topCoefficient;
            fast[i] = transistorTop;
        }
        oversampling.processSamplesDown (block);

        // TONE: a treble cut from 2.5 kHz (dark) to 12 kHz (open), after the
        // output capacitor that removes the DC the uneven clipping leaves.
        const auto couplingCoefficient = onePoleCoefficient (sampleRate, 20.0f);
        fuzzTone.setTargetValue (knobToFrequency (settings.tone, 2500.0f, 12000.0f));
        fuzzLevel.setTargetValue (faceOutputScale * juce::Decibels::decibelsToGain (settings.levelDb));
        for (int i = 0; i < numSamples; ++i)
        {
            outputCoupling += (samples[i] - outputCoupling) * couplingCoefficient;
            const auto coupled = samples[i] - outputCoupling;
            toneState2 += (coupled - toneState2) * onePoleCoefficient (sampleRate, fuzzTone.getNextValue());
            samples[i] = toneState2 * fuzzLevel.getNextValue();
        }
    }
}
