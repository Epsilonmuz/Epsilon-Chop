#include "Theme.h"
#include "BinaryData.h"
#include "../Shared/epsilon_ui/EpsilonSignature.h"

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
    // Epsilon Sounds signature keycap; a toggle that is on carries the champagne lume bar
    epsilon_ui::signature::keycap(g, button, over, down, epsilon_ui::Tier::Epsilon);
}

void Look::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool over, bool)
{
    const auto on = button.getToggleState();
    // Action buttons read brighter than switched-off toggles.
    auto colour = on ? epsilon_ui::palette(epsilon_ui::Tier::Epsilon).live : (over ? text : (button.getClickingTogglesState() ? dim : text.withAlpha(0.88f)));
    if (!button.isEnabled())
        colour = faint;
    g.setColour(colour);
    g.setFont(getTextButtonFont(button, button.getHeight()));
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(6, 0).withTrimmedBottom(button.getClickingTogglesState() ? 3 : 0),
                     juce::Justification::centred, 1);
}

void Look::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider& slider)
{
    // Epsilon Sounds signature coin-edge knob (static layers cached)
    epsilon_ui::signature::knob(g, juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h).reduced(2.0f), pos, slider,
                                epsilon_ui::Tier::Epsilon);
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
