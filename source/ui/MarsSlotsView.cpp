#include "MarsSlotsView.h"

#include "Parameters.h"
#include "ParagonLookAndFeel.h"

using Mod = effects::MarsModulation;

MarsSlotsView::AmountBar::AmountBar() : juce::Slider (juce::Slider::LinearBar, juce::Slider::NoTextBox)
{
    setTooltip ("How much the source moves the target: + one way, - the other. Double-click: 0");
    setDoubleClickReturnValue (true, 0.0);
}

void MarsSlotsView::AmountBar::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f, 1.5f);
    g.setColour (juce::Colour (0xff151217));
    g.fillRoundedRectangle (bounds, 3.0f);
    g.setColour (ParagonColours::panelEdge);
    g.drawRoundedRectangle (bounds, 3.0f, 1.0f);

    const auto amount = (float) getValue();
    const auto middle = bounds.getCentreX(), half = bounds.getWidth() * 0.5f - 2.0f;
    const auto end = middle + half * amount;
    g.setColour (colour.withAlpha (isEnabled() ? 0.55f : 0.2f));
    g.fillRoundedRectangle (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (middle, end), bounds.getY() + 2.0f,
                                                                        juce::jmax (middle, end), bounds.getBottom() - 2.0f), 2.0f);
    g.setColour (ParagonColours::panelEdge.brighter (0.4f));
    g.drawVerticalLine (juce::roundToInt (middle), bounds.getY() + 1.0f, bounds.getBottom() - 1.0f);

    // Where the slot has its target right now.
    if (std::abs (live) > 0.001f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        const auto x = middle + half * juce::jlimit (-1.0f, 1.0f, live);
        g.fillRect (juce::Rectangle<float> (2.0f, bounds.getHeight() - 2.0f).withCentre ({ x, bounds.getCentreY() }));
    }

    g.setColour (ParagonColours::text);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    const auto percent = juce::roundToInt (amount * 100.0f);
    g.drawText ((percent > 0 ? "+" : "") + juce::String (percent) + "%", bounds, juce::Justification::centred, false);
}

//==============================================================================
void MarsSlotsView::fillTargets (juce::ComboBox& box)
{
    // Grouped as the targets are numbered, so the box's item order is the parameter's.
    box.clear (juce::dontSendNotification);
    auto* root = box.getRootMenu();
    root->addItem (1, "None");
    const auto addGroup = [root] (const juce::String& name, int first, int count)
    {
        juce::PopupMenu group;
        for (int target = first; target < first + count; ++target)
            group.addItem (target + 1, Mod::targetName (target));
        root->addSubMenu (name, group);
    };
    for (int band = 0; band < effects::Mars::maxBands; ++band)
        addGroup ("Band " + juce::String (band + 1), Mod::bandTarget (band, 0), Mod::bandParts);
    addGroup ("Global", Mod::firstGlobalTarget, Mod::firstLfoTarget - Mod::firstGlobalTarget);
    for (int l = 0; l < Mod::numLfos; ++l)
        addGroup ("XLFO " + juce::String (l + 1), Mod::lfoTarget (l, 0), Mod::lfoParts);
    for (int e = 0; e < Mod::numEgs; ++e)
        addGroup ("EG " + juce::String (e + 1), Mod::egTarget (e, 0), Mod::egParts);
    for (int f = 0; f < Mod::numEfs; ++f)
        addGroup ("EF " + juce::String (f + 1), Mod::efTarget (f, 0), Mod::efParts);
    addGroup ("Slot amounts", Mod::firstSlotTarget, Mod::numSlots);
}

MarsSlotsView::MarsSlotsView (juce::AudioProcessorValueTreeState& state, const effects::Mars* marsToShow)
    : parameters (state), mars (marsToShow)
{
    for (int s = 0; s < Mod::numSlots; ++s)
    {
        auto& row = rows[(size_t) s];
        MarsModUi::styleLabel (row.number, juce::String (s + 1));
        addAndMakeVisible (row.number);

        for (int source = 0; source < Mod::numSources; ++source)
            row.source.addItem (source == 0 ? juce::String ("-") : MarsModUi::sourceShortName (source), source + 1);
        fillTargets (row.target);
        for (auto* box : { &row.source, &row.target })
        {
            box->setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff151217));
            box->setColour (juce::ComboBox::outlineColourId, ParagonColours::panelEdge);
            box->setColour (juce::ComboBox::textColourId, ParagonColours::text);
            box->setColour (juce::ComboBox::arrowColourId, ParagonColours::textDim);
            addAndMakeVisible (*box);
        }
        row.source.setTooltip ("What moves the target: an XLFO, envelope, MIDI, an XY pad or the expression pedal");
        row.target.setTooltip ("The knob it moves");
        addAndMakeVisible (row.amount);

        row.sourceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (parameters, ParamIDs::marsSlot (s, "Source"), row.source);
        row.targetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (parameters, ParamIDs::marsSlot (s, "Target"), row.target);
        row.amountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (parameters, ParamIDs::marsSlot (s, "Amount"), row.amount);
        row.source.onChange = row.target.onChange = [this] { refresh(); };
    }
    refresh();
}

MarsSlotsView::~MarsSlotsView() = default;

void MarsSlotsView::refresh()
{
    for (auto& row : rows)
    {
        const auto source = row.source.getSelectedItemIndex();
        const auto inUse = source > 0 && row.target.getSelectedItemIndex() > 0;
        const auto colour = MarsModUi::sourceColour (source);
        row.source.setColour (juce::ComboBox::textColourId, source > 0 ? colour : ParagonColours::textDim);
        row.number.setColour (juce::Label::textColourId, inUse ? colour : ParagonColours::textDim);
        row.amount.colour = colour;
        row.amount.setAlpha (inUse ? 1.0f : 0.45f);
        const auto live = inUse && mars != nullptr ? mars->getSourceValue (source) * (float) row.amount.getValue() : 0.0f;
        if (std::abs (live - row.amount.live) > 0.002f)
        {
            row.amount.live = live;
            row.amount.repaint();
        }
    }
}

void MarsSlotsView::resized()
{
    // Two columns of eight, with headings.
    auto area = getLocalBounds();
    headings = area.removeFromTop (14);
    const auto columnWidth = (area.getWidth() - 12) / 2;
    const auto rowHeight = area.getHeight() / 8;
    for (int s = 0; s < Mod::numSlots; ++s)
    {
        auto& row = rows[(size_t) s];
        const auto column = s / 8;
        auto line = juce::Rectangle<int> (area.getX() + column * (columnWidth + 12), area.getY() + (s % 8) * rowHeight, columnWidth, rowHeight).reduced (0, 1);
        row.number.setBounds (line.removeFromLeft (16));
        const auto rest = line.getWidth();
        row.source.setBounds (line.removeFromLeft (rest * 30 / 100).reduced (1, 0));
        row.target.setBounds (line.removeFromLeft (rest * 44 / 100).reduced (1, 0));
        row.amount.setBounds (line.reduced (1, 0));
    }
}

void MarsSlotsView::paint (juce::Graphics& g)
{
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    g.setColour (ParagonColours::textDim);
    const auto columnWidth = (getWidth() - 12) / 2;
    for (int column = 0; column < 2; ++column)
    {
        auto line = headings.withX (column * (columnWidth + 12)).withWidth (columnWidth);
        line.removeFromLeft (16);
        const auto rest = line.getWidth();
        g.drawText ("SOURCE", line.removeFromLeft (rest * 30 / 100), juce::Justification::centred, false);
        g.drawText ("TARGET", line.removeFromLeft (rest * 44 / 100), juce::Justification::centred, false);
        g.drawText ("AMOUNT", line, juce::Justification::centred, false);
    }
}
