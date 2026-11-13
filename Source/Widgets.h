#pragma once

#include "Theme.h"
#include "../Shared/epsilon_ui/EpsilonSignature.h"
#include <functional>

namespace ui
{
// Pill-shaped segmented selector. The selected segment is filled white.
class Segmented final : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void(int)> onChange;

    void setOptions(juce::StringArray labels, juce::Array<int> ids)
    {
        options = std::move(labels);
        values = std::move(ids);
        repaint();
    }
    void setSelected(int value)
    {
        if (selected != value) { selected = value; repaint(); }
    }
    int getSelected() const noexcept { return selected; }

    void paint(juce::Graphics& g) override
    {
        // Epsilon Sounds signature segmented switch: the selected cap carries the lume bar
        const auto pal = epsilon_ui::palette(epsilon_ui::Tier::Epsilon);
        const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        const auto count = options.size();
        std::vector<juce::Rectangle<float>> segs;
        int sel = -1;
        for (int i = 0; i < count; ++i) { segs.push_back(segment(i)); if (values[i] == selected) sel = i; }
        epsilon_ui::controls::drawSegmentTrack(g, bounds, segs, sel, pal);
        for (int i = 0; i < count; ++i)
        {
            epsilon_ui::controls::State st; st.on = i == sel; st.hover = i == hover;
            auto ink = epsilon_ui::controls::drawSegment(g, segs[(size_t) i], pal, st);
            if (!isEnabled()) ink = ink.withAlpha(0.45f);
            g.setColour(ink);
            g.setFont(theme::sans(fontHeight, true));
            g.drawFittedText(options[i], segs[(size_t) i].withTrimmedBottom(st.on ? 3.0f : 0.0f).toNearestInt(), juce::Justification::centred, 1);
        }
    }
    void mouseMove(const juce::MouseEvent& e) override { setHover(indexAt(e.position)); }
    void mouseExit(const juce::MouseEvent&) override { setHover(-1); }
    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto index = indexAt(e.position);
        if (index < 0 || !isEnabled())
            return;
        const auto wasSelected = values[index] == selected;
        selected = values[index];
        repaint();
        if (onChange && (!wasSelected || reportReselect))
            onChange(selected);
    }

    float fontHeight = 11.0f;
    bool reportReselect = false;

private:
    juce::Rectangle<float> segment(int index) const
    {
        const auto w = getWidth() / static_cast<float>(std::max(1, options.size()));
        return { index * w, 0.0f, w, static_cast<float>(getHeight()) };
    }
    int indexAt(juce::Point<float> p) const
    {
        for (int i = 0; i < options.size(); ++i)
            if (segment(i).contains(p)) return i;
        return -1;
    }
    void setHover(int index) { if (hover != index) { hover = index; repaint(); } }

    juce::StringArray options;
    juce::Array<int> values;
    int selected = -1, hover = -1;
};

// Rotary knob with a caption above and the value printed under it.
class Knob final : public juce::Component
{
public:
    explicit Knob(juce::String captionText) : caption(std::move(captionText))
    {
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.22f, juce::MathConstants<float>::pi * 2.78f, true);
        slider.setScrollWheelEnabled(false);
        slider.setMouseDragSensitivity(220);
        slider.onValueChange = [this] { repaint(); if (onValue) onValue(); };
        addAndMakeVisible(slider);
    }

    juce::Slider slider;
    std::function<juce::String(double)> format = [](double v) { return juce::String(juce::roundToInt(v)); };
    std::function<void()> onValue;

    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromTop(16);
        area.removeFromBottom(16);
        slider.setBounds(area);
    }
    void paint(juce::Graphics& g) override
    {
        g.setColour(theme::dim);
        g.setFont(theme::sans(11.0f, true));
        g.drawText(caption, getLocalBounds().removeFromTop(14), juce::Justification::centred);
        g.setColour(slider.isEnabled() ? theme::text : theme::faint);
        g.setFont(theme::mono(11.0f));
        g.drawText(format(slider.getValue()), getLocalBounds().removeFromBottom(14), juce::Justification::centred);
    }

private:
    juce::String caption;
};

// Large transport button. Lit with the live accent while the clock runs.
class PlayButton final : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void()> onClick;

    void setState(bool isPlaying, bool isSynced)
    {
        if (playing != isPlaying || synced != isSynced) { playing = isPlaying; synced = isSynced; repaint(); }
    }
    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(3.0f);
        const auto centre = bounds.getCentre();
        const auto lit = playing || synced;
        if (lit)
        {
            for (int ring = 3; ring >= 1; --ring)
            {
                g.setColour(theme::live.withAlpha(0.06f * ring));
                g.fillEllipse(bounds.expanded(static_cast<float>(4 - ring) * 1.2f));
            }
        }
        juce::ColourGradient shade(lit ? theme::live : theme::ink4, centre.x, bounds.getY(),
                                   lit ? theme::live.darker(0.35f) : theme::ink2, centre.x, bounds.getBottom(), false);
        g.setGradientFill(shade);
        g.fillEllipse(bounds);
        g.setColour(lit ? theme::live.brighter(0.4f) : theme::lineStrong);
        g.drawEllipse(bounds, 1.2f);

        const auto s = bounds.getWidth() * 0.2f;
        g.setColour(lit ? theme::ink0 : theme::text);
        if (lit)
            g.fillRoundedRectangle(centre.x - s * 0.8f, centre.y - s * 0.8f, s * 1.6f, s * 1.6f, 2.5f);
        else
        {
            juce::Path triangle;
            triangle.addTriangle(centre.x - s * 0.7f, centre.y - s, centre.x - s * 0.7f, centre.y + s, centre.x + s * 1.05f, centre.y);
            g.fillPath(triangle, {});
            g.strokePath(triangle, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        if (over && !lit)
        {
            g.setColour(theme::text.withAlpha(0.06f));
            g.fillEllipse(bounds);
        }
    }
    void mouseEnter(const juce::MouseEvent&) override { over = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { over = false; repaint(); }
    void mouseUp(const juce::MouseEvent& e) override
    {
        if (onClick && getLocalBounds().contains(e.getPosition())) onClick();
    }

private:
    bool playing = false, synced = false, over = false;
};

// The hero action. Its outline is drawn from the chop palette, so it reads as
// "these chops, rearranged".
class DiceButton final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    std::function<void()> onClick;
    juce::String title { "RANDOMIZE" };

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(1.5f);
        const auto radius = 14.0f;
        juce::ColourGradient rim(theme::chopColour(0), bounds.getX(), bounds.getY(), theme::chopColour(3), bounds.getRight(), bounds.getBottom(), false);
        rim.addColour(0.33, theme::chopColour(2));
        rim.addColour(0.66, theme::chopColour(1));
        g.setGradientFill(rim);
        g.fillRoundedRectangle(bounds, radius);
        auto inner = bounds.reduced(2.0f);
        juce::ColourGradient body(down ? theme::ink2 : (over ? theme::ink4 : theme::ink3), inner.getX(), inner.getY(),
                                  theme::ink1, inner.getX(), inner.getBottom(), false);
        g.setGradientFill(body);
        g.fillRoundedRectangle(inner, radius - 2.0f);

        // Dice glyph
        const auto die = juce::Rectangle<float>(inner.getX() + 18.0f, inner.getCentreY() - 15.0f, 30.0f, 30.0f);
        g.saveState();
        g.addTransform(juce::AffineTransform::rotation(spin, die.getCentreX(), die.getCentreY()));
        g.setColour(theme::text);
        g.fillRoundedRectangle(die, 7.0f);
        g.setColour(theme::ink0);
        constexpr std::array<std::array<float, 2>, 5> pips { { { .28f, .28f }, { .72f, .28f }, { .5f, .5f }, { .28f, .72f }, { .72f, .72f } } };
        for (const auto& p : pips)
            g.fillEllipse(die.getX() + die.getWidth() * p[0] - 2.6f, die.getY() + die.getHeight() * p[1] - 2.6f, 5.2f, 5.2f);
        g.restoreState();

        auto label = inner.withTrimmedLeft(58.0f);
        g.setColour(theme::text);
        g.setFont(theme::sans(17.0f, true));
        g.drawFittedText(title, label.removeFromTop(label.getHeight() * 0.62f).toNearestInt(), juce::Justification::bottomLeft, 1, 0.75f);
        g.setColour(theme::dim);
        g.setFont(theme::sans(11.0f));
        g.drawFittedText(subtitle, label.toNearestInt(), juce::Justification::topLeft, 1, 0.8f);
    }

    void setSubtitle(const juce::String& s) { if (s != subtitle) { subtitle = s; repaint(); } }
    void mouseEnter(const juce::MouseEvent&) override { over = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { over = false; repaint(); }
    void mouseDown(const juce::MouseEvent&) override { down = true; repaint(); }
    void mouseUp(const juce::MouseEvent& e) override
    {
        down = false;
        repaint();
        if (!getLocalBounds().contains(e.getPosition()) || !isEnabled())
            return;
        spin = 0.0f;
        startTimerHz(60);
        if (onClick) onClick();
    }

private:
    void timerCallback() override
    {
        spin += 0.32f;
        if (spin >= juce::MathConstants<float>::halfPi) { spin = 0.0f; stopTimer(); }
        repaint();
    }

    juce::String subtitle { juce::String::fromUTF8("Chops  ·  Rhythm  ·  Stutter") };
    float spin = 0.0f;
    bool over = false, down = false;
};

// Numeric readout that you drag up and down. Double-click resets.
class ValueBox final : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void(double)> onChange, onCommit;
    std::function<void()> onReset;
    juce::String caption, unit, tag;
    double value = 120.0, minimum = 40.0, maximum = 240.0, perPixel = 0.1;
    int decimals = 1;
    bool editable = true;
    float valueHeight = 20.0f;

    void setValue(double v) { if (std::abs(v - value) > 1.0e-6) { value = v; repaint(); } }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(theme::ink2);
        g.fillRoundedRectangle(bounds, 8.0f);
        g.setColour(dragging ? theme::lineStrong.brighter(0.3f) : theme::line);
        g.drawRoundedRectangle(bounds, 8.0f, 1.0f);
        auto area = getLocalBounds().reduced(10, 4);
        auto top = area.removeFromTop(13);
        g.setColour(theme::dim);
        g.setFont(theme::sans(10.5f, true));
        g.drawText(caption, top, juce::Justification::centredLeft);
        if (tag.isNotEmpty())
        {
            g.setColour(tagLive ? theme::live : theme::faint);
            g.drawText(tag, top, juce::Justification::centredRight);
        }
        g.setColour(editable ? theme::text : theme::text.withAlpha(0.85f));
        g.setFont(theme::mono(valueHeight));
        const auto number = juce::String(value, decimals);
        g.drawText(number, area, juce::Justification::centredLeft);
        g.setColour(theme::faint);
        g.setFont(theme::sans(10.0f, true));
        const auto numberWidth = juce::GlyphArrangement::getStringWidthInt(theme::mono(valueHeight), number);
        g.drawText(unit, area.withTrimmedLeft(numberWidth + 5), juce::Justification::centredLeft);
    }
    void mouseDown(const juce::MouseEvent&) override { start = value; dragging = editable; repaint(); }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!editable) return;
        const auto fine = e.mods.isShiftDown() ? 0.1 : 1.0;
        const auto next = juce::jlimit(minimum, maximum, start - e.getDistanceFromDragStartY() * perPixel * fine);
        const auto rounded = std::round(next * std::pow(10.0, decimals)) / std::pow(10.0, decimals);
        if (rounded != value) { value = rounded; repaint(); if (onChange) onChange(value); }
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if (dragging && value != start && onCommit) onCommit(value);
        dragging = false;
        repaint();
    }
    void mouseDoubleClick(const juce::MouseEvent&) override { if (editable && onReset) onReset(); }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        if (!editable) return;
        const auto step = std::pow(10.0, -decimals) * (w.deltaY > 0 ? 10.0 : -10.0);
        const auto next = juce::jlimit(minimum, maximum, value + step);
        if (next != value) { value = next; repaint(); if (onChange) onChange(value); if (onCommit) onCommit(value); }
    }

    bool tagLive = false;

private:
    double start = 0.0;
    bool dragging = false;
};
}
