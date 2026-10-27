#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/Icons.h"

// The FACEPLATE: a picture of the selected block as a piece of hardware,
// beside its controls on the PROCESSOR page (where the RIG BROWSER and
// SELECTED RIG panels were). It follows the block's TYPE and shows what
// the block is doing, live:
//
//   DYNAMICS    Classic: a studio rack compressor with a gain-reduction
//               needle. Daybreak: a boutique pedal with an LED ladder.
//               Revival: a vocal leveller drawing its GATE, COMPRESSION
//               and GAIN as a live curve, with a meter.
//   SATURATION  MARS: a valve unit, a glowing tube per band (glowing
//               harder the harder the band is driven).
//   the rest    a rack unit with the block's name, its TYPE and an ON lamp,
//               until each gets its own.
//
// All drawn in code, our own designs (no maker's look is copied).
class Faceplate final : public juce::Component,
                        private juce::Timer
{
public:
    explicit Faceplate (ParagonX1AudioProcessor&);

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

private:
    void timerCallback() override;
    float raw (const juce::String& id) const;
    float normalised (const juce::String& id) const;
    juce::String text (const juce::String& id) const;
    bool isOn() const;

    void paintGeneric (juce::Graphics&, juce::Rectangle<float>);
    void paintClassic (juce::Graphics&, juce::Rectangle<float>);
    void paintDaybreak (juce::Graphics&, juce::Rectangle<float>);
    void paintRevival (juce::Graphics&, juce::Rectangle<float>);
    void paintMars (juce::Graphics&, juce::Rectangle<float>);

    ParagonX1AudioProcessor& processor;
    juce::AudioProcessorValueTreeState& parameters;
    Unit unit = Unit::other;
    juce::String title, typeId;
    juce::StringArray onIds;
    Icons::Type icon = Icons::Type::eq;
    juce::Colour colour;

    // Meters, with a little ballistics so they move like real ones.
    float shownReduction = 0.0f;                 // dB, 0 or less
    std::array<float, 4> shownDrive {};          // MARS bands, 0 to 1+
};
