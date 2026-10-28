#include "HarmonyModulation.h"

namespace effects
{
    namespace
    {
        // A fixed random value (-1 to 1) for step `n`: the same every time,
        // so a random shape repeats when it's synced and doesn't crawl.
        float randomAt (std::uint32_t n, int seed) noexcept
        {
            auto x = n * 2654435761u + (std::uint32_t) seed * 40503u + 0x9e3779b9u;
            x ^= x >> 15;
            x *= 0x2c1b3c6du;
            x ^= x >> 12;
            x *= 0x297a2d39u;
            x ^= x >> 15;
            return (float) (x & 0xffffff) / (float) 0x7fffff - 1.0f;
        }

        float coefficient (float ms, double sampleRate) noexcept
        {
            return 1.0f - std::exp (-1.0f / juce::jmax (1.0e-3f, ms * 0.001f * (float) sampleRate));
        }
    }

    float HarmonyModulation::shapeValue (int shape, float p, std::uint32_t cycle, int seed) noexcept
    {
        p = juce::jlimit (0.0f, 1.0f, p);
        constexpr auto pi = juce::MathConstants<float>::pi;
        switch (shape)
        {
            case 1:  return p < 0.25f ? 4.0f * p : p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f;   // Triangle
            case 2:  return 2.0f * p - 1.0f;                                                         // Saw Up
            case 3:  return 1.0f - 2.0f * p;                                                         // Saw Down
            case 4:  return p < 0.5f ? 1.0f : -1.0f;                                                 // Square
            case 5:  return p < 0.25f ? 1.0f : -1.0f;                                                // Pulse (a quarter on)
            case 6:  return juce::jmin (3.0f, std::floor (p * 4.0f)) * (2.0f / 3.0f) - 1.0f;         // Stairs: four steps up
            case 7:  return randomAt (cycle * 4u + (std::uint32_t) juce::jmin (3.0f, std::floor (p * 4.0f)), seed);   // Random Steps: four a cycle
            case 8:                                                                                   // Smooth Random: glides between four a cycle
            {
                const auto place = p * 4.0f;
                const auto step = (std::uint32_t) juce::jmin (3.0f, std::floor (place));
                const auto t = 0.5f - 0.5f * std::cos (pi * (place - (float) step));
                const auto a = randomAt (cycle * 4u + step, seed), b = randomAt (cycle * 4u + step + 1u, seed);
                return a + (b - a) * t;
            }
            case 9:  return std::sin (pi * p);                                                       // Bump: 0 up to 1 and back (one way only)
            default: return std::sin (2.0f * pi * p);                                                // Sine
        }
    }

    float HarmonyModulation::modValue (const Mod& mod, float position, std::uint32_t cycle, int seed) noexcept
    {
        // WARP bends the time through the cycle, keeping its length.
        const auto warp = juce::jlimit (minWarp, maxWarp, mod.warp);
        const auto p = std::abs (warp - 1.0f) < 1.0e-4f ? position : std::pow (juce::jlimit (0.0f, 1.0f, position), 1.0f / warp);
        if (mod.type == 1)
        {
            const auto steps = juce::jlimit (2, maxSteps, mod.steps);
            const auto step = juce::jlimit (0, steps - 1, (int) std::floor (p * (float) steps));
            return juce::jlimit (-1.0f, 1.0f, mod.stepValues[(size_t) step] / pitchSpan);
        }
        return shapeValue (mod.shape, p, cycle, seed);
    }

    double HarmonyModulation::unitSeconds (const Mod& mod, double bpm) noexcept
    {
        const auto syncs = (int) std::size (syncBeats);
        if (mod.sync > 0 && mod.sync < syncs)
            return (double) syncBeats[mod.sync] * 60.0 / juce::jmax (20.0, bpm);
        return 1.0 / (double) juce::jlimit (minRateHz, maxRateHz, mod.rateHz);
    }

    void HarmonyModulation::prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        reset();
    }

    void HarmonyModulation::reset()
    {
        modStates = {};
        amEnvelope = 0.0f;
        ptSmoothed = 0.0f;
        ptStarted = false;
        values.fill (0.0f);
        for (auto& shown : shownValues)
            shown = 0.0f;
        for (auto& shown : shownPositions)
            shown = 0.0f;
    }

    void HarmonyModulation::advance (const Settings& settings, const Context& context, const float* voice, int numSamples) noexcept
    {
        const auto seconds = (double) numSamples / sampleRate;

        // M1-M4.
        for (int m = 0; m < numMods; ++m)
        {
            const auto& mod = settings.mods[(size_t) m];
            auto& state = modStates[(size_t) m];
            const auto unit = unitSeconds (mod, context.bpm);
            const auto cycleSeconds = mod.type == 1 ? unit * juce::jlimit (2, maxSteps, mod.steps) : unit;

            // TRIGGER: where the cycle restarts.
            if (mod.trigger == sync && context.songBeats >= 0.0)
                state.position = context.songBeats / (cycleSeconds * context.bpm / 60.0);   // locked to the song
            else if (context.newNote && (mod.trigger == retrigger || (mod.trigger == legato && ! context.heldBefore)))
                state.position = 0.0;

            // PLAY: where that puts it in its cycle.
            auto p = 0.0;
            std::uint32_t cycle = 0;
            switch (mod.play)
            {
                case oneShot: p = juce::jmin (state.position, 0.999999); break;
                case seesaw:
                {
                    const auto u = std::fmod (juce::jmax (0.0, state.position), 2.0);
                    p = u < 1.0 ? u : 2.0 - u;
                    cycle = (std::uint32_t) (state.position / 2.0);
                    break;
                }
                default:
                    p = state.position - std::floor (state.position);
                    cycle = (std::uint32_t) juce::jmax (0.0, std::floor (state.position));
                    break;
            }
            p += (double) juce::jlimit (0.0f, 1.0f, mod.phase);
            p -= std::floor (p);

            const auto raw = modValue (mod, (float) p, cycle, m + 1);
            if (! state.started)
            {
                state.smoothed = raw;
                state.started = true;
            }
            else
            {
                const auto alpha = 1.0f - std::exp (-(float) numSamples / juce::jmax (1.0e-3f, mod.smoothMs * 0.001f * (float) sampleRate));
                state.smoothed += (raw - state.smoothed) * alpha;
            }
            values[(size_t) (m1 + m)] = state.smoothed * juce::jlimit (0.0f, 1.0f, mod.level);
            shownPositions[(size_t) m] = (float) p;

            if (mod.play != hold)
                state.position += seconds / cycleSeconds;
        }

        // AM: how loud the voice is, 0 (-60 dB and below) to 1 (full scale), times LEVEL.
        {
            const auto attack = coefficient (settings.am.attackMs, sampleRate), release = coefficient (settings.am.releaseMs, sampleRate);
            for (int i = 0; i < numSamples; ++i)
            {
                const auto x = voice != nullptr ? std::abs (voice[i]) : 0.0f;
                amEnvelope += (x - amEnvelope) * (x > amEnvelope ? attack : release);
            }
            const auto db = juce::Decibels::gainToDecibels (amEnvelope, -60.0f);
            values[am] = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f) * juce::jlimit (0.0f, 2.0f, settings.am.level);
        }

        // PT: the pitch sung, 0 at middle C (plus OFFSET), 1 two octaves up; held between notes.
        {
            const auto& pitch = settings.pt;
            auto target = ptStarted ? ptSmoothed : juce::jlimit (-1.0f, 1.0f, pitch.offset);
            if (context.sungNote >= 0.0f)
            {
                target = juce::jlimit (-1.0f, 1.0f, (context.sungNote - 60.0f) / pitchSpan + pitch.offset);
                ptStarted = true;
            }
            const auto alpha = 1.0f - std::exp (-(float) numSamples / juce::jmax (1.0e-3f, pitch.smoothMs * 0.001f * (float) sampleRate));
            ptSmoothed += (target - ptSmoothed) * alpha;
            values[pt] = ptSmoothed * juce::jlimit (0.0f, 2.0f, pitch.level);
        }

        for (size_t s = 0; s < values.size(); ++s)
            shownValues[s] = values[s];
    }
}
