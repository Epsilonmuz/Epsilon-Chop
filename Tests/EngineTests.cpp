#include "ChopEngine.h"

#include <algorithm>
#include <array>
#include <vector>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
constexpr double rate = 48000.0;
int failures = 0;

void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << "\n";
        ++failures;
    }
}

// 2 seconds = one bar at 120 BPM, eight decaying hits on the eighth notes.
std::shared_ptr<chop::Sample> makeSample(int channels = 2)
{
    auto sample = std::make_shared<chop::Sample>();
    sample->sampleRate = rate;
    sample->channels = channels;
    const auto frames = static_cast<int>(rate * 2.0);
    for (int c = 0; c < channels; ++c)
        sample->pcm[c].resize(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i)
    {
        const auto t = i / rate;
        const auto age = std::fmod(t, 0.25);
        const auto hit = static_cast<int>(t / 0.25);
        const auto v = static_cast<float>(0.8 * std::exp(-25.0 * age) * std::sin(2.0 * 3.14159265 * (180.0 + 40.0 * hit) * age));
        for (int c = 0; c < channels; ++c)
            sample->pcm[c][static_cast<std::size_t>(i)] = v;
    }
    for (int hit = 1; hit < 8; ++hit)
        sample->onsets.push_back({ static_cast<int>(hit * 0.25 * rate), hit % 2 == 0 ? 1.0f : 0.2f });
    sample->strongest = 1.0f;
    sample->estimatedBpm = chop::estimateLoopBpm(frames, rate);
    return sample;
}

chop::Session makeSession(chop::SliceMode mode, int density)
{
    chop::Session session;
    session.sample = makeSample();
    session.edit = chop::defaultEdit();
    session.edit.mode = mode;
    session.edit.density = density;
    session.slices = chop::makeSlices(*session.sample, session.edit);
    session.sampleRevision = 1;
    return session;
}

struct Render
{
    std::vector<float> left;
    std::vector<int> steps;
};

Render render(const chop::Session& session, const chop::PlaySettings& settings, int frames, int block,
              bool hostPlaying, double bpm = 120.0)
{
    chop::Engine engine;
    engine.prepare(rate);
    Render out;
    out.left.resize(static_cast<std::size_t>(frames));
    out.steps.resize(static_cast<std::size_t>(frames));
    for (int offset = 0; offset < frames; offset += block)
    {
        const auto count = std::min(block, frames - offset);
        chop::Transport transport;
        transport.bpm = bpm;
        transport.playing = hostPlaying;
        transport.hasPosition = true;
        transport.ppq = offset * (bpm / 60.0) / rate;
        engine.beginBlock(session, transport, settings, count);
        for (int i = 0; i < count; ++i)
        {
            float l = 0.0f, r = 0.0f;
            engine.render(i, l, r);
            out.left[static_cast<std::size_t>(offset + i)] = l;
            out.steps[static_cast<std::size_t>(offset + i)] = engine.currentStep();
        }
        engine.endBlock();
    }
    return out;
}

int firstSound(const std::vector<float>& audio, int from, float threshold = 1.0e-3f)
{
    for (int i = from; i < static_cast<int>(audio.size()); ++i)
        if (std::abs(audio[static_cast<std::size_t>(i)]) > threshold)
            return i;
    return -1;
}

void slicingModes()
{
    const auto sample = makeSample();
    const auto frames = sample->length();
    require(std::abs(sample->estimatedBpm - 120.0) < 0.01, "loop BPM estimate is 120");
    require(std::abs(chop::estimateLoopBpm(static_cast<int>(rate * 60.0 / 92.0 * 8.0), rate) - 92.0) < 0.01, "2-bar 92 BPM loop");

    for (int m = 0; m < chop::sliceModeCount; ++m)
    {
        auto edit = chop::defaultEdit();
        edit.mode = static_cast<chop::SliceMode>(m);
        for (int density : { 4, 8, 16, 32 })
        {
            edit.density = density;
            const auto slices = chop::makeSlices(*sample, edit);
            require(chop::validSlices(slices, frames), "valid slices for mode " + std::to_string(m));
        }
    }

    auto edit = chop::defaultEdit();
    edit.mode = chop::SliceMode::transient;
    edit.sensitivity = 100;
    edit.snapZero = false;
    edit.density = 16;
    auto slices = chop::makeSlices(*sample, edit);
    require(slices.count == 8, "transient finds the 8 hits");
    edit.sensitivity = 0;
    require(chop::makeSlices(*sample, edit).count < 8, "low sensitivity keeps fewer chops");

    edit.mode = chop::SliceMode::beat;
    edit.beatDivision = 2;
    slices = chop::makeSlices(*sample, edit);
    require(slices.count == 8, "beat 1/8 gives 8 chops on a one-bar loop");
    edit.beatDivision = 4;
    require(chop::makeSlices(*sample, edit).count == 16, "beat 1/16 gives 16 chops");

    edit.mode = chop::SliceMode::random;
    edit.density = 8;
    const auto a = chop::makeSlices(*sample, edit);
    edit.chopSeed += 99;
    const auto b = chop::makeSlices(*sample, edit);
    require(a.count >= 6 && b.count >= 6, "random chop finds enough cuts");
    require(a.edge != b.edge, "re-chop moves the cuts");

    edit.mode = chop::SliceMode::grid;
    edit.density = 8;
    edit.snapZero = true;
    slices = chop::makeSlices(*sample, edit);
    for (int i = 1; i < slices.count; ++i)
    {
        const auto at = slices.edge[i];
        const auto before = sample->pcm[0][static_cast<std::size_t>(at - 1)], after = sample->pcm[0][static_cast<std::size_t>(at)];
        require((before <= 0.0f && after > 0.0f) || (before >= 0.0f && after < 0.0f) || std::abs(after) < 1.0e-4f,
                "zero-crossing snap at cut " + std::to_string(i));
    }

    auto manual = chop::equalSlices(10003, 8);
    require(chop::insertBoundary(manual, 300, 90) && manual.count == 9, "insert marker");
    require(!chop::insertBoundary(manual, 325, 90), "reject overlapping markers");
    require(chop::moveBoundary(manual, 1, 9999, 90) && manual.edge[1] == manual.edge[2] - 90, "clamp moved marker");
    require(chop::removeBoundary(manual, 1) && chop::validSlices(manual, 10003), "remove marker");
}

void randomizer()
{
    std::uint32_t seed = 1234;
    const auto original = chop::makePreset(1);

    // Swapping: chop A and chop B trade places everywhere, nothing else moves.
    {
        auto swapped = original;
        std::vector<chop::Swap> swaps;
        require(chop::randomizePattern(swapped, 8, chop::randomChops, 20, seed, &swaps) && !swaps.empty(), "swap roll reports its swaps");
        std::array<int, chop::maxSlices> map {};
        for (int i = 0; i < chop::maxSlices; ++i) map[i] = i;
        for (auto [a, b] : swaps) std::swap(map[a], map[b]);
        for (int s = 0; s < chop::stepCount; ++s)
            require(original[s].slice < 0 ? swapped[s].slice < 0 : swapped[s].slice == map[original[s].slice],
                    "swap moves chops only by trading places");
        auto ordered = chop::inOrder(16);
        bool full = false;
        chop::randomizePattern(ordered, 16, chop::randomChops, 100, seed, nullptr, &full);
        std::array<int, 16> seen {};
        for (const auto& step : ordered) if (step.slice >= 0) ++seen[step.slice];
        require(full && std::all_of(seen.begin(), seen.end(), [](int n) { return n == 1; }),
                "100% chaos shuffles every chop exactly once");
    }

    auto chopsOnly = original;
    require(chop::randomizePattern(chopsOnly, 8, chop::randomChops, 100, seed), "chops-only roll changes the bank");
    for (int s = 0; s < chop::stepCount; ++s)
        require((chopsOnly[s].slice >= 0) == (original[s].slice >= 0) && chopsOnly[s].gate == original[s].gate,
                "chops-only keeps rhythm and gates");

    auto rhythm = original;
    require(chop::randomizePattern(rhythm, 8, chop::randomRhythm, 100, seed), "rhythm roll changes the bank");
    bool moved = false;
    for (int s = 0; s < chop::stepCount; ++s)
        moved = moved || (rhythm[s].slice >= 0) != (original[s].slice >= 0);
    require(moved, "rhythm roll moves hits");

    int ratchets = 0, reversed = 0, pitched = 0;
    for (int roll = 0; roll < 50; ++roll)
    {
        auto p = original;
        chop::randomizePattern(p, 8, chop::randomAll, 100, seed);
        for (auto step : p)
        {
            require(chop::sanitiseStep(step), "random steps stay in range");
            ratchets += step.ratchet > 1;
            reversed += step.reverse;
            pitched += step.pitch != 0;
        }
    }
    require(ratchets > 0 && reversed > 0 && pitched > 0, "stutter, reverse and pitch all appear");

    chop::Pattern empty {};
    require(chop::randomizePattern(empty, 8, chop::randomChops, 50, seed) && chop::hitCount(empty) >= 5,
            "an empty bank gets a new phrase");

    const auto ordered = chop::inOrder(8);
    for (int i = 0; i < 8; ++i)
        require(ordered[static_cast<std::size_t>(i * 2)].slice == i, "in order lays chops on eighths");
}

void internalClock()
{
    const auto session = makeSession(chop::SliceMode::grid, 8);
    chop::PlaySettings settings;
    settings.preview = true;
    // No host transport at all: the internal clock must still play the pattern.
    const auto out = render(session, settings, static_cast<int>(rate), 512, false);
    require(firstSound(out.left, 0) >= 0 && firstSound(out.left, 0) < 64, "preview sounds immediately without the host");
    require(out.steps[100] == 0 && out.steps[6100] == 1 && out.steps[12100] == 2, "1/16 steps at 120 BPM are 6000 frames");

    settings.stepsPerQuarter = 2;
    const auto eighths = render(session, settings, static_cast<int>(rate), 512, false);
    require(eighths.steps[11900] == 0 && eighths.steps[12100] == 1, "1/8 steps are 12000 frames");

    settings.preview = false;
    const auto silent = render(session, settings, 8192, 512, false);
    require(firstSound(silent.left, 0) < 0, "stopped engine is silent");
}

void hostSync()
{
    const auto session = makeSession(chop::SliceMode::grid, 8);
    chop::PlaySettings settings;
    settings.followHost = true;
    const auto a = render(session, settings, 48000, 64, true);
    const auto b = render(session, settings, 48000, 1024, true);
    const auto c = render(session, settings, 48000, 257, true);
    float worst = 0.0f;
    for (std::size_t i = 0; i < a.left.size(); ++i)
        worst = std::max({ worst, std::abs(a.left[i] - b.left[i]), std::abs(a.left[i] - c.left[i]) });
    require(worst < 1.0e-5f, "host-synced output is independent of block size");
    require(firstSound(a.left, 0) >= 0, "host sync plays");
    const auto notPlaying = render(session, settings, 8192, 512, false);
    require(firstSound(notPlaying.left, 0) < 0, "host sync waits for the transport");
}

void stepFeatures()
{
    auto session = makeSession(chop::SliceMode::grid, 8);
    auto& bank = session.edit.patterns[0];
    bank = {};
    bank[0] = { 1, 100, 2, 0, false };   // ratchet x2 on step 1
    chop::PlaySettings settings;
    settings.preview = true;
    auto out = render(session, settings, 6000, 256, false);
    // Second trigger must start at 3000 frames: there should be energy right after it.
    float before = 0.0f, after = 0.0f;
    for (int i = 2950; i < 2990; ++i) before += std::abs(out.left[static_cast<std::size_t>(i)]);
    for (int i = 3010; i < 3050; ++i) after += std::abs(out.left[static_cast<std::size_t>(i)]);
    require(after > before * 4.0f, "ratchet retriggers halfway through the step");

    // Reverse: the start of a reversed chop plays the end of the chop.
    bank[0] = { 0, 100, 1, 0, true };
    chop::Engine engine;
    engine.prepare(rate);
    chop::Transport none;
    engine.beginBlock(session, none, {}, 512);
    engine.trigger(0, 1.0f, chop::VoiceOrigin::preview, 0, 0, true);
    float l = 0.0f, r = 0.0f;
    for (int i = 0; i < 200; ++i) engine.render(i, l, r);
    const auto end = session.slices.edge[1];
    const auto expected = session.sample->pcm[0][static_cast<std::size_t>(end - 200)];
    require(std::abs(l - expected) < 0.02f, "reverse reads backwards from the chop end");
    engine.endBlock();

    // Pitch +12 plays the chop in half the time.
    chop::Engine pitched;
    pitched.prepare(rate);
    pitched.beginBlock(session, none, {}, 48000);
    pitched.trigger(0, 1.0f, chop::VoiceOrigin::preview, 0, 12, false);
    int last = -1;
    for (int i = 0; i < 48000; ++i)
    {
        pitched.render(i, l, r);
        if (std::abs(l) > 1.0e-6f) last = i;
    }
    const auto length = session.slices.edge[1] - session.slices.edge[0];
    require(last > 0 && std::abs(last - length / 2) < 200, "octave up halves the chop length");
    pitched.endBlock();
}
}


// ---- v0.3.0 ------------------------------------------------------------------

// Eight 0.25 s chops; chop k is silent for late[k] ms after its cut, then hits hard.
constexpr std::array<double, 8> lateMs { 0.0, 3.0, 1.0, 6.0, 2.0, 8.0, 4.0, 5.0 };

std::shared_ptr<chop::Sample> makeLateSample()
{
    auto sample = std::make_shared<chop::Sample>();
    sample->sampleRate = rate;
    sample->channels = 1;
    const auto chopFrames = static_cast<int>(rate * 0.25);
    sample->pcm[0].assign(static_cast<std::size_t>(chopFrames * 8), 0.0f);
    for (int k = 0; k < 8; ++k)
    {
        const auto late = static_cast<int>(lateMs[static_cast<std::size_t>(k)] * rate / 1000.0);
        for (int i = late; i < chopFrames; ++i)
        {
            const auto age = (i - late) / rate;
            sample->pcm[0][static_cast<std::size_t>(k * chopFrames + i)]
                = static_cast<float>(0.8 * std::exp(-12.0 * age) * std::cos(2.0 * 3.14159265 * 150.0 * age));
        }
    }
    sample->strongest = 1.0f;
    sample->estimatedBpm = chop::estimateLoopBpm(sample->length(), rate);
    return sample;
}

chop::Session sessionFor(std::shared_ptr<chop::Sample> sample)
{
    chop::Session session;
    session.sample = std::move(sample);
    session.edit = chop::defaultEdit();
    session.edit.mode = chop::SliceMode::grid;
    session.edit.density = 8;
    session.edit.snapZero = false;
    session.slices = chop::makeSlices(*session.sample, session.edit);
    session.sampleRevision = 1;
    return session;
}

void hitAlign()
{
    auto session = sessionFor(makeLateSample());
    for (int k = 0; k < 8; ++k)
    {
        const auto expected = static_cast<int>(lateMs[static_cast<std::size_t>(k)] * rate / 1000.0);
        require(std::abs(session.slices.hit[static_cast<std::size_t>(k)] - expected) <= 24,
                "hit measured within 0.5 ms for chop " + std::to_string(k + 1));
    }

    // Shuffle the chops so late and early ones sit next to each other.
    chop::Pattern pattern {};
    constexpr std::array<int, 8> order { 0, 5, 2, 3, 1, 7, 4, 6 };
    for (int k = 0; k < 8; ++k)
        pattern[static_cast<std::size_t>(2 * k)] = { order[static_cast<std::size_t>(k)], 100, 1, 0, false };
    session.edit.patterns[0] = pattern;

    chop::PlaySettings settings;
    settings.followHost = true;
    settings.stepsPerQuarter = 4;
    settings.choke = chop::Choke::cut;
    const auto stepFrames = static_cast<int>(rate * 0.125);
    for (const auto align : { 1.0f, 0.0f })
    {
        settings.align = align;
        const auto out = render(session, settings, stepFrames * 16 + 512, 256, true);
        double worst = 0.0;
        for (int k = 1; k < 8; ++k)
        {
            const auto grid = 2 * k * stepFrames;
            const auto onset = firstSound(out.left, grid - static_cast<int>(rate * 0.012), 0.05f);
            worst = std::max(worst, std::abs(static_cast<double>(onset - grid)));
        }
        if (align > 0.0f)
            require(worst <= 0.0006 * rate, "Hit Align lands every hit within 0.6 ms of the grid");
        else
            require(worst >= 0.007 * rate, "without Hit Align the late chops drag");
    }

    // Live hits can't start early, so they start at the attack.
    chop::Engine engine;
    engine.prepare(rate);
    chop::Transport transport;
    settings.followHost = false;
    settings.align = 1.0f;
    engine.beginBlock(session, transport, settings, 4096);
    engine.trigger(5, 1.0f, chop::VoiceOrigin::midi, 0, 0, false, 60, true);
    std::vector<float> live(4096);
    for (int i = 0; i < 4096; ++i)
    {
        float r = 0.0f;
        engine.render(i, live[static_cast<std::size_t>(i)], r);
    }
    engine.endBlock();
    require(firstSound(live, 0, 0.05f) <= static_cast<int>(0.0006 * rate), "an aligned MIDI hit sounds at once");
}

void chokeModes()
{
    auto session = makeSession(chop::SliceMode::grid, 8);
    chop::Pattern pattern {};
    pattern[0] = { 0, 40, 1, 0, false };
    session.edit.patterns[0] = pattern;
    chop::PlaySettings settings;
    settings.preview = true;
    settings.stepsPerQuarter = 2;     // a step is 0.25 s, the whole chop
    const auto gateEnd = static_cast<int>(rate * 0.25 * 0.4);
    auto tailEnergy = [&](chop::Choke choke)
    {
        settings.choke = choke;
        const auto out = render(session, settings, static_cast<int>(rate * 0.24), 128, false);
        double energy = 0.0;
        for (int i = gateEnd + 200; i < static_cast<int>(out.left.size()); ++i)
            energy += out.left[static_cast<std::size_t>(i)] * out.left[static_cast<std::size_t>(i)];
        return energy;
    };
    require(tailEnergy(chop::Choke::gate) < 1.0e-9, "GATE stops a hit at its step gate");
    pattern[0].gate = 100;
    session.edit.patterns[0] = pattern;
    require(tailEnergy(chop::Choke::cut) > 1.0e-3, "CUT lets a full-gate hit ring until something cuts it");

    // POLY: a second hit doesn't cut the first.
    auto twoHits = [&](chop::Choke choke)
    {
        chop::Engine engine;
        engine.prepare(rate);
        settings.preview = false;
        settings.choke = choke;
        engine.beginBlock(session, chop::Transport {}, settings, 4000);
        std::vector<float> out(4000);
        for (int i = 0; i < 4000; ++i)
        {
            if (i == 0) engine.trigger(0, 1.0f, chop::VoiceOrigin::midi, 0, 0, false, 60);
            if (i == 500) engine.trigger(3, 1.0f, chop::VoiceOrigin::midi, 0, 0, false, 63);
            if (i == 2000) engine.noteOff(chop::VoiceOrigin::midi, 63);
            float r = 0.0f;
            engine.render(i, out[static_cast<std::size_t>(i)], r);
        }
        engine.endBlock();
        return out;
    };
    const auto cut = twoHits(chop::Choke::cut), poly = twoHits(chop::Choke::poly), gate = twoHits(chop::Choke::gate);
    double difference = 0.0;
    for (int i = 1500; i < 1900; ++i)
        difference += std::abs(cut[static_cast<std::size_t>(i)] - poly[static_cast<std::size_t>(i)]);
    require(difference > 1.0, "POLY keeps the first hit ringing under the second");
    require(std::abs(gate[3000]) < 1.0e-6f && std::abs(cut[3000]) > 1.0e-4f, "note-off ends a hit in GATE mode only");
}

void vintage()
{
    auto session = makeSession(chop::SliceMode::grid, 8);
    chop::PlaySettings settings;
    settings.preview = true;
    const auto clean = render(session, settings, 24000, 256, false);
    for (int mode = 1; mode < chop::vintageCount; ++mode)
    {
        settings.vintage = static_cast<chop::Vintage>(mode);
        settings.drive = 1.0f;
        const auto coloured = render(session, settings, 24000, 256, false);
        auto finite = true;
        auto largest = 0.0f;
        double difference = 0.0;
        for (std::size_t i = 0; i < coloured.left.size(); ++i)
        {
            finite = finite && std::isfinite(coloured.left[i]);
            largest = std::max(largest, std::abs(coloured.left[i]));
            difference += std::abs(coloured.left[i] - clean.left[i]);
        }
        require(finite && largest < 1.5f, "vintage mode " + std::to_string(mode) + " stays finite and bounded");
        require(difference > 1.0, "vintage mode " + std::to_string(mode) + " changes the sound");
    }

    // The 12-bit converter swallows anything under its last bit.
    auto quiet = makeSample(1);
    for (auto& v : quiet->pcm[0]) v *= 1.0e-4f;
    auto quietSession = makeSession(chop::SliceMode::grid, 8);
    quietSession.sample = quiet;
    settings.vintage = chop::Vintage::sixty;
    settings.drive = 0.0f;
    const auto out = render(quietSession, settings, 12000, 256, false);
    require(std::all_of(out.left.begin() + 2000, out.left.end(), [](float v) { return std::abs(v) < 1.0e-6f; }),
            "12-bit mode truncates sub-LSB signal");
}

std::shared_ptr<chop::Sample> makeToneSample()
{
    // Eight chops, each a steady tone. Chop 6 is the same tone as chop 1.
    constexpr std::array<double, 8> hz { 220.0, 330.0, 247.0, 392.0, 440.0, 220.0, 523.0, 294.0 };
    auto sample = std::make_shared<chop::Sample>();
    sample->sampleRate = rate;
    sample->channels = 1;
    const auto chopFrames = static_cast<int>(rate * 0.25);
    sample->pcm[0].resize(static_cast<std::size_t>(chopFrames * 8));
    for (int k = 0; k < 8; ++k)
        for (int i = 0; i < chopFrames; ++i)
            sample->pcm[0][static_cast<std::size_t>(k * chopFrames + i)]
                = static_cast<float>(0.5 * std::sin(2.0 * 3.14159265 * hz[static_cast<std::size_t>(k)] * i / rate));
    sample->strongest = 1.0f;
    sample->estimatedBpm = chop::estimateLoopBpm(sample->length(), rate);
    return sample;
}

double chainCost(const chop::FitTable& table, const chop::Pattern& p, int anchor)
{
    std::vector<int> hits;
    for (int i = 0; i < chop::stepCount; ++i)
        if (p[static_cast<std::size_t>(i)].slice >= 0) hits.push_back(i);
    double total = 0.0;
    for (std::size_t k = 0; k < hits.size(); ++k)
        total += chop::joinCost(table, p[static_cast<std::size_t>(hits[k])], p[static_cast<std::size_t>(hits[(k + 1) % hits.size()])],
                                &p[static_cast<std::size_t>(anchor)]);
    return total;
}

void dovetail()
{
    const auto session = sessionFor(makeToneSample());
    const auto table = chop::buildFitTable(*session.sample, session.slices);
    require(table->count == 8, "fit table covers every chop");

    chop::Pattern base {};
    base[0] = { 0, 100, 1, 0, false };
    base[4] = { 3, 100, 1, 0, false };
    base[8] = { 6, 100, 1, 0, false };
    base[12] = { 2, 100, 1, 0, false };

    chop::FitContext fit;
    fit.table = table.get();
    fit.anchor = 0;
    fit.amount = 100;
    int twin = 0;
    double fitted = 0.0, shuffled = 0.0;
    for (std::uint32_t s = 1; s <= 200; ++s)
    {
        auto p = base;
        auto seed = s * 2654435761u;
        std::vector<int> chain;
        chop::randomizePattern(p, 8, chop::randomChops, 10, seed, nullptr, nullptr, &fit, &chain);
        require(p[0] == base[0], "Dovetail never changes the pinned step");
        require(chain.size() == 4 && chain[0] == 0, "Dovetail reports the chain from the pinned chop");
        auto again = base;
        auto seed2 = s * 2654435761u;
        chop::randomizePattern(again, 8, chop::randomChops, 10, seed2, nullptr, nullptr, &fit);
        require(again == p, "Dovetail is repeatable for a seed");
        if (p[4].slice == 5) ++twin;
        fitted += chainCost(*table, p, 0);

        auto plain = base;
        auto seed3 = s * 2654435761u;
        chop::randomizePattern(plain, 8, chop::randomChops, 100, seed3);
        shuffled += chainCost(*table, plain, 0);
    }
    require(twin >= 120, "the chop identical to the pinned one is placed after it most of the time");
    require(fitted < shuffled * 0.7, "Dovetail chains fit far better than a shuffle");

    // Fit 0, or no pinned hit, is plain RANDOMIZE.
    for (const auto amount : { 0, 100 })
    {
        auto withFit = base, without = base;
        auto seedA = 77u, seedB = 77u;
        chop::FitContext off = fit;
        off.amount = amount;
        if (amount > 0)
            off.anchor = 1;   // a rest: nothing pinned
        chop::randomizePattern(withFit, 8, chop::randomAll, 60, seedA, nullptr, nullptr, &off);
        chop::randomizePattern(without, 8, chop::randomAll, 60, seedB);
        require(withFit == without, "Dovetail is off without a pinned hit or with Fit at 0");
    }

    // Rhythm and step rolls never touch the pinned step either.
    for (std::uint32_t s = 1; s <= 50; ++s)
    {
        auto p = base;
        auto seed = s;
        fit.anchor = 8;
        chop::randomizePattern(p, 8, chop::randomAll, 100, seed, nullptr, nullptr, &fit);
        require(p[8] == base[8], "a full roll leaves the pinned step alone");
    }
}

void randomizerFixes()
{
    // Swaps are disjoint pairs, so the readout is exactly what happened.
    for (std::uint32_t s = 1; s <= 200; ++s)
    {
        auto p = chop::inOrder(16);
        auto seed = s;
        std::vector<chop::Swap> swaps;
        chop::randomizePattern(p, 16, chop::randomChops, 80, seed, &swaps);
        std::array<int, chop::maxSlices> seen {};
        for (auto [a, b] : swaps) { ++seen[static_cast<std::size_t>(a)]; ++seen[static_cast<std::size_t>(b)]; }
        require(std::all_of(seen.begin(), seen.end(), [](int n) { return n <= 1; }), "no chop appears in two swaps");
    }

    // Low Chaos keeps most hand-set ratchets.
    int kept = 0, total = 0;
    for (std::uint32_t s = 1; s <= 100; ++s)
    {
        auto p = chop::inOrder(8);
        for (auto& step : p) if (step.slice >= 0) step.ratchet = 3;
        auto seed = s;
        chop::randomizePattern(p, 8, chop::randomStutter, 10, seed);
        for (const auto& step : p) if (step.slice >= 0) { ++total; kept += step.ratchet == 3 ? 1 : 0; }
    }
    require(kept >= total * 8 / 10, "Chaos 10 keeps at least 80% of hand-set ratchets");
}

int main()
{
    slicingModes();
    randomizer();
    internalClock();
    hostSync();
    stepFeatures();
    hitAlign();
    chokeModes();
    vintage();
    dovetail();
    randomizerFixes();
    if (failures == 0)
        std::cout << "Engine tests passed\n";
    return failures == 0 ? 0 : 1;
}
