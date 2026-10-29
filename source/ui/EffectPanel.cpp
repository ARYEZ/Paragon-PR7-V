#include "EffectPanel.h"

#include "Finish.h"
#include "LedDisplay.h"
#include "ParagonLookAndFeel.h"

namespace
{
    constexpr int maxTileWidth = 104;
    constexpr int maxRackTileWidth = 116;   // the rack has the room for bigger knobs
    constexpr int rackValueHeight = 18;     // a knob's value screen, under it
    constexpr int tileGap = 8;
    constexpr int groupGap = 22;
}

// One control: a label plus whichever widget suits the parameter's type.
struct EffectPanel::Control
{
    Control (juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& labelText)
    {
        auto* parameter = state.getParameter (id);
        jassert (parameter != nullptr);   // a typo in a panel's parameter list
        label.setText (labelText, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, ParagonColours::textDim);
        label.setMinimumHorizontalScale (0.6f);   // squeeze a long name (PING-PONG) into a narrow card rather than cut it off
        label.setBorderSize ({});

        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (parameter))
        {
            auto box = std::make_unique<juce::ComboBox>();
            box->addItemList (choice->choices, 1);
            for (const auto& item : choice->choices)
                longestChoice = juce::jmax (longestChoice, item.length());
            box->setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff151217));
            box->setColour (juce::ComboBox::outlineColourId, ParagonColours::panelEdge);
            box->setColour (juce::ComboBox::textColourId, ParagonColours::accent);
            box->setColour (juce::ComboBox::arrowColourId, ParagonColours::textDim);
            comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, id, *box);
            widget = std::move (box);
        }
        else if (dynamic_cast<juce::AudioParameterBool*> (parameter) != nullptr)
        {
            auto button = std::make_unique<juce::ToggleButton>();
            buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, id, *button);
            widget = std::move (button);
        }
        else
        {
            auto slider = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            slider->setColour (juce::Slider::textBoxTextColourId, ParagonColours::accent);
            slider->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, maxTileWidth, 22);
            sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id, *slider);
            // Double-click a knob to reset it.
            slider->setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
            isKnob = true;
            widget = std::move (slider);
        }
    }

    void setBounds (juce::Rectangle<int> area, bool rack, int knobWidth = 0)
    {
        if (rack)
        {
            // As on the unit: the name stamped over the control; a knob's
            // value on a small screen under it; a list on a screen; a key.
            // Every card's control centred on the knobs' line, so a row of
            // knobs, lists and keys lines up across the face.
            // The knob's drawing fills only the middle of its box, so the
            // box may reach a little past its card; the row sits in the
            // middle of the face, each name just over its control.
            const auto knobSize = juce::jmin (juce::roundToInt ((float) knobWidth * 1.3f), area.getHeight() - 14 - rackValueHeight - 4);
            auto column = area.withSizeKeepingCentre (area.getWidth(), 14 + knobSize + rackValueHeight + 4);
            label.setBounds (column.removeFromTop (14).expanded (6, 0));
            const auto knobCentreY = column.getY() + juce::roundToInt ((float) knobSize * 0.42f);   // where the knob's top is
            if (isKnob)
                widget->setBounds (column.withSizeKeepingCentre (knobSize, column.getHeight()));
            else if (buttonAttachment != nullptr)
                widget->setBounds (column.withSizeKeepingCentre (juce::jmin (area.getWidth() - 4, 48), 26).withCentre ({ column.getCentreX(), knobCentreY }));
            else
                widget->setBounds (column.withSizeKeepingCentre (area.getWidth() - 4, 30).withCentre ({ column.getCentreX(), knobCentreY }));
            return;
        }

        // Label at the top of the card, the control filling the rest.
        label.setBounds (area.removeFromTop (22));
        area.removeFromTop (4);
        if (isKnob)
            widget->setBounds (area.reduced (2, 0));
        else if (buttonAttachment != nullptr)
            widget->setBounds (area.withSizeKeepingCentre (26, 26));   // LED in the middle of its card
        else
            widget->setBounds (area.withSizeKeepingCentre (area.getWidth() - 4, 28));
    }

    // Returns true if the control appeared or disappeared, so the panel lays out again.
    bool updateActive()
    {
        const auto active = ! isActive || isActive();
        if (hideWhenInactive)
        {
            const auto changed = shown != active;
            shown = active;
            label.setVisible (active && onPage);
            widget->setVisible (active && onPage);
            return changed;
        }
        widget->setEnabled (active);
        widget->setAlpha (active ? 1.0f : 0.35f);
        label.setAlpha (active ? 1.0f : 0.35f);
        return false;
    }

    std::function<bool()> isActive;
    bool hideWhenInactive = false;
    bool shown = true;     // not hidden for belonging to another type
    bool onPage = true;    // on the page showing (always, on a panel without pages)

    juce::Label label;
    std::unique_ptr<juce::Component> widget;
    bool isKnob = false;
    int longestChoice = 0;   // a list's longest entry, in characters
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
};

EffectPanel::EffectPanel (juce::AudioProcessorValueTreeState& parameters, std::vector<Group> groupsToShow)
{
    for (auto& group : groupsToShow)
    {
        GroupLayout layout { group.title, {}, {}, group.page };
        for (const auto& item : group.items)
        {
            controls.push_back (std::make_unique<Control> (parameters, item.parameterId, item.label));
            auto* control = controls.back().get();
            control->isActive = item.isActive;
            control->hideWhenInactive = item.hideWhenInactive;
            control->onPage = group.page == 0;
            addAndMakeVisible (control->label);
            addAndMakeVisible (*control->widget);
            control->updateActive();
            layout.controls.push_back (control);
        }
        groups.push_back (std::move (layout));
    }

    startTimerHz (10);
}

EffectPanel::~EffectPanel() = default;

void EffectPanel::setRack (bool inRack)
{
    if (rack == inRack)
        return;
    rack = inRack;
    inkColour = {};
    for (auto& control : controls)
    {
        control->label.setFont (juce::FontOptions (rack ? 9.0f : 10.5f, juce::Font::bold));
        if (auto* slider = dynamic_cast<juce::Slider*> (control->widget.get()))
        {
            slider->getProperties().set (ParagonLookAndFeel::pedalKnobProperty, rack);
            slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, maxTileWidth, rack ? rackValueHeight : 22);
            slider->setColour (juce::Slider::textBoxTextColourId, rack ? LedDisplay::ledOrange : ParagonColours::accent);
            slider->setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        }
        else if (auto* box = dynamic_cast<juce::ComboBox*> (control->widget.get()))
        {
            box->setColour (juce::ComboBox::backgroundColourId, rack ? juce::Colours::transparentBlack : juce::Colour (0xff151217));
            box->setColour (juce::ComboBox::outlineColourId, rack ? juce::Colours::transparentBlack : ParagonColours::panelEdge);
            box->setColour (juce::ComboBox::textColourId, rack ? LedDisplay::ledOrange : ParagonColours::accent);
            box->setColour (juce::ComboBox::arrowColourId, rack ? LedDisplay::ledOrange.withAlpha (0.6f) : ParagonColours::textDim);
        }
        else
        {
            control->widget->getProperties().set (ParagonLookAndFeel::rackKeyProperty, rack);
        }
    }
    for (auto& button : pageButtons)
        button->getProperties().set (ParagonLookAndFeel::pedalButtonProperty, rack);
    timerCallback();   // the names' ink
    resized();
    repaint();
}

void EffectPanel::timerCallback()
{
    // The names follow the finish (ink is dark on pearl white, light on black).
    const auto ink = rack ? Finish::ink() : ParagonColours::textDim;
    if (ink != inkColour)
    {
        inkColour = ink;
        for (auto& control : controls)
            control->label.setColour (juce::Label::textColourId, ink);
    }

    auto relayout = false;
    for (auto& control : controls)
        if (control->isActive)
            relayout = control->updateActive() || relayout;
    if (relayout)
    {
        resized();
        repaint();
    }

    if (meterSource != nullptr)
    {
        // Falls back gently, as a meter's needle would, so short dips stay readable.
        const auto target = juce::jmin (0.0f, meterSource());
        const auto next = target < meterDb ? target : meterDb + (target - meterDb) * 0.25f;
        if (std::abs (next - meterDb) > 0.05f)
            repaint (meterArea);
        meterDb = next;
    }
}

void EffectPanel::visibilityChanged()
{
    if (isVisible())
        timerCallback();
}

void EffectPanel::setPages (const juce::StringArray& names)
{
    pageButtons.clear();
    for (int page = 0; page < names.size(); ++page)
    {
        auto button = std::make_unique<juce::TextButton> (names[page]);
        button->setClickingTogglesState (true);
        button->setRadioGroupId (5310);
        button->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff151217));
        button->setColour (juce::TextButton::textColourOnId, ParagonColours::accent);   // the page showing, in orange as the plugin's other tabs
        button->onClick = [this, page] { showPage (page); };
        addAndMakeVisible (*button);
        pageButtons.push_back (std::move (button));
    }
    showPage (0);
}

void EffectPanel::showPage (int page)
{
    currentPage = juce::jlimit (0, juce::jmax (0, (int) pageButtons.size() - 1), page);
    if (juce::isPositiveAndBelow (currentPage, (int) pageButtons.size()))
        pageButtons[(size_t) currentPage]->setToggleState (true, juce::dontSendNotification);
    updateVisibility();
    resized();
    repaint();
}

void EffectPanel::updateVisibility()
{
    for (const auto& group : groups)
        for (auto* control : group.controls)
        {
            control->onPage = group.page == currentPage;
            const auto visible = control->onPage && control->shown;
            control->label.setVisible (visible);
            control->widget->setVisible (visible);
        }
}

void EffectPanel::setMeter (std::function<float()> gainReductionDb)
{
    meterSource = std::move (gainReductionDb);
    startTimerHz (30);   // a meter needs to move smoothly
}

void EffectPanel::setHeader (Icons::Type icon, juce::Colour colour, const juce::String& title)
{
    headerIcon = icon;
    headerColour = colour;
    headerTitle = title;
    repaint();
}

void EffectPanel::paint (juce::Graphics& g)
{
    if (rack)
    {
        // On the unit's face: only the screens behind the values and lists,
        // and the parts' names stamped over them.
        glasses.clear();
        for (const auto& control : controls)
        {
            if (! control->widget->isVisible() || control->buttonAttachment != nullptr)
                continue;
            auto area = control->widget->getBounds().toFloat();
            if (control->isKnob)
                area = area.removeFromBottom ((float) rackValueHeight)
                           .withSizeKeepingCentre ((float) control->label.getWidth() - 14.0f, (float) rackValueHeight).expanded (0.0f, 3.0f);   // as wide as its card
            glasses.push_back (LedDisplay::paintScreen (g, area, 0.35f));
        }
        const auto groupsShowing = std::count_if (groups.begin(), groups.end(), [this] (const GroupLayout& group) { return group.page == currentPage; });
        if (groupsShowing > 1)
            for (const auto& group : groups)
                if (group.page == currentPage)
                {
                    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
                    ParagonLookAndFeel::drawStampedText (g, group.title, group.area.withHeight (14).toFloat(), juce::Justification::centredLeft, Finish::printedRed());
                }
        return;
    }

    // Same dark panel as the rest of the dock.
    const auto bounds = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0e0b0f), 0.0f, bounds.getY(),
                                             juce::Colour (0xff09070a), 0.0f, bounds.getBottom(), false));
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (ParagonColours::panelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    // Header: the block's icon in its colour, and its name.
    auto header = getLocalBounds().reduced (16, 10).removeFromTop (24).toFloat();
    if (headerTitle.isNotEmpty())
    {
        Icons::draw (g, headerIcon, header.removeFromLeft (26.0f).withSizeKeepingCentre (22.0f, 20.0f), headerColour, 1.6f, true);
        header.removeFromLeft (8.0f);
        g.setColour (ParagonColours::text);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (headerTitle, header, juce::Justification::centredLeft, false);
    }

    // The gain-reduction meter: a row of LEDs, like a compressor pedal's,
    // lighting from the right as it turns the guitar down further.
    if (meterSource != nullptr && ! meterArea.isEmpty())
    {
        auto area = meterArea.toFloat();
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.setColour (ParagonColours::textDim);
        g.drawText ("GAIN REDUCTION", area.removeFromLeft (92.0f), juce::Justification::centredLeft, false);
        auto readout = area.removeFromRight (46.0f);
        g.setColour (meterDb < -0.1f ? headerColour : ParagonColours::textDim);
        g.drawText (juce::String (meterDb, 1) + " dB", readout, juce::Justification::centredRight, false);

        constexpr float steps[] = { -20.0f, -15.0f, -12.0f, -10.0f, -8.0f, -7.0f, -6.0f, -5.0f, -4.0f, -3.0f, -2.0f, -1.0f };
        const auto ledWidth = area.getWidth() / (float) std::size (steps);
        for (size_t i = 0; i < std::size (steps); ++i)
        {
            const auto led = juce::Rectangle<float> (area.getX() + (float) i * ledWidth, area.getY(), ledWidth, area.getHeight())
                                 .withSizeKeepingCentre (7.0f, 7.0f);
            const auto lit = meterDb <= steps[i];
            if (lit)
            {
                g.setColour (headerColour.withAlpha (0.35f));
                g.fillEllipse (led.expanded (2.5f));
            }
            g.setColour (lit ? headerColour : juce::Colour (0xff2a2430));
            g.fillEllipse (led);
        }
    }

    // Group names, when a block (or the page showing) has more than one part.
    const auto groupsShowing = std::count_if (groups.begin(), groups.end(), [this] (const GroupLayout& group) { return group.page == currentPage; });
    if (groupsShowing > 1)
    {
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        for (const auto& group : groups)
        {
            if (group.page != currentPage)
                continue;
            g.setColour (ParagonColours::textDim);
            g.drawText (group.title, group.area.withHeight (16), juce::Justification::centredLeft, false);
        }
    }

    // A card behind each control, with a thin line of the block's colour along its top.
    for (const auto& tile : tiles)
    {
        const auto card = tile.toFloat();
        g.setColour (juce::Colour (0xff0f0c10));
        g.fillRoundedRectangle (card, 6.0f);
        g.setColour (ParagonColours::panelEdge);
        g.drawRoundedRectangle (card.reduced (0.5f), 6.0f, 1.0f);
        g.setColour (headerColour.withAlpha (0.5f));
        g.fillRoundedRectangle (card.reduced (12.0f, 0.0f).withHeight (2.0f), 1.0f);
    }
}

void EffectPanel::paintOverChildren (juce::Graphics& g)
{
    for (const auto& glass : glasses)
        LedDisplay::paintGlassOverlay (g, glass, 0.35f);
}

void EffectPanel::resized()
{
    auto area = rack ? getLocalBounds() : getLocalBounds().reduced (16, 10);

    // The meter sits in the header, just after the block's name.
    meterArea = {};
    if (meterSource != nullptr)
    {
        const auto titleWidth = juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (14.0f, juce::Font::bold), headerTitle);
        meterArea = area.withHeight (24).withTrimmedLeft (26 + 8 + titleWidth + 28).withWidth (92 + 12 * 14 + 46);
    }

    // The page buttons, in the header after the block's name: only for
    // pages with something on them for the type chosen (MOD's second page
    // is Cyclone's alone), and none when only one page has.
    if (! pageButtons.empty())
    {
        const auto hasControls = [this] (int page)
        {
            for (const auto& group : groups)
                if (group.page == page)
                    for (const auto* control : group.controls)
                        if (control->shown)
                            return true;
            return false;
        };
        if (! hasControls (currentPage))
            for (int page = 0; page < (int) pageButtons.size(); ++page)
                if (hasControls (page))
                {
                    currentPage = page;
                    pageButtons[(size_t) page]->setToggleState (true, juce::dontSendNotification);
                    updateVisibility();
                    break;
                }
        int pagesUsed = 0;
        for (int page = 0; page < (int) pageButtons.size(); ++page)
            pagesUsed += hasControls (page) ? 1 : 0;

        const auto titleWidth = juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (14.0f, juce::Font::bold), headerTitle);
        auto row = rack ? area.withHeight (24) : area.withHeight (24).withTrimmedLeft (26 + 8 + titleWidth + 24);
        for (int page = 0; page < (int) pageButtons.size(); ++page)
        {
            auto& button = pageButtons[(size_t) page];
            button->setVisible (pagesUsed > 1 && hasControls (page));
            if (! button->isVisible())
                continue;
            button->setBounds (row.removeFromLeft (76).reduced (0, 1));
            row.removeFromLeft (4);
        }
    }

    // Only the controls showing: a hidden one's neighbours close up, and
    // only the page showing's groups.
    std::vector<GroupLayout*> groupsShowing;
    std::vector<std::vector<Control*>> shown;
    for (auto& group : groups)
    {
        if (group.page != currentPage)
        {
            group.area = {};
            continue;
        }
        groupsShowing.push_back (&group);
        shown.emplace_back();
        for (auto* control : group.controls)
            if (control->shown)
                shown.back().push_back (control);
    }

    area.removeFromTop (rack ? 30 : 34);   // header (on the rack, the row of keys: pages, GRAPHIC EQ)
    if (groupsShowing.size() > 1)
        area.removeFromTop (rack ? 14 : 20);   // group names
    area.removeFromBottom (4);

    // Cards as wide as fit (up to a limit), centred as a row. An ON light
    // needs little room and a drop-down list a lot, so cards are sized by
    // what they hold rather than all alike.
    int numControls = 0;
    for (const auto& groupControls : shown)
        numControls += (int) groupControls.size();
    // On a crowded panel (the delay's nine controls) the lists give up a
    // little width so the knobs stay a good size.
    const auto listWeight = numControls >= 9 ? 1.75f : 1.9f;
    // A list whose entries run long (the Whammy-Wah's "22 Down 4th / Down 3rd")
    // gets a wider card, in step with its longest entry, so they read in full.
    const auto weight = [listWeight] (const Control& control)
    {
        if (control.isKnob)
            return 1.0f;
        if (control.buttonAttachment != nullptr)
            return 0.75f;
        if (control.longestChoice <= 6)
            return 1.5f;   // a short list (GLITCH's Post / Pre) needs less room than a long one
        return juce::jmax (listWeight, listWeight * (float) control.longestChoice / 16.0f);
    };
    float totalWeight = 0.0f;
    for (const auto& groupControls : shown)
        for (const auto* control : groupControls)
            totalWeight += weight (*control);
    const auto gaps = tileGap * (numControls - (int) groupsShowing.size()) + groupGap * ((int) groupsShowing.size() - 1);
    const auto unitWidth = juce::jmin ((float) (rack ? maxRackTileWidth : maxTileWidth), (float) (area.getWidth() - gaps) / juce::jmax (1.0f, totalWeight));
    const auto tileWidth = [&] (const Control& control) { return juce::roundToInt (unitWidth * weight (control)); };

    auto totalWidth = gaps;
    for (const auto& groupControls : shown)
        for (const auto* control : groupControls)
            totalWidth += tileWidth (*control);
    area = area.withSizeKeepingCentre (totalWidth, area.getHeight());

    tiles.clear();
    for (size_t g = 0; g < groupsShowing.size(); ++g)
    {
        auto& group = *groupsShowing[g];
        auto groupWidth = tileGap * ((int) shown[g].size() - 1);
        for (const auto* control : shown[g])
            groupWidth += tileWidth (*control);
        group.area = area.removeFromLeft (groupWidth).withTrimmedTop (-20);
        area.removeFromLeft (groupGap);

        auto row = group.area.withTrimmedTop (20);
        for (auto* control : shown[g])
        {
            const auto tile = row.removeFromLeft (tileWidth (*control));
            row.removeFromLeft (tileGap);
            tiles.push_back (tile);
            control->setBounds (rack ? tile.reduced (2, 4) : tile.reduced (4, 8), rack, juce::roundToInt (unitWidth) - 4);
        }
    }
}
