#pragma once
// Epsilon Sounds signature control set v1 ("precision luxury") - drawing code.
// Implements Shared/epsilon_ui/CONTROLS-SPEC.md 1:1: coin-edge knobs with a watch-hand pointer and a
// floating champagne lume tip, machined keycaps whose "on" is a lume bar, pill toggles with an LED jewel,
// segmented switches, value windows, combo boxes, panels with the Hallmark's floating section bar.
// Plain functions of (Graphics, bounds, palette, state) so any plugin can use them with or without
// the shared LookAndFeel. Static knob layers are cached as images (Intel Mac rule).
#include "EpsilonPalette.h"
#include <map>

namespace epsilon_ui::controls
{
using juce::Colour;
using juce::Rectangle;
using juce::Point;

struct State
{
    bool hover = false, pressed = false, on = false, disabled = false, focus = false;
};

inline Colour mix(Colour a, Colour b, float t) { return a.interpolatedWith(b, t); }

// angle 0 = 12 o'clock, clockwise; returns the point at radius r
inline Point<float> polar(Point<float> c, float r, float a) { return { c.x + r * std::sin(a), c.y - r * std::cos(a) }; }

inline void bodyGradient(juce::Graphics& g, Rectangle<float> r, const Palette& p, const State& s)
{
    Colour top, bot;
    if (s.pressed)     { top = mix(p.surface, p.bg, 0.30f);  bot = mix(p.raised, p.surface, 0.30f); }
    else if (s.hover)  { top = mix(p.raised, p.ink, 0.11f);  bot = mix(p.raised, p.ink, 0.02f); }
    else               { top = mix(p.raised, p.ink, 0.07f);  bot = p.raised; }
    g.setGradientFill(juce::ColourGradient(top, 0.0f, r.getY(), bot, 0.0f, r.getBottom(), false));
}

inline float sealRadius(float h) { return std::min(0.25f * h, 8.0f); }

inline void focusRing(juce::Graphics& g, Rectangle<float> r, float radius, const Palette& p)
{
    g.setColour(p.focus);
    g.drawRoundedRectangle(r.expanded(3.5f), radius + 3.5f, 1.5f);
}

//==============================================================================
// Knob
struct KnobGeo
{
    float R, minorLen, majorLen, grooveR, grooveW, arcW, bezelOut, bezelIn, faceR, knurlW, handR0, handR1, handW, tipR0, tipR1, tipW, hubR, idxW, idxH;
    int ticks, majorEvery, knurl;
    std::vector<float> rings;
};

inline KnobGeo knobGeo(float D)
{
    // the three designed sizes; anything else scales from the nearest
    static const KnobGeo hero  { 48, 3, 4.5f, 41, 5, 2.5f, 37.5f, 30, 28, 0.8f, 6.5f, 18.5f, 2, 21, 26.5f, 3, 2.5f, 2.5f, 6, 40, 4, 104, { 8, 12.5f, 17, 21.5f } };
    static const KnobGeo main  { 30, 2, 2.75f, 25.5f, 3.5f, 2, 23, 18.5f, 17, 0.7f, 4, 11.5f, 1.6f, 13, 15.8f, 2.4f, 1.7f, 2, 4, 20, 2, 76, { 6.5f, 11 } };
    static const KnobGeo small { 20, 2, 2, 16.25f, 3, 1.5f, 14.25f, 11.25f, 10.25f, 0.6f, 2.8f, 6.8f, 1.3f, 7.8f, 9.6f, 1.9f, 1.2f, 1.8f, 3, 10, 1, 52, { 6 } };
    const KnobGeo& base = D >= 78 ? hero : D >= 50 ? main : small;
    const float k = D / (2.0f * base.R);
    KnobGeo g = base;
    for (float* f : { &g.R, &g.minorLen, &g.majorLen, &g.grooveR, &g.grooveW, &g.arcW, &g.bezelOut, &g.bezelIn, &g.faceR, &g.handR0, &g.handR1,
                      &g.handW, &g.tipR0, &g.tipR1, &g.tipW, &g.hubR, &g.idxW, &g.idxH })
        *f *= k;
    for (auto& r : g.rings) r *= k;
    return g;
}

constexpr float sweepStart = -0.75f * juce::MathConstants<float>::pi;   // -135 deg
constexpr float sweep = 1.5f * juce::MathConstants<float>::pi;          // 270 deg

// Static layers 1-9, drawn into a D x D area at (0,0).
inline void drawKnobStatic(juce::Graphics& g, float D, const Palette& p, const State& s)
{
    const auto G = knobGeo(D);
    const Point<float> c(D * 0.5f, D * 0.5f);
    auto disc = [&](float r) { return Rectangle<float>(r * 2, r * 2).withCentre(c); };
    // 1 bezel
    juce::ColourGradient bez(mix(p.raised, p.trim, s.hover ? 0.62f : 0.50f), 0.0f, c.y - G.bezelOut, mix(p.bg, p.trim, 0.12f), 0.0f, c.y + G.bezelOut, false);
    bez.addColour(0.45, mix(p.raised, p.trim, 0.16f));
    g.setGradientFill(bez);
    g.fillEllipse(disc(G.bezelOut));
    if (s.hover) { g.setColour(p.trim.withAlpha(0.7f)); g.drawEllipse(disc(G.bezelOut).reduced(0.5f), 1.0f); }
    // 2 knurl
    juce::Path knurl;
    for (int i = 0; i < G.knurl; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) i / (float) G.knurl;
        knurl.startNewSubPath(polar(c, G.bezelIn + 0.6f, a));
        knurl.lineTo(polar(c, G.bezelOut - 0.4f, a));
    }
    g.setColour(p.bg.withAlpha(0.55f));
    g.strokePath(knurl, juce::PathStrokeType(G.knurlW));
    // 3 gap disc, 4 face
    g.setColour(p.bg);
    g.fillEllipse(disc(G.bezelIn));
    g.setGradientFill(juce::ColourGradient(mix(p.raised, p.ink, s.pressed ? 0.035f : 0.07f), 0.0f, c.y - G.faceR,
                                           mix(p.surface, p.bg, 0.35f), 0.0f, c.y + G.faceR, false));
    g.fillEllipse(disc(G.faceR));
    // 5 finish rings
    juce::Path rings;
    for (auto r : G.rings) rings.addEllipse(disc(r));
    g.setColour(p.ink.withAlpha(0.035f));
    g.strokePath(rings, juce::PathStrokeType(0.6f));
    // 6 face hairline
    juce::ColourGradient hl(p.ink.withAlpha(0.28f), 0.0f, c.y - G.faceR, p.ink.withAlpha(0.05f), 0.0f, c.y + G.faceR, false);
    hl.addColour(0.5, p.ink.withAlpha(0.0f));
    g.setGradientFill(hl);
    g.drawEllipse(disc(G.faceR - 0.5f), 1.0f);
    // 7 minute track (centre tick replaced by the index)
    juce::Path ticks;
    for (int i = 0; i <= G.ticks; ++i)
    {
        if (i * 2 == G.ticks) continue;
        const float a = sweepStart + sweep * (float) i / (float) G.ticks;
        const float len = (i % G.majorEvery == 0) ? G.majorLen : G.minorLen;
        ticks.startNewSubPath(polar(c, G.R, a));
        ticks.lineTo(polar(c, G.R - len, a));
    }
    g.setColour(p.inkMuted.withAlpha(s.hover ? 0.85f : 0.6f));
    g.strokePath(ticks, juce::PathStrokeType(1.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
    // 8 lume groove
    juce::Path groove;
    groove.addCentredArc(c.x, c.y, G.grooveR, G.grooveR, 0.0f, sweepStart, sweepStart + sweep, true);
    g.setColour(p.bg);
    g.strokePath(groove, juce::PathStrokeType(G.grooveW, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    // 9 index + hub jewel
    juce::Path jewel;
    jewel.addRectangle(c.x - G.idxW * 0.5f, c.y - G.R, G.idxW, G.idxH);
    jewel.addEllipse(Rectangle<float>(G.hubR * 2, G.hubR * 2).withCentre(c));
    g.setColour(p.trim);
    g.fillPath(jewel);
}

// Whole knob in bounds (square area centred). origin = proportion (0..1) the arc starts from (0 unipolar, value-0 for bipolar).
inline void drawKnob(juce::Graphics& g, Rectangle<float> bounds, float pos, float origin, const Palette& p, const State& s, bool lightTier)
{
    const float D = std::floor(std::min(bounds.getWidth(), bounds.getHeight()));
    if (D < 8) return;
    const auto area = Rectangle<float>(D, D).withCentre(bounds.getCentre());
    // ---- static layers from cache (tier, D, state, scale)
    const float scale = std::max(1.0f, (float) g.getInternalContext().getPhysicalPixelScaleFactor());
    const int st = s.disabled ? 3 : s.pressed ? 2 : s.hover ? 1 : 0;
    const auto key = juce::String(lightTier ? "e" : "m") + juce::String((int) D) + "_" + juce::String(st) + "_" + juce::String(scale, 2);
    static std::map<juce::String, juce::Image> cache;
    auto it = cache.find(key);
    if (it == cache.end())
    {
        if (cache.size() > 64) cache.clear();
        const int px = juce::roundToInt(D * scale);
        juce::Image img(juce::Image::ARGB, px, px, true, juce::SoftwareImageType());
        juce::Graphics ig(img);
        ig.addTransform(juce::AffineTransform::scale(scale));
        drawKnobStatic(ig, D, p, s);
        it = cache.emplace(key, img).first;
    }
    g.setOpacity(s.disabled ? 0.4f : 1.0f);
    g.drawImage(it->second, area, juce::RectanglePlacement::stretchToFit);
    g.setOpacity(1.0f);

    // ---- dynamic layers 10-12
    const auto G = knobGeo(D);
    const auto c = area.getCentre();
    const float a = sweepStart + sweep * juce::jlimit(0.0f, 1.0f, pos);
    const float a0 = sweepStart + sweep * juce::jlimit(0.0f, 1.0f, origin);
    const Colour lume = s.disabled ? p.inkMuted.withAlpha(0.4f) : p.live;
    if (std::abs(a - a0) > 0.0087f)
    {
        juce::Path arc;
        arc.addCentredArc(c.x, c.y, G.grooveR, G.grooveR, 0.0f, std::min(a0, a), std::max(a0, a), true);
        if (s.pressed && ! s.disabled)
        {
            g.setColour(p.live.withAlpha(0.28f));
            g.strokePath(arc, juce::PathStrokeType(G.grooveW + 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        }
        g.setColour(lume);
        g.strokePath(arc, juce::PathStrokeType(G.arcW, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    }
    g.setColour(s.disabled ? p.ink.withAlpha(0.4f) : p.ink);
    g.drawLine({ polar(c, G.handR0, a), polar(c, G.handR1, a) }, G.handW);
    juce::Path tip;
    tip.startNewSubPath(polar(c, G.tipR0, a));
    tip.lineTo(polar(c, G.tipR1, a));
    g.setColour(lume);
    g.strokePath(tip, juce::PathStrokeType(G.tipW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (s.focus)
    {
        const float side = D + 7.0f;
        g.setColour(p.focus);
        g.drawRoundedRectangle(Rectangle<float>(side, side).withCentre(c), 0.25f * side, 1.5f);
    }
}

//==============================================================================
// Keycap (momentary / toggle). Returns the label colour to use.
inline Colour drawKeycap(juce::Graphics& g, Rectangle<float> b, const Palette& p, const State& s, bool isToggle)
{
    const float r = sealRadius(b.getHeight());
    auto body = b.reduced(0.5f);
    if (s.pressed) body = body.translated(0.0f, 1.0f);
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.fillRoundedRectangle(b.translated(0.0f, 1.0f), r);
    bodyGradient(g, body, p, s);
    g.fillRoundedRectangle(body, r);
    g.setColour(s.on ? p.live.withAlpha(0.6f) : s.hover ? mix(p.line, p.trim, 0.5f) : p.line);
    g.drawRoundedRectangle(body, r, 1.0f);
    if (s.on) { g.setColour(p.live.withAlpha(0.09f)); g.fillRoundedRectangle(body.reduced(1.0f), r - 1.0f); }
    if (! s.pressed)
    {
        g.setColour(p.ink.withAlpha(0.16f));
        g.fillRect(body.getX() + r, body.getY() + 1.5f, body.getWidth() - 2 * r, 1.0f);
    }
    if (isToggle)
    {
        const float wl = std::min(16.0f, 0.22f * body.getWidth());
        const float cx = body.getCentreX();
        if (s.on)
        {
            g.setColour(p.live.withAlpha(0.28f));
            g.fillRoundedRectangle(cx - (wl + 4) * 0.5f, body.getBottom() - 8.0f, wl + 4, 6.0f, 3.0f);
        }
        g.setColour(s.on ? p.live : juce::Colours::black.withAlpha(0.38f));
        g.fillRoundedRectangle(cx - wl * 0.5f, body.getBottom() - 6.0f, wl, 2.0f, 1.0f);
    }
    if (s.focus) focusRing(g, b, r, p);
    if (s.on) return p.live;
    if (! isToggle) return p.ink;
    return (s.hover || s.pressed) ? p.ink : p.inkMuted;
}

// Pill toggle with LED jewel. Returns label colour; labelArea receives where the text goes.
inline Colour drawPill(juce::Graphics& g, Rectangle<float> b, const Palette& p, const State& s, Rectangle<float>& labelArea)
{
    const float r = b.getHeight() * 0.5f;
    auto body = b.reduced(0.5f);
    if (s.pressed) body = body.translated(0.0f, 1.0f);
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.fillRoundedRectangle(b.translated(0.0f, 1.0f), r);
    bodyGradient(g, body, p, s);
    g.fillRoundedRectangle(body, r);
    g.setColour(s.on ? p.live.withAlpha(0.6f) : s.hover ? mix(p.line, p.trim, 0.5f) : p.line);
    g.drawRoundedRectangle(body, r, 1.0f);
    if (s.on) { g.setColour(p.live.withAlpha(0.09f)); g.fillRoundedRectangle(body.reduced(1.0f), r - 1.0f); }
    if (! s.pressed) { g.setColour(p.ink.withAlpha(0.16f)); g.fillRect(body.getX() + r, body.getY() + 1.5f, body.getWidth() - 2 * r, 1.0f); }
    const Point<float> led(body.getX() + r + 1.0f, body.getCentreY());
    if (s.on)
    {
        g.setColour(p.live.withAlpha(0.25f)); g.fillEllipse(Rectangle<float>(10, 10).withCentre(led));
        g.setColour(p.live); g.fillEllipse(Rectangle<float>(5.5f, 5.5f).withCentre(led));
    }
    else
    {
        g.setColour(p.bg); g.fillEllipse(Rectangle<float>(5.5f, 5.5f).withCentre(led));
        g.setColour(p.line); g.drawEllipse(Rectangle<float>(5.5f, 5.5f).withCentre(led), 1.0f);
    }
    labelArea = body.withTrimmedLeft(r + 2.0f);
    if (s.focus) focusRing(g, b, r, p);
    return s.on ? p.live : (s.hover || s.pressed) ? p.ink : p.inkMuted;
}

// Segmented switch track + dividers (selected index sel, segment rects given).
inline void drawSegmentTrack(juce::Graphics& g, Rectangle<float> b, const std::vector<Rectangle<float>>& segs, int sel, const Palette& p)
{
    const float r = std::min(0.25f * b.getHeight(), 7.0f);
    g.setColour(p.bg);
    g.fillRoundedRectangle(b.reduced(0.5f), r);
    g.setColour(p.line);
    g.drawRoundedRectangle(b.reduced(0.5f), r, 1.0f);
    juce::Path div;
    for (int i = 1; i < (int) segs.size(); ++i)
    {
        if (i == sel || i - 1 == sel) continue;
        if (std::abs(segs[(size_t) i].getY() - segs[(size_t) i - 1].getY()) > 1.0f) continue;   // new row
        const float x = segs[(size_t) i].getX();
        div.addRectangle(x - 0.5f, segs[(size_t) i].getY() + 7.0f, 1.0f, segs[(size_t) i].getHeight() - 14.0f);
    }
    g.fillPath(div);
}

// One segment's cap (selected / hover ghost / pressed ghost). Returns label colour.
inline Colour drawSegment(juce::Graphics& g, Rectangle<float> seg, const Palette& p, const State& s)
{
    const float r = std::max(2.0f, std::min(0.25f * seg.getHeight(), 7.0f) - 2.0f);
    const auto cap = seg.reduced(2.5f);
    if (s.on)
    {
        bodyGradient(g, cap, p, {});
        g.fillRoundedRectangle(cap, r);
        g.setColour(p.live.withAlpha(0.5f));
        g.drawRoundedRectangle(cap, r, 1.0f);
        g.setColour(p.live.withAlpha(0.07f));
        g.fillRoundedRectangle(cap.reduced(0.5f), r);
        g.setColour(p.ink.withAlpha(0.16f));
        g.fillRect(cap.getX() + r, seg.getY() + 4.0f, cap.getWidth() - 2 * r, 1.0f);
        const float w = std::min(14.0f, 0.2f * seg.getWidth());
        g.setColour(p.live.withAlpha(0.28f));
        g.fillRoundedRectangle(seg.getCentreX() - (w + 4) * 0.5f, seg.getBottom() - 8.0f, w + 4, 6.0f, 3.0f);
        g.setColour(p.live);
        g.fillRoundedRectangle(seg.getCentreX() - w * 0.5f, seg.getBottom() - 6.0f, w, 2.0f, 1.0f);
        return p.live;
    }
    if (s.pressed) { g.setColour(juce::Colours::black.withAlpha(0.25f)); g.fillRoundedRectangle(cap, r); return p.ink; }
    if (s.hover)   { g.setColour(p.raised.withAlpha(0.55f)); g.fillRoundedRectangle(cap, r); return p.ink; }
    return p.inkMuted;
}

//==============================================================================
// Value ("date") window. Draws frame, window and inner shadow; caller draws the text inside.
inline void drawValueWindow(juce::Graphics& g, Rectangle<float> b, const Palette& p, bool hover)
{
    const float r = 0.25f * b.getHeight();
    g.setColour(p.trim.withAlpha(hover ? 0.75f : 0.4f));
    g.drawRoundedRectangle(b.expanded(2.0f), r + 2.0f, 1.0f);
    g.setColour(p.bg);
    g.fillRoundedRectangle(b, r);
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.fillRect(b.getX() + r, b.getY() + 0.75f, b.getWidth() - 2 * r, 1.5f);
}

// Combo box body (keycap recipe + divider + chevron).
inline void drawCombo(juce::Graphics& g, Rectangle<float> b, const Palette& p, const State& s, bool open)
{
    const float r = 7.0f;
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.fillRoundedRectangle(b.translated(0.0f, 1.0f), r);
    auto body = b.reduced(0.5f);
    bodyGradient(g, body, p, s);
    g.fillRoundedRectangle(body, r);
    g.setColour(open ? p.live.withAlpha(0.6f) : s.hover ? mix(p.line, p.trim, 0.5f) : p.line);
    g.drawRoundedRectangle(body, r, 1.0f);
    g.setColour(p.ink.withAlpha(0.16f));
    g.fillRect(body.getX() + r, body.getY() + 1.5f, body.getWidth() - 2 * r, 1.0f);
    const float h = b.getHeight();
    g.setColour(p.line);
    g.fillRect(b.getRight() - h, b.getY() + 6.0f, 1.0f, h - 12.0f);
    const auto cc = Point<float>(b.getRight() - h * 0.5f, b.getCentreY());
    juce::Path chev;
    const float d = open ? -2.0f : 2.0f;
    chev.startNewSubPath(cc.x - 3.5f, cc.y - d); chev.lineTo(cc.x, cc.y + d); chev.lineTo(cc.x + 3.5f, cc.y - d);
    g.setColour(open ? p.live : p.inkMuted);
    g.strokePath(chev, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (s.focus) focusRing(g, b, r, p);
}

//==============================================================================
// Section label: the Hallmark's floating bar + spaced caps.
template <typename FontFn>
inline void drawSectionLabel(juce::Graphics& g, Point<float> at, const juce::String& text, const Palette& p, FontFn&& makeFont)
{
    g.setColour(p.trim);
    g.fillRoundedRectangle(at.x, at.y - 1.25f, 10.0f, 2.5f, 0.5f);
    auto f = makeFont(10.0f);
    g.setFont(f.withExtraKerningFactor(2.2f / 10.0f));
    g.setColour(p.inkMuted);
    g.drawText(text.toUpperCase(), juce::Rectangle<float>(at.x + 15.0f, at.y - 8.0f, 400.0f, 16.0f), juce::Justification::centredLeft, false);
}

// Panel frame (r 10) with optional section title.
template <typename FontFn>
inline void drawPanelFrame(juce::Graphics& g, Rectangle<float> r, const Palette& p, const juce::String& title, FontFn&& makeFont)
{
    g.setColour(p.surface);
    g.fillRoundedRectangle(r.reduced(0.5f), 10.0f);
    g.setColour(p.line);
    g.drawRoundedRectangle(r.reduced(0.5f), 10.0f, 1.0f);
    g.setColour(p.ink.withAlpha(0.07f));
    g.fillRect(r.getX() + 10.0f, r.getY() + 1.5f, r.getWidth() - 20.0f, 1.0f);
    if (title.isNotEmpty()) drawSectionLabel(g, { r.getX() + 16.0f, r.getY() + 20.0f }, title, p, makeFont);
}

// Recessed well (waveform, analyser, trays).
inline void drawWell(juce::Graphics& g, Rectangle<float> r, const Palette& p)
{
    g.setColour(p.bg);
    g.fillRoundedRectangle(r, 8.0f);
    g.setColour(p.line);
    g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, 1.0f);
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.fillRect(r.getX() + 8.0f, r.getY() + 0.75f, r.getWidth() - 16.0f, 1.5f);
}
} // namespace epsilon_ui::controls
