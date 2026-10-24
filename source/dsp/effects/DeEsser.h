#pragma once

#include "EffectUtils.h"

namespace effects
{
    // The de-esser in DYNAMICS, after the compressor (whose make-up gain
    // brings esses up). Our own split-band design, as studio de-essers work:
    //
    // Esses are listened for above FREQUENCY. While that part dominates the
    // sound, as it does in an "s", "sh" or "t" and never in a vowel, the top
    // of the voice is turned down, by up to AMOUNT dB. The part turned down
    // starts an octave below FREQUENCY (a steep 24 dB-per-octave split), so
    // the whole of each ess is in it, not just its upper half. Judging the highs
    // against the whole voice, rather than by their level alone, means it
    // reacts the same whether you sing softly or belt.
    //
    // The split is complementary: the two parts add back up to exactly the
    // input, so while nothing is being turned down the voice comes through
    // untouched, bit for bit.
    //
    // LISTEN plays only what's being taken away: the esses, and silence in
    // between. It's the quickest way to set FREQUENCY: sweep it until LISTEN
    // has the esses and none of the voice.
    class DeEsser
    {
    public:
        struct Settings
        {
            bool on = false;
            float frequencyHz = 6000.0f;
            float amountDb = 6.0f;     // the most the esses are turned down
            bool listen = false;
        };

        // How far the highs must stand out before it acts: within this many
        // dB of the whole voice's level.
        static constexpr float sibilanceThresholdDb = -15.0f;

        void prepare (double sampleRate, int maxBlockSize);
        void process (juce::dsp::AudioBlock<float> monoBlock, const Settings& settings) noexcept;

        // How far the esses were turned down in the last block (dB, 0 or less).
        float getReductionDb() const noexcept { return reductionDb.load(); }

    private:
        double sampleRate = 48000.0;
        juce::dsp::StateVariableTPTFilter<float> split, split2;   // two low-passes in a row: the "voice" part
        juce::dsp::StateVariableTPTFilter<float> detector;    // high-pass: what the esses are judged by
        float highEnvelope = 0.0f, fullEnvelope = 0.0f;
        float reductionNowDb = 0.0f;                          // smoothed
        float attack = 0.0f, release = 0.0f, gainAttack = 0.0f, gainRelease = 0.0f;
        juce::SmoothedValue<float> frequency, listenAmount;
        BypassFade bypass;
        std::atomic<float> reductionDb { 0.0f };
    };
}
