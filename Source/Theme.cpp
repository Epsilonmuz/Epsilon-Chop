#include "Theme.h"
#include "BinaryData.h"

namespace theme
{
namespace
{
struct Faces
{
    // Epsilon Dial, the Epsilon Sounds house typeface. Its figures are tabular, so the
    // readouts that used a monospace face use Dial Bold and still don't jitter.
    juce::Typeface::Ptr regular = juce::Typeface::createSystemTypefaceFor(
        BinaryData::EpsilonDialRegular_ttf, BinaryData::EpsilonDialRegular_ttfSize);
    juce::Typeface::Ptr bold = juce::Typeface::createSystemTypefaceFor(
        BinaryData::EpsilonDialBold_ttf, BinaryData::EpsilonDialBold_ttfSize);
    juce::Typeface::Ptr monoBold = bold;
};

const Faces& faces()
{
    static const Faces instance;
    return instance;
}
}

juce::Colour chopColour(int index)
{
    // Eight hues spaced so neighbouring chops never look alike.
    static const std::array<juce::Colour, 8> palette {
        juce::Colour { 0xffff6a4d },  // ember
        juce::Colour { 0xff4fd8c4 },  // lagoon
        juce::Colour { 0xffffb84a },  // saffron
        juce::Colour { 0xff8f86ff },  // iris
        juce::Colour { 0xffff6fae },  // rose
        juce::Colour { 0xff5cb8ff },  // sky
        juce::Colour { 0xffff9a6b },  // peach
        juce::Colour { 0xffc583ff },  // orchid
    };
    return palette[static_cast<std::size_t>(((index % 8) + 8) % 8)];
}

juce::Font sans(float height, bool bold)
{
    const auto& f = faces();
    return juce::Font(juce::FontOptions(bold ? f.bold : f.regular).withHeight(height));
}

juce::Font mono(float height)
{
    return juce::Font(juce::FontOptions(faces().monoBold).withHeight(height));
}

juce::String noteName(int note)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String(names[((note % 12) + 12) % 12]) + juce::String(note / 12);
}

Look::Look()
{
    setColour(juce::PopupMenu::backgroundColourId, ink2);
    setColour(juce::PopupMenu::textColourId, text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, ink4);
    setColour(juce::PopupMenu::highlightedTextColourId, text);
    setColour(juce::ComboBox::textColourId, text);
    setColour(juce::ComboBox::arrowColourId, dim);
    setColour(juce::TextButton::textColourOffId, dim);
    setColour(juce::TextButton::textColourOnId, ink0);
    setColour(juce::Slider::textBoxTextColourId, text);
    setColour(juce::Label::textColourId, text);
    setColour(juce::TooltipWindow::backgroundColourId, ink3);
    setColour(juce::TooltipWindow::textColourId, text);
    setColour(juce::TooltipWindow::outlineColourId, lineStrong);
}

juce::Typeface::Ptr Look::getTypefaceForFont(const juce::Font& font)
{
    if (font.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        return font.isBold() ? faces().bold : faces().regular;
    return LookAndFeel_V4::getTypefaceForFont(font);
}

void Look::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&, bool over, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    const auto radius = std::min(bounds.getHeight() * 0.5f, 8.0f);
    const auto on = button.getToggleState();
    const auto enabled = button.isEnabled();

    auto fill = on ? text : (over && enabled ? ink4 : ink3);
    if (down && enabled)
        fill = fill.darker(0.15f);
    if (!enabled)
        fill = ink2;
    g.setColour(fill);
    g.fillRoundedRectangle(bounds, radius);
    if (!on)
    {
        g.setColour(enabled ? lineStrong : line);
        g.drawRoundedRectangle(bounds, radius, 1.0f);
    }
}

void Look::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool over, bool)
{
    const auto on = button.getToggleState();
    // Action buttons read brighter than switched-off toggles.
    auto colour = on ? ink0 : (over ? text : (button.getClickingTogglesState() ? dim : text.withAlpha(0.88f)));
    if (!button.isEnabled())
        colour = faint;
    g.setColour(colour);
    g.setFont(getTextButtonFont(button, button.getHeight()));
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(6, 0), juce::Justification::centred, 1);
}

void Look::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& slider)
{
    const auto size = static_cast<float>(std::min(w, h));
    const auto centre = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                               static_cast<float>(w), static_cast<float>(h)).getCentre();
    const auto radius = size * 0.5f - 3.0f;
    const auto angle = start + pos * (end - start);
    const auto enabled = slider.isEnabled();

    // Track ring
    juce::Path track;
    track.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, start, end, true);
    g.setColour(line);
    g.strokePath(track, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Value arc grows from the centre for bipolar knobs
    const auto bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const auto from = bipolar ? (start + end) * 0.5f : start;
    juce::Path value;
    value.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, std::min(from, angle), std::max(from, angle), true);
    g.setColour(enabled ? text : faint);
    g.strokePath(value, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Cap
    const auto cap = radius - 6.0f;
    juce::ColourGradient shade(ink4, centre.x, centre.y - cap, ink2, centre.x, centre.y + cap, false);
    g.setGradientFill(shade);
    g.fillEllipse(centre.x - cap, centre.y - cap, cap * 2.0f, cap * 2.0f);
    g.setColour(lineStrong.withAlpha(0.8f));
    g.drawEllipse(centre.x - cap, centre.y - cap, cap * 2.0f, cap * 2.0f, 1.0f);

    // Pointer
    const auto tip = centre.getPointOnCircumference(cap - 3.0f, angle);
    const auto base = centre.getPointOnCircumference(cap * 0.35f, angle);
    g.setColour(enabled ? text : faint);
    g.drawLine({ base, tip }, 2.0f);
}

void Look::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                            juce::Slider::SliderStyle, juce::Slider& slider)
{
    const auto mid = static_cast<float>(y) + h * 0.5f;
    const auto left = static_cast<float>(x) + 6.0f;
    const auto right = static_cast<float>(x + w) - 6.0f;
    g.setColour(line);
    g.fillRoundedRectangle(left, mid - 2.0f, right - left, 4.0f, 2.0f);
    g.setColour(slider.isEnabled() ? text : faint);
    g.fillRoundedRectangle(left, mid - 2.0f, std::max(0.0f, pos - left), 4.0f, 2.0f);
    g.fillEllipse(pos - 6.0f, mid - 6.0f, 12.0f, 12.0f);
    g.setColour(ink0);
    g.fillEllipse(pos - 2.0f, mid - 2.0f, 4.0f, 4.0f);
}

void Look::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)).reduced(0.5f);
    const auto radius = std::min(bounds.getHeight() * 0.5f, 8.0f);
    g.setColour(box.isMouseOver(true) ? ink4 : ink3);
    g.fillRoundedRectangle(bounds, radius);
    g.setColour(lineStrong);
    g.drawRoundedRectangle(bounds, radius, 1.0f);
    juce::Path arrow;
    const auto ax = bounds.getRight() - 14.0f, ay = bounds.getCentreY();
    arrow.startNewSubPath(ax - 4.0f, ay - 2.0f);
    arrow.lineTo(ax, ay + 2.0f);
    arrow.lineTo(ax + 4.0f, ay - 2.0f);
    g.setColour(dim);
    g.strokePath(arrow, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void Look::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(4, 0, box.getWidth() - 24, box.getHeight());
    label.setFont(getComboBoxFont(box));
    label.setJustificationType(juce::Justification::centredLeft);
}

void Look::drawPopupMenuBackground(juce::Graphics& g, int w, int h)
{
    g.fillAll(ink2);
    g.setColour(lineStrong);
    g.drawRect(0, 0, w, h);
}
}
