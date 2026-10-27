#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/Icons.h"

// The FACEPLATE: the selected block as a small unit of the PR7 line, beside
// its controls on the PROCESSOR page (where the RIG BROWSER and SELECTED
// RIG panels were). Built only from the PR7's own parts, in the current
// finish: the unit's pearl chassis, its LCD screens, big knobs, small keys
// with light stripes, EQ faders, neon LED bars, and lettering stamped in
// ink or printed in red. It follows the block's TYPE and shows what the
// block is doing, live, on its screens:
//
//   DYNAMICS    Classic: a gain-reduction needle, THRESHOLD/RATIO/LEVEL.
//               Daybreak: COMP/DETAIL/MIX knobs, INTENSITY key, a meter.
//               Revival: GATE/COMPRESSION/GAIN as one curve, faders, a meter.
//   SATURATION  MARS: a tube per band, glowing as hard as it's driven.
//   the rest    the block's name, icon and TYPE, until each gets its own.
class Faceplate final : public juce::Component,
                        private juce::Timer,
                        private juce::ChangeListener
{
public:
    explicit Faceplate (ParagonX1AudioProcessor&);
    ~Faceplate() override;

    // Which kind of block: the ones with their own faceplates, or any other.
    enum class Unit { dynamics, saturation, other };
    // What's drawn now (for tests).
    enum class Look { generic, classic, daybreak, revival, mars };

    // `onIds`: the block's on switches (comma separated); `typeId`: its TYPE
    // (or empty).
    void setBlock (Unit unit, const juce::String& title, Icons::Type icon, juce::Colour colour,
                   const juce::String& onIds, const juce::String& typeId);
    Look getLook() const;

    // Moves the meters one step towards the plugin's readings (the timer
    // does it 30 times a second; tests and screenshots call it directly).
    void updateMeters();
    float getShownReduction() const noexcept { return shownReduction; }
    float getShownDrive (int band) const noexcept { return shownDrive[(size_t) juce::jlimit (0, 3, band)]; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;   // the finish changed
    float raw (const juce::String& id) const;
    float normalised (const juce::String& id) const;
    juce::String text (const juce::String& id) const;
    bool isOn() const;

    // Each draws its name in `header` and the rest in `body`.
    void paintGeneric (juce::Graphics&, juce::Rectangle<float> header, juce::Rectangle<float> body);
    void paintClassic (juce::Graphics&, juce::Rectangle<float> header, juce::Rectangle<float> body);
    void paintDaybreak (juce::Graphics&, juce::Rectangle<float> header, juce::Rectangle<float> body);
    void paintRevival (juce::Graphics&, juce::Rectangle<float> header, juce::Rectangle<float> body);
    void paintMars (juce::Graphics&, juce::Rectangle<float> header, juce::Rectangle<float> body);

    ParagonX1AudioProcessor& processor;
    juce::AudioProcessorValueTreeState& parameters;
    Unit unit = Unit::other;
    juce::String title, typeId;
    juce::StringArray onIds;
    Icons::Type icon = Icons::Type::eq;
    juce::Colour colour;   // the block's colour in the chain (the faders' light)
    juce::Image satin;     // the pearl's grain, as on the unit

    // Meters, with a little ballistics so they move like real ones.
    float shownReduction = 0.0f;                 // dB, 0 or less
    std::array<float, 4> shownDrive {};          // MARS bands, 0 to 1+
};
