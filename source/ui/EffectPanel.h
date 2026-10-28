#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Icons.h"

// The edit panel under the footswitches: the knobs, switches and menus for
// one effect block. Built from a description, so each block's panel is just
// a list of parameter IDs:
//   float parameter  -> knob with its value underneath
//   choice parameter -> drop-down menu
//   on/off parameter -> LED switch
// Each control sits on its own dark card, like the RIG SUMMARY cards it
// replaces; blocks with two parts (comp and gate) show each part's name.
class EffectPanel final : public juce::Component,
                          private juce::Timer
{
public:
    struct Item
    {
        juce::String parameterId, label;
        // Optional: the control is greyed out while this returns false
        // (e.g. delay TIME while the delay is synced to tempo)...
        std::function<bool()> isActive = nullptr;
        // ...or hidden, the other cards closing up, when it belongs to
        // another type (the Classic compressor's knobs while it's Daybreak).
        bool hideWhenInactive = false;
    };

    struct Group
    {
        juce::String title;
        std::vector<Item> items;
        int page = 0;   // which page it's on, when the panel has pages (setPages)
    };

    EffectPanel (juce::AudioProcessorValueTreeState& parameters, std::vector<Group> groups);
    ~EffectPanel() override;

    // The block's name, icon and colour, shown at the top as in the signal chain.
    void setHeader (Icons::Type icon, juce::Colour colour, const juce::String& title);

    // Optional: a gain-reduction meter in the header (the compressor's), fed
    // by this. It returns how far the signal is being turned down, in dB (0 or less).
    void setMeter (std::function<float()> gainReductionDb);

    // Optional: pages, for a block with more controls than fit in a row
    // (as on a rack reverb's edit pages): a button for each in the header,
    // showing the groups whose `page` is that one.
    void setPages (const juce::StringArray& names);
    void showPage (int page);
    int getPage() const noexcept { return currentPage; }

    // In the rack: the panel's controls drawn on the rack unit's face
    // (Faceplate) instead of on dark cards. Knobs are the unit's big knobs
    // with their values on small LCD screens, lists are LCD screens, switches
    // are small keys with light stripes, names are stamped in ink. No
    // background and no header name: the unit shows both.
    void setRack (bool inRack);
    bool isRack() const noexcept { return rack; }

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    struct Control;
    struct GroupLayout
    {
        juce::String title;
        std::vector<Control*> controls;
        juce::Rectangle<int> area;
        int page = 0;
    };

    void updateVisibility();

    std::vector<std::unique_ptr<Control>> controls;
    std::vector<GroupLayout> groups;
    std::vector<juce::Rectangle<int>> tiles;
    Icons::Type headerIcon = Icons::Type::comp;
    juce::Colour headerColour;
    juce::String headerTitle;
    std::function<float()> meterSource;
    float meterDb = 0.0f;
    juce::Rectangle<int> meterArea;
    std::vector<std::unique_ptr<juce::TextButton>> pageButtons;
    int currentPage = 0;
    bool rack = false;
    juce::Colour inkColour;                            // the stamped names' colour, for the finish showing
    mutable std::vector<juce::Rectangle<float>> glasses;   // the LCD screens' glass, for their overlay
};
