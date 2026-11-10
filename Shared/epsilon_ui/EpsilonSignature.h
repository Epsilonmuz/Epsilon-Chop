#pragma once
// Epsilon Sounds signature controls for any plugin's own LookAndFeel.
// Call these from drawRotarySlider / drawButtonBackground so every plugin in the line gets the same
// coin-edge knob (cached static layers, champagne lume tip) and machined keycap with a lume bar,
// whatever the rest of its panel looks like.
#include <juce_gui_basics/juce_gui_basics.h>
#include "EpsilonPalette.h"
#include "EpsilonControls.h"

namespace epsilon_ui::signature
{
// Knob in the area JUCE gives drawRotarySlider. Bipolar ranges (min < 0 < max) light the arc from 0.
inline void knob(juce::Graphics& g, juce::Rectangle<float> area, float pos, juce::Slider& slider, Tier tier)
{
    controls::State st;
    st.hover = slider.isMouseOverOrDragging();
    st.pressed = slider.isMouseButtonDown();
    st.disabled = ! slider.isEnabled();
    const auto range = slider.getRange();
    float origin = 0.0f;
    if (range.getStart() < 0.0 && range.getEnd() > 0.0)
        origin = (float) slider.valueToProportionOfLength(0.0);
    if (slider.getProperties().getWithDefault("bipolar", false))
        origin = 0.5f;
    controls::drawKnob(g, area, pos, origin, palette(tier), st, tier == Tier::Epsilon);
}

// Machined keycap. A toggle that is on shows the champagne lume bar.
inline void keycap(juce::Graphics& g, juce::Button& b, bool over, bool down, Tier tier)
{
    controls::State st;
    st.hover = over;
    st.pressed = down;
    st.on = b.getToggleState();
    st.disabled = ! b.isEnabled();
    const bool toggle = b.getClickingTogglesState() || b.getToggleState() || (bool) b.getProperties().getWithDefault("toggle", false);
    controls::drawKeycap(g, b.getLocalBounds().toFloat().reduced(0.5f), palette(tier), st, toggle);
}

// The label colour a keycap wants (ink when on, muted when off, faint when disabled).
inline juce::Colour keycapInk(const juce::Button& b, Tier tier)
{
    const auto p = palette(tier);
    if (! b.isEnabled()) return p.inkMuted.withAlpha(0.45f);
    return b.getToggleState() ? p.ink : p.inkMuted;
}
}
