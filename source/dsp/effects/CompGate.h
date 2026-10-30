#pragma once

#include "EffectUtils.h"

namespace effects
{
    // Footswitch 1: noise gate then compressor, before the amp.
    // The gate comes first so the compressor doesn't bring the noise back up.
    // Two compressors:
    //
    //   classic   threshold and ratio, as studio compressors have
    //   daybreak  a modern pedal compressor, laid out like the boutique
    //             ones metal players use: COMP (how much, with automatic
    //             make-up gain), DETAIL (how much pick attack gets through
    //             before it clamps), MIX (parallel compression: the
    //             squashed sound blended with the dry guitar) and INTENSITY
    //             (off: smooth and gentle; on: fast and hard). Its detector
    //             ignores the lowest bass, so low palm mutes don't make it pump.
    //   revival  a vocal leveller in three moves, inspired by Waves
    //             Renaissance Vox (the owner's reference, from its user
    //             guide): our own design. GATE, a gentle downward expander
    //             with a soft knee (it eases noise down rather than
    //             chopping); COMPRESSION, one fader that lowers the
    //             threshold and raises the ratio together, with automatic
    //             make-up, so pulling it down makes the voice louder and
    //             more even; GAIN, the ceiling of a peak limiter after it,
    //             so however far COMPRESSION goes, nothing passes GAIN.
    //             Gate, then compressor, then limiter; no look-ahead, so no
    //             latency. It replaces the noise gate and LEVEL.
    //
    // Added in PR7-V 0.41.0, two classic studio compressors (the owner
    // asked for them by their originals; named our own way), from the
    // originals' published specifications, our design where they don't say:
    //
    //   snap      inspired by the 1176 FET limiting amplifier. INPUT drives
    //             the voice into a fixed threshold (more INPUT, more
    //             compression), OUTPUT sets the level back. ATTACK 1-7 (7
    //             fastest, 20 us; 1 slowest, 800 us) and RELEASE 1-7 (7
    //             fastest, 50 ms; 1 slowest, 1.1 s), numbered as on the
    //             original. RATIO 4, 8, 12, 20 and ALL ("all buttons in": the
    //             loudest peaks pushed below quieter ones, quicker, grittier).
    //             The FET's colour, a little distortion growing with the gain
    //             reduction, is our own curve.
    //   glow      inspired by the LA-2A optical leveller. PEAK REDUCTION (how
    //             much) and GAIN; COMPRESS (gentle) or LIMIT (firmer);
    //             EMPHASIS (the original's rear trim: it reacts more to bright
    //             sounds). The light cell's feel: about 10 ms to grab; lets go
    //             half way in about 60 ms and the rest over 1 to 5 seconds,
    //             slower the harder and longer it's been working. The ratios,
    //             that memory and the valve warmth are our guesses: the cell
    //             isn't specified that precisely.
    class CompGate
    {
    public:
        // Saved by number in rigs and projects: only ever add to the end.
        enum class Type { classic, daybreak, revival, snap, glow };
        static inline const juce::StringArray typeNames { "Classic", "Daybreak", "Revival", "Snap", "Glow" };
        static inline const juce::StringArray snapRatioNames { "4:1", "8:1", "12:1", "20:1", "All" };
        static inline const juce::StringArray glowModeNames { "Compress", "Limit" };

        struct Settings
        {
            bool on = false;
            float thresholdDb = -18.0f;  // classic
            float ratio = 4.0f;          // classic
            float levelDb = 0.0f;        // make-up gain (both)
            float gateThresholdDb = gateOffDb;
            Type type = Type::classic;
            float amount = 5.0f;         // daybreak: COMP, 0 to 10
            float detail = 5.0f;         // daybreak: DETAIL, 0 to 10
            float mix = 1.0f;            // daybreak: MIX, 0 to 1
            bool intensity = false;      // daybreak: INTENSITY
            float revivalGateDb = revivalGateOffDb;   // revival: GATE
            float revivalCompDb = -12.0f;              // revival: COMPRESSION, 0 (none) to -36
            float revivalGainDb = 0.0f;                // revival: GAIN, the limiter's ceiling
            float snapInputDb = 18.0f;                 // snap: INPUT, 0 to 40
            float snapOutputDb = -8.0f;                // snap: OUTPUT
            float snapAttack = 4.0f;                   // snap: ATTACK 1 (slow) to 7 (fast)
            float snapRelease = 5.0f;                  // snap: RELEASE 1 (slow) to 7 (fast)
            int snapRatio = 0;                         // snap: snapRatioNames
            float glowPeakReduction = 40.0f;           // glow: PEAK REDUCTION, 0 to 100
            float glowGainDb = 6.0f;                   // glow: GAIN
            int glowMode = 0;                          // glow: glowModeNames
            float glowEmphasis = 0.0f;                 // glow: EMPHASIS, 0 to 1
        };

        // Snap's attack and release (seconds) for its 1-7 knobs: 7 fastest.
        static float snapAttackSeconds (float knob) noexcept;
        static float snapReleaseSeconds (float knob) noexcept;
        // Snap's and Glow's gain change at a steady level, in dB (0 or less),
        // before OUTPUT / GAIN: for tests and the curves' shape. Snap's input
        // is the level after INPUT.
        static float snapCurveDb (float drivenDb, int ratio) noexcept;
        static float glowCurveDb (float inputDb, float peakReduction, int mode) noexcept;

        // Revival's GATE at (or below) this is off.
        static constexpr float revivalGateOffDb = -80.0f;
        static constexpr float revivalMinDb = -36.0f;   // COMPRESSION's and GAIN's lowest

        // Revival's compressor at a steady input level, in dB: its gain change
        // including the make-up, before the limiter. For tests, and the shape
        // of the curve: 0 dB in gives back about 0 dB out at any COMPRESSION.
        static float revivalCurveDb (float inputDb, float compressionDb) noexcept;

        // Gate threshold at (or below) this means the gate is off.
        static constexpr float gateOffDb = -90.0f;

        void prepare (double sampleRate, int maxBlockSize);
        void process (juce::dsp::AudioBlock<float> monoBlock, const Settings& settings) noexcept;

        // How far the compressor turned the voice down in the last block, in
        // dB (0 or less), for the panel's meter (Revival: its gate and
        // compressor together). 0 while it's off.
        float getGainReductionDb() const noexcept { return gainReductionDb.load(); }

        // Daybreak's gain at a steady input level, in dB, before its make-up
        // gain: exposed for tests, and the shape of its compression curve.
        static float daybreakCurveDb (float inputDb, float amount, bool intensity) noexcept;

    private:
        void processDaybreak (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept;
        void processRevival (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept;
        void processSnap (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept;
        void processGlow (juce::dsp::AudioBlock<float> block, const Settings& settings) noexcept;

        double sampleRate = 48000.0;
        juce::dsp::NoiseGate<float> gate;
        juce::dsp::Compressor<float> compressor;
        juce::SmoothedValue<float> makeUpGain;
        BypassFade bypass;
        std::atomic<float> gainReductionDb { 0.0f };

        // Daybreak
        float envelope = 0.0f;               // the detector's level
        float sidechainState = 0.0f;         // its bass cut
        juce::SmoothedValue<float> mix, daybreakMakeUp;

        // Revival
        float gateLevel = 0.0f, gateGainDb = 0.0f;        // the gate's peak detector and gain
        float compLevelDb = -120.0f, sustainedDb = 0.0f;  // the compressor's detector, and how long it's been working
        float limiterEnvelope = 0.0f;
        juce::SmoothedValue<float> revivalThreshold, revivalCeiling;

        // Snap: the FET's detector (dB), and its input and output gains.
        float snapLevelDb = -120.0f, snapColourPeak = 0.0f;
        juce::SmoothedValue<float> snapIn, snapOut;
        // Glow: the cell's fast and slow parts (dB of reduction, 0 or less),
        // its memory of working hard (0 to 1), EMPHASIS's filter, its gain.
        float glowFastDb = 0.0f, glowSlowDb = 0.0f, glowMemory = 0.0f, glowPeak = 0.0f, emphasisState = 0.0f;
        juce::SmoothedValue<float> glowGain;
    };
}
