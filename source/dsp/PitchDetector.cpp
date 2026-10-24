#include "PitchDetector.h"

#include <cmath>

namespace
{
    // YIN's threshold: the first dip in the (normalised) difference below
    // this is taken as the period. 0.15 is the value the YIN paper found best.
    constexpr float threshold = 0.15f;
}

void PitchDetector::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;

    // Reduce to roughly 12 kHz whatever the host rate (44.1/48 -> /4, 96 -> /8).
    decimation = juce::jmax (1, (int) std::round (sampleRate / 12000.0));

    // The window has to hold at least two periods of the lowest note.
    const auto reducedRate = sampleRate / decimation;
    windowSize = juce::nextPowerOfTwo ((int) std::ceil (2.0 * reducedRate / lowHz));

    reduced.assign ((size_t) windowSize, 0.0f);
    difference.assign ((size_t) windowSize / 2, 0.0f);
}

std::optional<PitchDetector::Result> PitchDetector::detect (const float* samples, int numSamples)
{
    if (numSamples < windowSizeAtHostRate())
        return std::nullopt;

    // Use the newest audio. Averaging each group of `decimation` samples is
    // a simple low-pass that also does the rate reduction.
    const auto* start = samples + (numSamples - windowSizeAtHostRate());
    float peak = 0.0f;
    for (int i = 0; i < windowSize; ++i)
    {
        float sum = 0.0f;
        for (int j = 0; j < decimation; ++j)
            sum += start[i * decimation + j];
        reduced[(size_t) i] = sum / (float) decimation;
        peak = std::max (peak, std::abs (reduced[(size_t) i]));
    }

    if (peak < 1.0e-3f)   // about -60 dB: nothing playing
        return std::nullopt;

    const auto reducedRate = sampleRate / decimation;
    const auto minLag = juce::jmax (2, (int) std::floor (reducedRate / highHz));
    const auto maxLag = juce::jmin ((int) difference.size() - 1, (int) std::ceil (reducedRate / lowHz));
    const auto length = windowSize / 2;

    // Step 1-2: difference function d(lag) = sum (x[i] - x[i+lag])^2.
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        float sum = 0.0f;
        for (int i = 0; i < length; ++i)
        {
            const auto delta = reduced[(size_t) i] - reduced[(size_t) (i + lag)];
            sum += delta * delta;
        }
        difference[(size_t) lag] = sum;
    }

    // Step 3: cumulative mean normalised difference, so a perfect match is 0
    // and "no better than average" is about 1.
    difference[0] = 1.0f;
    float runningSum = 0.0f;
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        runningSum += difference[(size_t) lag];
        difference[(size_t) lag] = runningSum > 0.0f ? difference[(size_t) lag] * (float) lag / runningSum : 1.0f;
    }

    // Step 4: the first dip below the threshold, followed to its lowest point.
    int bestLag = -1;
    for (int lag = minLag; lag < maxLag; ++lag)
    {
        if (difference[(size_t) lag] < threshold)
        {
            while (lag + 1 < maxLag && difference[(size_t) (lag + 1)] < difference[(size_t) lag])
                ++lag;
            bestLag = lag;
            break;
        }
    }

    if (bestLag < 0)
        return std::nullopt;

    // Step 5: refine between samples with a parabola through the three points
    // around the dip, for accuracy well under a cent.
    const auto before = difference[(size_t) (bestLag - 1)];
    const auto at = difference[(size_t) bestLag];
    const auto after = difference[(size_t) (bestLag + 1)];
    const auto curvature = before - 2.0f * at + after;
    const auto shift = std::abs (curvature) > 1.0e-9f ? 0.5f * (before - after) / curvature : 0.0f;
    const auto period = (double) bestLag + (double) juce::jlimit (-1.0f, 1.0f, shift);

    return Result { sampleRate / refineAtHostRate (start, period * decimation), juce::jlimit (0.0, 1.0, 1.0 - (double) at) };
}

// High notes have periods of only ~10 samples at the reduced rate, too few
// for the parabola to be accurate to a cent. So with the period now known
// roughly, search the few lags around it again using the full-rate audio,
// where the same note spans ~4-8x more samples.
double PitchDetector::refineAtHostRate (const float* start, double roughPeriod) const
{
    const auto length = windowSizeAtHostRate() / 2;
    const auto centre = (int) std::round (roughPeriod);
    const auto first = juce::jmax (2, centre - decimation - 1);
    const auto last = juce::jmin (windowSizeAtHostRate() - length - 1, centre + decimation + 1);

    auto differenceAt = [&] (int lag)
    {
        double sum = 0.0;
        for (int i = 0; i < length; ++i)
        {
            const auto delta = (double) start[i] - (double) start[i + lag];
            sum += delta * delta;
        }
        return sum;
    };

    if (last - first < 2)
        return roughPeriod;

    int bestLag = first + 1;
    auto best = differenceAt (bestLag);
    for (int lag = first + 2; lag < last; ++lag)
        if (const auto value = differenceAt (lag); value < best)
        {
            best = value;
            bestLag = lag;
        }

    const auto before = differenceAt (bestLag - 1);
    const auto after = differenceAt (bestLag + 1);
    const auto curvature = before - 2.0 * best + after;
    const auto shift = curvature > 0.0 ? 0.5 * (before - after) / curvature : 0.0;
    return (double) bestLag + juce::jlimit (-1.0, 1.0, shift);
}

PitchDetector::Note PitchDetector::toNote (double frequency, double referenceA4)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    // MIDI note numbers: A4 = 69. Round to the nearest note, keep the rest as cents.
    const auto midi = 69.0 + 12.0 * std::log2 (frequency / referenceA4);
    const auto nearest = (int) std::round (midi);
    const auto index = ((nearest % 12) + 12) % 12;
    return { names[index], nearest / 12 - 1, (midi - nearest) * 100.0 };
}
