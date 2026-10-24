#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Parameters.h"
#include "PluginProcessor.h"

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace
{
    const juce::File exampleModels { PARAGON_NAM_EXAMPLE_MODELS };
    const juce::File wavenetModel = exampleModels.getChildFile ("wavenet.nam");   // 48 kHz, loudness -20.0 dB
    const juce::File lstmModel = exampleModels.getChildFile ("lstm.nam");         // 48 kHz, loudness -37.8 dB

    constexpr int blockSize = 256;

    struct TestRig
    {
        explicit TestRig (double rate = 48000.0) : sampleRate (rate)
        {
            processor.setPlayConfigDetails (1, 2, sampleRate, blockSize);
            set (ParamIDs::inputLowCut, effects::LowCut::offHz);   // LOW CUT off: these tests are about the amp and cab
            processor.prepareToPlay (sampleRate, blockSize);
        }

        void set (const char* id, float plainValue)
        {
            auto* parameter = processor.getParameters().getParameter (id);
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
        }

        // Plays `numBlocks` of a 220 Hz sine (a guitar's A string) and
        // returns everything that came out of the left channel.
        std::vector<float> play (int numBlocks, float amplitude = 0.3f)
        {
            std::vector<float> output;
            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer midi;

            for (int block = 0; block < numBlocks; ++block)
            {
                buffer.clear();
                for (int i = 0; i < blockSize; ++i, ++phase)
                    buffer.setSample (0, i, amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * phase / sampleRate));

                processor.processBlock (buffer, midi);
                output.insert (output.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
            }
            return output;
        }

        // Plays a single block made of `samples` and returns the output.
        std::vector<float> playBlock (const std::vector<float>& samples)
        {
            juce::AudioBuffer<float> buffer (2, (int) samples.size());
            buffer.clear();
            std::copy (samples.begin(), samples.end(), buffer.getWritePointer (0));
            juce::MidiBuffer midi;
            processor.processBlock (buffer, midi);
            return { buffer.getReadPointer (0), buffer.getReadPointer (0) + buffer.getNumSamples() };
        }

        void waitForLoads() { REQUIRE (processor.waitForLoads (20000)); }

        double sampleRate;
        long phase = 0;
        ParagonX1AudioProcessor processor;
    };

    float rms (const std::vector<float>& samples, size_t from = 0)
    {
        double sum = 0.0;
        for (auto i = from; i < samples.size(); ++i)
            sum += samples[i] * samples[i];
        return (float) std::sqrt (sum / (double) (samples.size() - from));
    }

    bool allFinite (const std::vector<float>& samples)
    {
        return std::all_of (samples.begin(), samples.end(), [] (float s) { return std::isfinite (s); });
    }

    // How different two signals are, relative to the first one's level.
    float relativeDifference (const std::vector<float>& a, const std::vector<float>& b)
    {
        std::vector<float> difference (a.size());
        for (size_t i = 0; i < a.size(); ++i)
            difference[i] = a[i] - b[i];
        return rms (difference) / rms (a);
    }

    // Writes a mono WAV file containing `samples`.
    juce::File writeWav (const juce::File& file, const std::vector<float>& samples, double sampleRate = 48000.0)
    {
        file.deleteFile();
        juce::AudioBuffer<float> buffer (1, (int) samples.size());
        std::copy (samples.begin(), samples.end(), buffer.getWritePointer (0));

        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        auto writer = juce::WavAudioFormat().createWriterFor (stream, juce::AudioFormatWriterOptions()
                                                                          .withSampleRate (sampleRate)
                                                                          .withNumChannels (1)
                                                                          .withBitsPerSample (32)
                                                                          .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        return file;
    }

    juce::File tempFile (const juce::String& name)
    {
        return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ParagonX1Tests").getChildFile (name);
    }
}

TEST_CASE ("Loading a NAM model changes the sound", "[amp]")
{
    TestRig rig;
    const auto clean = rig.play (40);

    rig.processor.loadAmpModel (wavenetModel);
    rig.waitForLoads();

    const auto status = rig.processor.getAmpStatus();
    CHECK (status.file == wavenetModel);
    CHECK (status.error.isEmpty());
    CHECK (status.detail.contains ("48 kHz"));
    CHECK_FALSE (status.detail.contains ("resampled"));

    rig.phase = 0;
    const auto amped = rig.play (40);
    REQUIRE (allFinite (amped));
    CHECK (rms (amped, blockSize * 10) > 0.001f);
    CHECK (relativeDifference (clean, amped) > 0.1f);   // clearly not the clean signal any more
    CHECK (rig.processor.getLatencySamples() == 0);     // no resampling at 48 kHz, so no latency
}

TEST_CASE ("At other sample rates the model is resampled and the latency reported", "[amp]")
{
    for (const auto rate : { 44100.0, 96000.0 })
    {
        DYNAMIC_SECTION (rate << " Hz")
        {
            TestRig rig (rate);
            rig.processor.loadAmpModel (wavenetModel);
            rig.waitForLoads();
            CHECK (rig.processor.getAmpStatus().detail.contains ("resampled"));

            // The host learns about latency in prepareToPlay (or from a
            // timer while playing), so re-prepare as a host would.
            rig.processor.prepareToPlay (rate, blockSize);
            CHECK (rig.processor.getLatencySamples() > 0);

            const auto output = rig.play (100);
            REQUIRE (allFinite (output));
            CHECK (rms (output, output.size() / 2) > 0.001f);
        }
    }
}

TEST_CASE ("Normalise matches the official plugin's -18 dB loudness target", "[amp]")
{
    // Two identical rigs, normalise on and off. The model is deterministic, so
    // the only difference should be the normalisation gain:
    // wavenet.nam's loudness is -20.02 dB, so the gain is 10^((-18 + 20.02) / 20).
    TestRig normalised, raw;
    raw.set (ParamIDs::ampNormalise, 0.0f);

    for (auto* rig : { &normalised, &raw })
    {
        rig->processor.loadAmpModel (wavenetModel);
        rig->waitForLoads();
    }

    const auto normalisedOut = normalised.play (60);
    const auto rawOut = raw.play (60);
    const auto expectedGain = std::pow (10.0, (-18.0 - -20.020729064941406) / 20.0);
    CHECK_THAT (rms (normalisedOut, blockSize * 20) / rms (rawOut, blockSize * 20), WithinRel (expectedGain, 0.001));
}

TEST_CASE ("Turning the amp off, or unloading it, fades back to the clean signal", "[amp]")
{
    TestRig rig;
    const auto clean = rig.play (40);

    rig.processor.loadAmpModel (wavenetModel);
    rig.waitForLoads();
    rig.play (10);

    SECTION ("Amp switch off")
    {
        rig.set (ParamIDs::ampOn, 0.0f);
    }

    SECTION ("Unload the model")
    {
        rig.processor.clearAmpModel();
        rig.waitForLoads();
        CHECK (rig.processor.getAmpStatus().file == juce::File());
    }

    rig.play (10);   // let the 20 ms fade finish
    rig.phase = 0;
    CHECK (relativeDifference (clean, rig.play (40)) < 1.0e-5f);
}

TEST_CASE ("A file that isn't a NAM model is reported, and the current model keeps playing", "[amp]")
{
    TestRig rig;
    const auto clean = rig.play (40);

    rig.processor.loadAmpModel (wavenetModel);
    rig.waitForLoads();

    auto junk = tempFile ("not-a-model.nam");
    junk.create();
    junk.replaceWithText ("this is not a model");
    rig.processor.loadAmpModel (junk);
    rig.waitForLoads();

    const auto status = rig.processor.getAmpStatus();
    CHECK (status.error.contains ("not-a-model.nam"));
    CHECK (status.file == wavenetModel);

    // Still the amp, not the clean signal.
    rig.phase = 0;
    const auto after = rig.play (40);
    REQUIRE (allFinite (after));
    CHECK (relativeDifference (clean, after) > 0.1f);
}

TEST_CASE ("Swapping models crossfades instead of jumping", "[amp]")
{
    // The largest sample-to-sample step in a stretch of audio.
    const auto maxStep = [] (const std::vector<float>& samples, size_t from, size_t to)
    {
        float result = 0.0f;
        for (auto i = from + 1; i < to; ++i)
            result = std::max (result, std::abs (samples[i] - samples[i - 1]));
        return result;
    };

    TestRig rig;
    rig.processor.loadAmpModel (wavenetModel);
    rig.waitForLoads();
    const auto before = rig.play (40);

    rig.processor.loadAmpModel (lstmModel);
    rig.waitForLoads();
    const auto after = rig.play (40);   // the swap happens in the first block
    REQUIRE (allFinite (after));
    CHECK (rig.processor.getAmpStatus().file == lstmModel);

    // A hard switch between two models shows up as one jump about the size
    // of the waveform itself (~0.1 here). With the crossfade, the steps in
    // the swap block stay within what either model does on its own.
    const auto normalStep = std::max (maxStep (before, blockSize * 10, before.size()),
                                      maxStep (after, blockSize * 10, after.size()));
    CHECK (maxStep (after, 0, blockSize) <= normalStep * 1.2f);
}

TEST_CASE ("The cabinet IR is convolved with the signal", "[cab]")
{
    TestRig rig;

    // An IR that is just a delayed click makes the result easy to predict:
    // the output should be the input, delayed by 10 samples and turned down
    // by the official plugin's fixed 18 dB.
    std::vector<float> ir (64, 0.0f);
    ir[10] = 1.0f;
    const auto irFile = writeWav (tempFile ("delay-ir.wav"), ir);

    rig.processor.loadCabinetIR (irFile);
    rig.waitForLoads();

    const auto status = rig.processor.getCabStatus();
    CHECK (status.file == irFile);
    CHECK (status.error.isEmpty());

    // JUCE's convolution swaps the IR in on its own thread and then
    // crossfades, so play silence until that's done.
    for (int i = 0; i < 100; ++i)
    {
        rig.playBlock (std::vector<float> (blockSize, 0.0f));
        juce::Thread::sleep (2);
    }

    std::vector<float> click (blockSize, 0.0f);
    click[0] = 0.5f;
    const auto output = rig.playBlock (click);

    CHECK_THAT (output[0], WithinAbs (0.0, 1.0e-4));
    CHECK_THAT (output[10], WithinRel (0.5 * std::pow (10.0, -18.0 / 20.0), 0.001));
    const auto peak = std::max_element (output.begin(), output.end(), [] (float a, float b) { return std::abs (a) < std::abs (b); });
    CHECK (std::distance (output.begin(), peak) == 10);
}

TEST_CASE ("An unreadable cabinet file is reported", "[cab]")
{
    TestRig rig;
    auto junk = tempFile ("not-audio.wav");
    junk.create();
    junk.replaceWithText ("this is not audio");

    rig.processor.loadCabinetIR (junk);
    rig.waitForLoads();

    const auto status = rig.processor.getCabStatus();
    CHECK (status.error.contains ("not-audio.wav"));
    CHECK (status.file == juce::File());
}

TEST_CASE ("The amp and cabinet files are saved with the project and reloaded", "[state]")
{
    std::vector<float> ir (32, 0.0f);
    ir[0] = 1.0f;
    const auto irFile = writeWav (tempFile ("unit-ir.wav"), ir);

    juce::MemoryBlock saved;
    {
        TestRig original;
        original.processor.loadAmpModel (wavenetModel);
        original.processor.loadCabinetIR (irFile);
        original.waitForLoads();
        original.processor.getStateInformation (saved);
    }

    TestRig restored;
    restored.processor.setStateInformation (saved.getData(), (int) saved.getSize());
    restored.waitForLoads();

    CHECK (restored.processor.getAmpStatus().file == wavenetModel);
    CHECK (restored.processor.getAmpStatus().error.isEmpty());
    CHECK (restored.processor.getCabStatus().file == irFile);
}

TEST_CASE ("A project whose model file is missing keeps the path and says so", "[state]")
{
    const auto missing = tempFile ("moved-away.nam");
    missing.deleteFile();

    // Build a saved state that points at a file that doesn't exist.
    juce::MemoryBlock saved;
    {
        TestRig original;
        original.processor.getStateInformation (saved);
    }
    auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
    xml->setAttribute ("ampModelPath", missing.getFullPathName());
    saved.reset();
    juce::AudioProcessor::copyXmlToBinary (*xml, saved);

    TestRig restored;
    restored.processor.setStateInformation (saved.getData(), (int) saved.getSize());
    restored.waitForLoads();

    const auto status = restored.processor.getAmpStatus();
    CHECK (status.file == missing);
    CHECK (status.error.contains ("Can't find"));

    // Saving again must not lose the path.
    juce::MemoryBlock resaved;
    restored.processor.getStateInformation (resaved);
    auto resavedXml = juce::AudioProcessor::getXmlFromBinary (resaved.getData(), (int) resaved.getSize());
    CHECK (resavedXml->getStringAttribute ("ampModelPath") == missing.getFullPathName());

    // And the plugin still passes audio.
    CHECK (allFinite (restored.play (10)));
}
