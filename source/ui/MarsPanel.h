#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/effects/Mars.h"

// SATURATION's panel: MARS, the multi-band saturator inspired by
// FabFilter Saturn 2. Two pages:
//
//   BANDS   left: ON, how many BANDS, and the band display: the
//           frequencies left to right, each band a column. Click a band to
//           pick it; drag a crossover line to move it; drag up or down in a
//           band for its DRIVE; double-click to split a band there (the new
//           one copies it); right-click a band to remove it.
//           right: the picked band's ON, STYLE, DRIVE, DYNAMICS, FEEDBACK,
//           FB FREQ, MIX, and BASS, MID, TREBLE, PRESENCE, LEVEL.
//   GLOBAL  INPUT, OUTPUT, MIX, QUALITY and the crossovers' SLOPE.
class MarsPanel final : public juce::Component,
                        private juce::Timer
{
public:
    // `bandDrive` returns how hard a band is being driven right now (for the display).
    MarsPanel (juce::AudioProcessorValueTreeState&, std::function<float (int)> bandDrive);
    ~MarsPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    void selectBand (int band);
    int getSelectedBand() const noexcept { return selected; }
    void showPage (int page);

    // The display's frequency at an x position, and back. Public for tests.
    float frequencyAt (float x) const;
    float xFor (float hz) const;

    // Split band `band` at `hz` (the new band copies it), or remove one. Public for tests.
    void splitBand (int band, float hz);
    void removeBand (int band);

private:
    struct Knob
    {
        juce::Label label;
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    struct Choice
    {
        juce::Label label;
        juce::ComboBox box;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };
    struct Switch
    {
        juce::Label label;
        juce::ToggleButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };

    void timerCallback() override;
    void bindBandControls();
    void setUp (Knob&, const juce::String& label, const juce::String& tooltip);
    void setUp (Choice&, const juce::String& label, const juce::StringArray& items, const juce::String& tooltip);
    void setUp (Switch&, const juce::String& label, const juce::String& tooltip);
    float value (const juce::String& id) const;
    void set (const juce::String& id, float plainValue);
    int numBands() const;
    std::array<float, effects::Mars::maxBands + 1> bandEdges() const;   // each band's left and right, in Hz
    int bandAt (float x) const;
    int crossoverAt (float x) const;                                    // the crossover line near x, or -1

    juce::AudioProcessorValueTreeState& parameters;
    std::function<float (int)> bandDrive;
    int selected = 0, page = 0;
    int draggingCrossover = -1, draggingDriveOf = -1;
    float dragStartDrive = 0.0f;

    Switch on, bandOn;
    Choice bands, style, quality, slope;
    Knob drive, dynamics, feedback, feedbackFreq, mix, bass, mid, treble, presence, level;
    Knob input, output, globalMix, cross1, cross2, cross3;
    std::array<juce::TextButton, 2> pages;

    juce::Rectangle<float> display;
    juce::Rectangle<int> bandTitle;
    std::array<float, effects::Mars::maxBands> shownDrive {};
};
