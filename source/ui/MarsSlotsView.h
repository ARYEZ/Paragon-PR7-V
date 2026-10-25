#pragma once

#include "MarsModUi.h"
#include "ParagonLookAndFeel.h"

// MARS's SLOTS page: the 16 modulation slots, each a SOURCE, a TARGET and an
// AMOUNT (-100% to +100%), with what the slot is doing right now shown in
// its amount bar.
class MarsSlotsView final : public juce::Component
{
public:
    MarsSlotsView (juce::AudioProcessorValueTreeState&, const effects::Mars*);
    ~MarsSlotsView() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh();   // moves the live marks: call regularly while shown

    // The target menu, grouped (Band 1, Band 2... Global, XLFO 1...). Public for tests.
    static void fillTargets (juce::ComboBox&);

private:
    // The AMOUNT: a bar filling left or right of its middle.
    class AmountBar final : public juce::Slider
    {
    public:
        AmountBar();
        void paint (juce::Graphics&) override;
        juce::Colour colour { ParagonColours::accent };
        float live = 0.0f;   // where the slot has its target now (a fraction, -1 to 1)
    };

    struct Row
    {
        juce::Label number;
        juce::ComboBox source, target;
        AmountBar amount;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sourceAttachment, targetAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAttachment;
    };

    juce::AudioProcessorValueTreeState& parameters;
    const effects::Mars* mars;
    std::array<Row, effects::MarsModulation::numSlots> rows;
    juce::Rectangle<int> headings;
};
