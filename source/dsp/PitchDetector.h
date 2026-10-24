#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

// Finds the pitch of a guitar note, for the tuner.
//
// Uses the YIN method (de Cheveigné & Kawahara, 2002): it looks for the
// delay at which the signal best matches itself, which is one period of the
// note. YIN is the usual choice for tuners because, unlike simple peak
// counting, it isn't fooled by a guitar's strong overtones into reading the
// note an octave high.
//
// To keep it cheap, the audio is first reduced to a quarter of the sample
// rate: a guitar's fundamental is at most about 1.3 kHz, so nothing needed
// is lost.
class PitchDetector
{
public:
    struct Result
    {
        double frequency = 0.0;
        double clarity = 0.0;   // 0 (noise) to 1 (a perfectly clean note)
    };

    static constexpr double lowestHz = 25.0;     // below a 5-string bass's low B
    static constexpr double highestHz = 1500.0;

    // The range of notes to look for. The tuner uses the full range above;
    // HARMONY a voice's (a narrower range is cheaper: a shorter window).
    explicit PitchDetector (double lowestToFind = lowestHz, double highestToFind = highestHz)
        : lowHz (lowestToFind), highHz (highestToFind) {}

    // Message thread, before use. Allocates.
    void prepare (double sampleRate);

    // Detects the pitch of the most recent audio. `samples` should hold at
    // least windowSizeAtHostRate() samples; returns nothing if there's no
    // clear note (silence, noise, or two strings at once).
    std::optional<Result> detect (const float* samples, int numSamples);

    int windowSizeAtHostRate() const noexcept { return windowSize * decimation; }

    // Note name and how far off it is, for the display.
    struct Note
    {
        juce::String name;      // e.g. "A", "F#"
        int octave = 0;         // A4 = 440 Hz
        double cents = 0.0;     // -50 to +50
    };
    static Note toNote (double frequency, double referenceA4 = 440.0);

private:
    double refineAtHostRate (const float* start, double roughPeriod) const;

    double lowHz, highHz;
    double sampleRate = 48000.0;
    int decimation = 4;
    int windowSize = 2048;   // at the reduced rate
    std::vector<float> reduced, difference;
};
