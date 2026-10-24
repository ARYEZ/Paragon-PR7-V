#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The floor unit from the mockup, drawn as one piece of pearl (white, or black: see Finish.h):
// a thick bevelled rim with the red-orange light spilling round it, the
// satin face plate with screws in its corners, recessed section panels
// (the slot row and the footswitch row), the panels behind the slot windows, the
// PARAGON PR7-V badge, and the bay on the right that the expression pedal sits
// in. Purely visual: the controls on it are separate components on top.
class PedalBody final : public juce::Component
{
public:
    PedalBody();

    // Positions in this component's coordinates, set by the layout code.
    struct Geometry
    {
        juce::Rectangle<float> face;                       // the main face plate
        juce::Rectangle<float> pedalBay;                   // the expression pedal's housing
        std::vector<juce::Rectangle<float>> sections;      // raised panels (slot row, footswitch row)
        std::vector<juce::Rectangle<float>> insets;        // recessed panels (none on PR7-V since the slot windows went)
        juce::Rectangle<float> badge;                      // PARAGON over the PR7-V mark
        juce::Path printedRed, printedInk;                 // lines printed on the face (the tuner bracket, SAVE—SAVE AS)
    };
    void setGeometry (Geometry);

    // Redraws it in the current finish (see Finish.h).
    void refinish();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void makeSatinTexture();
    void paintBadge (juce::Graphics&) const;
    void drawSatin (juce::Graphics&) const;   // the pearl satin grain over the whole component (clip first)
    static void strokeTopHighlight (juce::Graphics&, juce::Rectangle<float> area, float radius, float thickness, float alpha);
    static void paintScrew (juce::Graphics&, juce::Point<float> centre, float radius);

    juce::Image satin;
    Geometry geometry;
};
