#include "MarsModUi.h"

#include "Parameters.h"
#include "ParagonLookAndFeel.h"

namespace MarsModUi
{
    juce::Colour sourceColour (int source)
    {
        switch (source)
        {
            case Mod::lfo1: case Mod::lfo2:   return juce::Colour (0xff4fc3f7);   // XLFO: sky blue
            case Mod::eg1: case Mod::eg2:     return juce::Colour (0xffc58cff);   // EG: violet
            case Mod::ef1: case Mod::ef2:     return juce::Colour (0xff6fdc8c);   // EF: green
            case Mod::midi1: case Mod::midi2: return juce::Colour (0xff4fe3dd);   // MIDI: teal
            case Mod::xy1x: case Mod::xy1y:
            case Mod::xy2x: case Mod::xy2y:   return juce::Colour (0xffff6fa8);   // XY: pink
            case Mod::pedal:                  return juce::Colour (0xffff9a4d);   // the pedal: orange
            default:                          return ParagonColours::textDim;
        }
    }

    juce::String sourceShortName (int source)
    {
        return source == Mod::pedal ? juce::String ("Pedal") : Mod::sourceNames[juce::jlimit (0, Mod::numSources - 1, source)];
    }

    //==============================================================================
    void ModSlider::mouseDown (const juce::MouseEvent& event)
    {
        menuShown = event.mods.isPopupMenu() && target > 0 && onModulateMenu != nullptr;
        if (menuShown)
            onModulateMenu (target);
        else
            juce::Slider::mouseDown (event);
    }

    void ModSlider::mouseDrag (const juce::MouseEvent& event)
    {
        if (! menuShown)
            juce::Slider::mouseDrag (event);
    }

    void ModSlider::mouseUp (const juce::MouseEvent& event)
    {
        if (! menuShown)
            juce::Slider::mouseUp (event);
        menuShown = false;
    }

    //==============================================================================
    void styleLabel (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, ParagonColours::textDim);
        label.setMinimumHorizontalScale (0.6f);
        label.setBorderSize ({});
        label.setInterceptsMouseClicks (false, false);
    }

    void setUp (juce::Component& parent, Knob& knob, const juce::String& labelText, const juce::String& tooltip)
    {
        styleLabel (knob.label, labelText);
        knob.slider.setColour (juce::Slider::textBoxTextColourId, ParagonColours::accent);
        knob.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 15);
        knob.slider.setTooltip (tooltip);
        parent.addAndMakeVisible (knob.label);
        parent.addAndMakeVisible (knob.slider);
    }

    void setUp (juce::Component& parent, Choice& choice, const juce::String& labelText, const juce::StringArray& items, const juce::String& tooltip)
    {
        styleLabel (choice.label, labelText);
        choice.box.addItemList (items, 1);
        choice.box.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff151217));
        choice.box.setColour (juce::ComboBox::outlineColourId, ParagonColours::panelEdge);
        choice.box.setColour (juce::ComboBox::textColourId, ParagonColours::accent);
        choice.box.setColour (juce::ComboBox::arrowColourId, ParagonColours::textDim);
        choice.box.setTooltip (tooltip);
        parent.addAndMakeVisible (choice.label);
        parent.addAndMakeVisible (choice.box);
    }

    void setUp (juce::Component& parent, Switch& control, const juce::String& labelText, const juce::String& tooltip)
    {
        styleLabel (control.label, labelText);
        control.button.setTooltip (tooltip);
        parent.addAndMakeVisible (control.label);
        parent.addAndMakeVisible (control.button);
    }

    void attach (Parameters& parameters, Knob& knob, const juce::String& id, int target)
    {
        knob.attachment.reset();
        knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (parameters, id, knob.slider);
        auto* parameter = parameters.getParameter (id);
        knob.slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
        knob.slider.target = target;
    }

    void attach (Parameters& parameters, Choice& choice, const juce::String& id)
    {
        choice.attachment.reset();
        choice.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (parameters, id, choice.box);
    }

    void attach (Parameters& parameters, Switch& control, const juce::String& id)
    {
        control.attachment.reset();
        control.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (parameters, id, control.button);
    }

    void show (Knob& knob, bool visible) { knob.label.setVisible (visible); knob.slider.setVisible (visible); }
    void show (Choice& choice, bool visible) { choice.label.setVisible (visible); choice.box.setVisible (visible); }
    void show (Switch& control, bool visible) { control.label.setVisible (visible); control.button.setVisible (visible); }

    void place (juce::Component& label, juce::Component& widget, juce::Rectangle<int> cell, int widgetHeight)
    {
        label.setBounds (cell.removeFromTop (14));
        cell.removeFromTop (2);
        widget.setBounds (widgetHeight > 0 ? cell.withSizeKeepingCentre (cell.getWidth(), widgetHeight) : cell);
    }

    float value (Parameters& parameters, const juce::String& id) { return parameters.getRawParameterValue (id)->load(); }

    void set (Parameters& parameters, const juce::String& id, float plainValue)
    {
        auto* parameter = parameters.getParameter (id);
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
        parameter->endChangeGesture();
    }

    //==============================================================================
    std::vector<SlotOnTarget> slotsOn (Parameters& parameters, int target)
    {
        std::vector<SlotOnTarget> found;
        for (int slot = 0; slot < Mod::numSlots; ++slot)
        {
            const auto source = juce::roundToInt (value (parameters, ParamIDs::marsSlot (slot, "Source")));
            if (source > Mod::none && juce::roundToInt (value (parameters, ParamIDs::marsSlot (slot, "Target"))) == target)
                found.push_back ({ slot, source, value (parameters, ParamIDs::marsSlot (slot, "Amount")) });
        }
        return found;
    }

    int addSlot (Parameters& parameters, int source, int target, float amount)
    {
        for (int slot = 0; slot < Mod::numSlots; ++slot)
        {
            const auto used = juce::roundToInt (value (parameters, ParamIDs::marsSlot (slot, "Source"))) > Mod::none
                              && juce::roundToInt (value (parameters, ParamIDs::marsSlot (slot, "Target"))) > 0;
            if (used)
                continue;
            set (parameters, ParamIDs::marsSlot (slot, "Source"), (float) source);
            set (parameters, ParamIDs::marsSlot (slot, "Target"), (float) target);
            set (parameters, ParamIDs::marsSlot (slot, "Amount"), amount);
            return slot;
        }
        return -1;
    }

    void clearSlot (Parameters& parameters, int slot)
    {
        set (parameters, ParamIDs::marsSlot (slot, "Source"), 0.0f);
        set (parameters, ParamIDs::marsSlot (slot, "Target"), 0.0f);
        set (parameters, ParamIDs::marsSlot (slot, "Amount"), 0.0f);
    }

    juce::Colour targetColour (Parameters& parameters, int target)
    {
        if (target <= 0)
            return juce::Colours::transparentBlack;
        const auto slots = slotsOn (parameters, target);
        if (slots.empty())
            return juce::Colours::transparentBlack;
        const auto first = sourceColour (slots.front().source);
        for (const auto& slot : slots)
            if (sourceColour (slot.source) != first)
                return mixedColour;
        return first;
    }

    void showModulateMenu (Parameters& parameters, int target, juce::Component& knob, std::function<void()> showSlots)
    {
        juce::PopupMenu sources;
        for (int source = 1; source < Mod::numSources; ++source)
            sources.addColouredItem (source, Mod::sourceNames[source], sourceColour (source));

        juce::PopupMenu menu;
        menu.addSectionHeader (Mod::targetName (target));
        menu.addSubMenu ("Modulate with", sources);
        const auto slots = slotsOn (parameters, target);
        for (const auto& slot : slots)
            menu.addItem (100 + slot.slot, "Remove slot " + juce::String (slot.slot + 1) + " (" + sourceShortName (slot.source) + ", "
                                               + (slot.amount > 0.0f ? "+" : "") + juce::String (juce::roundToInt (slot.amount * 100.0f)) + "%)");
        menu.addSeparator();
        menu.addItem (200, "Show the slots");

        juce::Component::SafePointer<juce::Component> safeKnob (&knob);
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                            [&parameters, target, safeKnob, showSlots] (int chosen)
        {
            if (chosen <= 0 || safeKnob == nullptr)
                return;
            if (chosen < Mod::numSources)
            {
                // A new slot at +50%: enough to hear, easy to change.
                if (addSlot (parameters, chosen, target, 0.5f) < 0)
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "All 16 slots are in use",
                                                            "Remove a slot (on the SLOTS page, or right-click a modulated knob) to add another.");
            }
            else if (chosen >= 100 && chosen < 100 + Mod::numSlots)
            {
                clearSlot (parameters, chosen - 100);
            }
            else if (chosen == 200 && showSlots != nullptr)
            {
                showSlots();
            }
            safeKnob->getParentComponent()->repaint();
        });
    }

    void paintModulation (juce::Graphics& g, juce::Component& parent, const ModSlider& slider, float offset, juce::Colour colour)
    {
        if (! slider.isVisible() || colour.isTransparent())
            return;
        // The knob's own ring, as ParagonLookAndFeel draws it, just outside.
        auto& mutableSlider = const_cast<ModSlider&> (slider);
        const auto layout = slider.getLookAndFeel().getSliderLayout (mutableSlider);
        const auto area = parent.getLocalArea (&slider, layout.sliderBounds.toFloat()).reduced (4.0f);
        const auto radius = juce::jmin (area.getWidth(), area.getHeight()) / 2.0f;
        const auto ring = radius - 3.0f + 3.5f;
        const auto centre = area.getCentre();
        const auto rotary = mutableSlider.getRotaryParameters();
        const auto base = (float) mutableSlider.valueToProportionOfLength (mutableSlider.getValue());
        const auto now = juce::jlimit (0.0f, 1.0f, base + offset);
        const auto angleAt = [&rotary] (float proportion) { return rotary.startAngleRadians + proportion * (rotary.endAngleRadians - rotary.startAngleRadians); };

        // A faint ring all round: this knob is modulated...
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, ring, ring, 0.0f, rotary.startAngleRadians, rotary.endAngleRadians, true);
        g.setColour (colour.withAlpha (0.3f));
        g.strokePath (track, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        // ...and, bright, from its setting to where the modulation has it now.
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, ring, ring, 0.0f, angleAt (juce::jmin (base, now)), angleAt (juce::jmax (base, now)), true);
        g.setColour (colour);
        g.strokePath (arc, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (centre.getPointOnCircumference (ring, angleAt (now))));
    }
}
