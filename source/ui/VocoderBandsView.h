#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/effects/Vocoder.h"

// The VOCODER's display on the rack: one screen across the rack's display
// area, showing each band (bars as tall as the voice is loud in it, live)
// and the bands' VOL or PAN curve over them, as VocalSynth's Advanced View
// shows its band bars (our drawing). Drag on it to draw the curve; VOL and
// PAN choose which; RESET flattens the one showing (as the manual's RESET).
// The notes the synth is playing are named at the top.
class VocoderBandsView final : public juce::Component,
                               private juce::Timer
{
public:
    using Voc = effects::Vocoder;

    VocoderBandsView (juce::AudioProcessorValueTreeState&, const Voc* vocoder);
    ~VocoderBandsView() override;

    // Which curve the mouse draws: VOL (false) or PAN (true).
    void showPan (bool pan);
    bool isShowingPan() const noexcept { return showingPan; }

    // The plotting area, and drawing the curve at a point on it (and those
    // passed since `from`, so a fast drag leaves no gaps). Public for tests.
    juce::Rectangle<float> getPlotArea() const noexcept { return plot; }
    void drawAt (juce::Point<float>, std::optional<juce::Point<float>> from = {});
    void resetCurve();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    std::vector<int> drawnState() const;           // what paint() draws that can change
    juce::String pointId (int point) const;
    float yFor (float value) const noexcept;       // a curve value on the plot
    float valueAt (float y) const noexcept;        // and back

    juce::AudioProcessorValueTreeState& parameters;
    const Voc* vocoder;
    bool showingPan = false;
    juce::TextButton volKey { "VOL" }, panKey { "PAN" }, resetKey { "RESET" };
    juce::Rectangle<float> glass, plot;
    std::optional<juce::Point<float>> lastDrag;
    std::vector<int> shownState;
};
