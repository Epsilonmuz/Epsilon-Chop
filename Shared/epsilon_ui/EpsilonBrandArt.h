#pragma once
// Epsilon Sounds brand art as SVG path data, so every plugin carries the marks as vectors.
// Master mark: the Hallmark (curved epsilon with a free-floating bar, in a rounded seal), v1 2026-11-09.
// Usage: hallmark() from 32 px up; hallmarkSmall() (solid seal, cut-out epsilon) for 16-31 px.
// Ink is the tier's trim colour: gold on Muzi, platinum on Epsilon. Never champagne (that means "live").
#include <juce_gui_basics/juce_gui_basics.h>

namespace epsilon_ui::brand
{
inline const char* hallmarkSvg()      { return R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 48 48" width="48" height="48"><g fill="{INK}" fill-rule="evenodd"><path d="M12 0H36A12 12 0 0 1 48 12V36A12 12 0 0 1 36 48H12A12 12 0 0 1 0 36V12A12 12 0 0 1 12 0ZM12 5H36A7 7 0 0 1 43 12V36A7 7 0 0 1 36 43H12A7 7 0 0 1 5 36V12A7 7 0 0 1 12 5Z"/><path d="M36 12H24A12 12 0 0 0 24 36H36V30H24A6 6 0 0 1 24 18H36Z"/><path d="M21 21H33V27H21Z"/></g></svg>
)SVG"; }
inline const char* hallmarkSmallSvg() { return R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 48 48" width="48" height="48"><g fill="{INK}" fill-rule="evenodd"><path d="M12 3H36A9 9 0 0 1 45 12V36A9 9 0 0 1 36 45H12A9 9 0 0 1 3 36V12A9 9 0 0 1 12 3ZM39 9H24A15 15 0 0 0 24 39H39V33H24A9 9 0 0 1 24 15H39ZM21 21V27H36V21Z"/></g></svg>
)SVG"; }
// "MUZI" in the drawn high-contrast serif, 352 x 101 units.
inline const char* muziWordmarkSvg()  { return R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 352 101"><g transform="translate(0 0)" fill="{INK}"><g transform="translate(0 0)"><path d="M0 97H24V100H0ZM10 0H14V100H10ZM0 0H12V3H0ZM10 0H25L56 100H50L12 6ZM50 100L88 0H92L54 100ZM86 0H108V3H101V97H108V100H80V97H87V3H86Z"/></g><g transform="translate(120 0)"><path d="M12 3V64C12 88 30 101 52 101C73 101 87 88 87 64V3H83V64C83 84 71 95 55 95C37 95 27 84 27 64V3ZM3 0H36V3H3ZM75 0H95V3H75Z"/></g><g transform="translate(228 0)"><path d="M6 0H74V5H6ZM6 0H9V20H6ZM59 5H74L19 95H4ZM3 95H75V100H3ZM72 78H75V100H72Z"/></g><g transform="translate(316 0)"><path d="M9 3H24V97H9ZM0 0H33V3H0ZM0 97H33V100H0Z"/></g></g></svg>)SVG"; }

inline std::unique_ptr<juce::Drawable> fromSvg(const char* svg, juce::Colour ink)
{
    const auto text = juce::String(svg).replace("{INK}", "#" + ink.toDisplayString(false));
    return juce::Drawable::createFromSVGString(text);   // JUCE 9 API
}
inline std::unique_ptr<juce::Drawable> hallmark(juce::Colour ink, bool small = false) { return fromSvg(small ? hallmarkSmallSvg() : hallmarkSvg(), ink); }
inline std::unique_ptr<juce::Drawable> muziWordmark(juce::Colour ink) { return fromSvg(muziWordmarkSvg(), ink); }

// Plain vector outline in the SVG's own units (safe to keep in a static: no Component inside).
// evenOdd must match the SVG's fill-rule: the hallmarks use evenodd (the seal's hole), MUZI uses nonzero.
inline juce::Path outline(const char* svg, bool evenOdd)
{
    juce::Path p;
    if (auto d = fromSvg(svg, juce::Colours::white))
    {
        p = d->getOutlineAsPath();
        p.setUsingNonZeroWinding(! evenOdd);
    }
    return p;
}
inline const juce::Path& hallmarkPath(bool small)
{
    static const juce::Path big = outline(hallmarkSvg(), true), sm = outline(hallmarkSmallSvg(), true);
    return small ? sm : big;
}
inline const juce::Path& muziWordmarkPath()
{
    static const juce::Path p = outline(muziWordmarkSvg(), false);
    return p;
}
// Fill an outline into an area keeping proportions; returns the drawn bounds.
inline juce::Rectangle<float> fillOutline(juce::Graphics& g, const juce::Path& p, juce::Rectangle<float> area, juce::RectanglePlacement placement, juce::Colour ink)
{
    const auto b = p.getBounds();
    if (b.isEmpty()) return {};
    const auto t = placement.getTransformToFit(b, area);
    g.setColour(ink);
    g.fillPath(p, t);
    return b.transformedBy(t);
}
} // namespace epsilon_ui::brand
