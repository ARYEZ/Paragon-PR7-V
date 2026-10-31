#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>

// Keys and scales for HARMONY: which notes are "in" a song, and how a
// harmony voice's note is corrected into them. Pure functions, no state.
namespace Scales
{
    // Saved by number: only ever add to the end.
    inline const juce::StringArray keyNames { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };

    enum class Scale { major, naturalMinor, harmonicMinor, melodicMinor, dorian, phrygian, lydian, mixolydian, locrian,
                       majorPentatonic, minorPentatonic, blues, chromatic, custom };
    inline const juce::StringArray scaleNames { "Major", "Natural Minor", "Harmonic Minor", "Melodic Minor", "Dorian", "Phrygian",
                                                "Lydian", "Mixolydian", "Locrian", "Major Pentatonic", "Minor Pentatonic", "Blues",
                                                "Chromatic", "Custom" };

    // SCALE Custom (the Note Map Editor's Scale Editor, inspired by Waves
    // Harmony's): for each note above the key, 0 = in the scale, +1 = goes
    // up to the next note that is, -1 = down to the next one below.
    using Custom = std::array<int, 12>;

    // The notes in each scale, as semitones above its key note.
    inline std::array<bool, 12> notesIn (Scale scale) noexcept
    {
        const auto make = [] (std::initializer_list<int> steps)
        {
            std::array<bool, 12> in {};
            for (const auto step : steps)
                in[(size_t) step] = true;
            return in;
        };
        switch (scale)
        {
            case Scale::major:            return make ({ 0, 2, 4, 5, 7, 9, 11 });
            case Scale::naturalMinor:     return make ({ 0, 2, 3, 5, 7, 8, 10 });
            case Scale::harmonicMinor:    return make ({ 0, 2, 3, 5, 7, 8, 11 });
            case Scale::melodicMinor:     return make ({ 0, 2, 3, 5, 7, 9, 11 });
            case Scale::dorian:           return make ({ 0, 2, 3, 5, 7, 9, 10 });
            case Scale::phrygian:         return make ({ 0, 1, 3, 5, 7, 8, 10 });
            case Scale::lydian:           return make ({ 0, 2, 4, 6, 7, 9, 11 });
            case Scale::mixolydian:       return make ({ 0, 2, 4, 5, 7, 9, 10 });
            case Scale::locrian:          return make ({ 0, 1, 3, 5, 6, 8, 10 });
            case Scale::majorPentatonic:  return make ({ 0, 2, 4, 7, 9 });
            case Scale::minorPentatonic:  return make ({ 0, 3, 5, 7, 10 });
            case Scale::blues:            return make ({ 0, 3, 5, 6, 7, 10 });
            case Scale::chromatic:        break;
            case Scale::custom:           break;   // see the Custom overloads below
        }
        return make ({ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 });
    }

    inline int aboveKey (int midiNote, int key) noexcept { return ((midiNote - key) % 12 + 12) % 12; }

    inline bool isIn (int midiNote, int key, Scale scale, const Custom* custom = nullptr) noexcept
    {
        if (scale == Scale::custom)
            return custom == nullptr || (*custom)[(size_t) aboveKey (midiNote, key)] == 0;
        return notesIn (scale)[(size_t) aboveKey (midiNote, key)];
    }

    // A Custom scale's note for `midi`: itself if it's in, else the next
    // in-scale note in its box's direction (or itself if none is in).
    inline int customNote (double midi, int key, const Custom& custom) noexcept
    {
        const auto centre = (int) std::lround (midi);
        const auto direction = custom[(size_t) aboveKey (centre, key)];
        if (direction == 0)
            return centre;
        for (int distance = 1; distance <= 12; ++distance)
        {
            const auto candidate = centre + (direction > 0 ? distance : -distance);
            if (custom[(size_t) aboveKey (candidate, key)] == 0)
                return candidate;
        }
        return centre;
    }

    // The nearest note in the scale (MIDI note numbers; fractional input).
    inline int nearest (double midi, int key, Scale scale, const Custom* custom = nullptr) noexcept
    {
        if (scale == Scale::custom)
            return custom != nullptr ? customNote (midi, key, *custom) : (int) std::lround (midi);
        const auto centre = (int) std::lround (midi);
        for (int distance = 0; distance < 12; ++distance)
        {
            // Of two equally near notes, the lower.
            const auto below = centre - distance, above = centre + distance;
            const auto belowIn = isIn (below, key, scale), aboveIn = isIn (above, key, scale);
            if (belowIn && aboveIn)
                return std::abs (midi - below) <= std::abs (midi - above) ? below : above;
            if (belowIn)
                return below;
            if (aboveIn)
                return above;
        }
        return centre;
    }

    // What kind of interval a number of semitones is, as a musician names it
    // (a second, a third...): 1 and 2 are seconds, 3 and 4 thirds, and so on.
    // The tritone (6) counts as neither a fourth nor a fifth: -1.
    inline int intervalKind (int semitones) noexcept
    {
        constexpr int kinds[] = { 0, 1, 1, 2, 2, 3, -1, 4, 5, 5, 6, 6 };
        const auto octaves = (int) std::floor (semitones / 12.0);
        return kinds[semitones - 12 * octaves] + 7 * octaves;
    }

    // A harmony note `interval` semitones from `sourceNote`, corrected into
    // the scale. When that exact note isn't in the scale, the in-scale note
    // that keeps the interval's kind wins: a "+4" (a major third) on a note
    // whose major third isn't in the key becomes its minor third, as a
    // singer would sing it, and a "+3" its major third. Otherwise, the
    // nearest in-scale note (the lower, if two are as near).
    inline int harmonyNote (int sourceNote, int interval, int key, Scale scale, const Custom* custom = nullptr) noexcept
    {
        const auto target = sourceNote + interval;
        if (scale == Scale::custom)   // the Scale Editor's arrows decide
            return custom != nullptr ? customNote (target, key, *custom) : target;
        if (isIn (target, key, scale))
            return target;

        const auto kind = intervalKind (interval);
        for (int distance = 1; distance < 12; ++distance)
        {
            const auto below = target - distance, above = target + distance;
            const auto belowIn = isIn (below, key, scale), aboveIn = isIn (above, key, scale);
            if (belowIn && aboveIn)
            {
                if (intervalKind (above - sourceNote) == kind && intervalKind (below - sourceNote) != kind)
                    return above;
                if (kind < 0)   // a tritone: up to the fifth rather than down to the fourth
                    return above;
                return below;
            }
            if (belowIn)
                return below;
            if (aboveIn)
                return above;
        }
        return target;
    }

    inline double midiFromHz (double hz) noexcept { return 69.0 + 12.0 * std::log2 (hz / 440.0); }
    inline double hzFromMidi (double midi) noexcept { return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0); }
}
