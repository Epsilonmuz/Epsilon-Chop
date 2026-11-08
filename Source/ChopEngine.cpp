#include "ChopEngine.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

namespace chop
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr int taps = 32, phases = 64, cutoffCount = 81;
constexpr double tickEpsilon = 1.0e-9;
constexpr std::int64_t noEvent = std::numeric_limits<std::int64_t>::min();
constexpr std::int64_t reanchor = noEvent + 1;

struct ResamplingTable
{
    using Kernel = std::array<float, taps>;
    std::array<std::array<Kernel, phases + 1>, cutoffCount> bands {};

    ResamplingTable()
    {
        for (int band = 0; band < cutoffCount; ++band)
        {
            const auto cutoff = std::exp2(-static_cast<double>(band) / 16.0);
            for (int phase = 0; phase <= phases; ++phase)
            {
                auto& kernel = bands[band][phase];
                double total = 0.0;
                for (int tap = 0; tap < taps; ++tap)
                {
                    const auto distance = tap - (taps / 2 - 1) - static_cast<double>(phase) / phases;
                    const auto x = distance / (taps / 2);
                    const auto window = std::abs(x) < 1.0
                        ? 0.42 + 0.5 * std::cos(pi * x) + 0.08 * std::cos(2.0 * pi * x) : 0.0;
                    const auto angle = pi * cutoff * distance;
                    const auto sinc = std::abs(angle) < 1.0e-12 ? 1.0 : std::sin(angle) / angle;
                    kernel[tap] = static_cast<float>(cutoff * sinc * window);
                    total += kernel[tap];
                }
                for (auto& coefficient : kernel)
                    coefficient /= static_cast<float>(total);
            }
        }
    }
};

const ResamplingTable& resamplingTable()
{
    static const ResamplingTable table;
    return table;
}

std::uint32_t next(std::uint32_t& seed) noexcept
{
    seed = seed * 1664525u + 1013904223u;
    return seed >> 8u;
}

int draw(std::uint32_t& seed, int upper) noexcept
{
    return upper <= 1 ? 0 : static_cast<int>(next(seed) % static_cast<unsigned>(upper));
}

float chance(std::uint32_t& seed) noexcept
{
    return static_cast<float>(next(seed) & 0xffffu) / 65536.0f;
}

float smooth(float value) noexcept
{
    return value * value * (3.0f - 2.0f * value);
}

float mono(const Sample& sample, int index) noexcept
{
    const auto left = sample.pcm[0][static_cast<std::size_t>(index)];
    return sample.channels == 2 ? 0.5f * (left + sample.pcm[1][static_cast<std::size_t>(index)]) : left;
}

int snapToZero(const Sample& sample, int position) noexcept
{
    const auto frames = sample.length();
    const auto radius = std::max(1, static_cast<int>(sample.sampleRate * 0.002));
    for (int distance = 0; distance <= radius; ++distance)
        for (const auto candidate : { position - distance, position + distance })
        {
            if (candidate <= 1 || candidate >= frames - 1)
                continue;
            const auto before = mono(sample, candidate - 1);
            const auto after = mono(sample, candidate);
            if ((before <= 0.0f && after > 0.0f) || (before >= 0.0f && after < 0.0f))
                return candidate;
        }
    return position;
}

// Builds a valid slice list from sorted interior boundary candidates.
Slices fromBoundaries(std::vector<int> marks, int frames, int minimumSpacing) noexcept
{
    std::sort(marks.begin(), marks.end());
    Slices result;
    result.edge[0] = 0;
    result.count = 0;
    int previous = 0;
    for (const auto mark : marks)
    {
        if (result.count >= maxSlices - 1)
            break;
        if (mark - previous < minimumSpacing || frames - mark < minimumSpacing)
            continue;
        result.edge[++result.count] = mark;
        previous = mark;
    }
    result.edge[++result.count] = frames;
    return result;
}
}

bool validSampleSize(double rate, int channels, std::int64_t frames) noexcept
{
    return std::isfinite(rate) && rate >= 8000.0 && rate <= 192000.0
           && channels >= 1 && channels <= 2 && frames > 0
           && frames <= maxSampleFrames && static_cast<double>(frames) <= rate * maxSampleSeconds;
}

Slices equalSlices(int frames, int density) noexcept
{
    Slices result;
    if (frames <= 0)
        return result;

    result.count = std::min(frames, std::clamp(density, 1, maxSlices));
    for (int index = 0; index <= result.count; ++index)
        result.edge[index] = static_cast<int>((static_cast<std::int64_t>(frames) * index) / result.count);
    return result;
}

bool validSlices(const Slices& slices, int frames) noexcept
{
    if (slices.count < 1 || slices.count > maxSlices || slices.edge[0] != 0
        || slices.edge[slices.count] != frames)
        return false;
    for (int index = 1; index <= slices.count; ++index)
        if (slices.edge[index] <= slices.edge[index - 1])
            return false;
    return true;
}

double estimateLoopBpm(int frames, double sampleRate) noexcept
{
    if (frames <= 0 || sampleRate <= 0.0)
        return 120.0;
    // Assume a 4/4 loop of 1, 2, 4 ... 64 beats and pick the length that lands
    // in the 80-160 BPM window (the window most loops are made in).
    const auto seconds = frames / sampleRate;
    double best = 120.0, bestDistance = 1.0e9;
    for (int beats = 1; beats <= 256; beats *= 2)
    {
        const auto bpm = 60.0 * beats / seconds;
        const auto distance = bpm < 80.0 ? 80.0 - bpm : (bpm >= 160.0 ? bpm - 160.0 + 1.0e-3 : 0.0);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = bpm;
        }
    }
    return std::clamp(best, 40.0, 300.0);
}

double sampleBpm(const Sample& sample, const Edit& edit) noexcept
{
    return edit.sampleBpmTenths > 0 ? edit.sampleBpmTenths / 10.0 : sample.estimatedBpm;
}

Slices makeSlices(const Sample& sample, const Edit& edit) noexcept
{
    const auto frames = sample.length();
    if (frames <= 0)
        return {};
    if (edit.mode == SliceMode::manual)
        return validSlices(edit.manual, frames) ? edit.manual : equalSlices(frames, edit.density);

    const auto minimumSpacing = std::max(1, std::min(static_cast<int>(sample.sampleRate * 0.025), frames / 64));
    std::vector<int> marks;
    marks.reserve(maxOnsets);

    switch (edit.mode)
    {
        case SliceMode::transient:
        {
            if (sample.onsets.empty() || sample.strongest <= 0.0f)
                break;
            const auto open = 1.0f - std::clamp(edit.sensitivity, 0, 100) / 100.0f;
            const auto threshold = sample.strongest * (0.6f * open * open + 0.004f);
            std::vector<Onset> chosen;
            for (const auto& onset : sample.onsets)
                if (onset.strength >= threshold)
                    chosen.push_back(onset);
            const auto limit = static_cast<std::size_t>(std::clamp(edit.density, 2, maxSlices) - 1);
            if (chosen.size() > limit)
            {
                std::stable_sort(chosen.begin(), chosen.end(), [](auto& a, auto& b) { return a.strength > b.strength; });
                chosen.resize(limit);
            }
            for (const auto& onset : chosen)
                marks.push_back(onset.position);
            break;
        }
        case SliceMode::beat:
        {
            const auto bpm = std::clamp(sampleBpm(sample, edit), 30.0, 400.0);
            auto division = sample.sampleRate * 60.0 / bpm / std::clamp(edit.beatDivision, 1, 8);
            while (frames / division > maxSlices + 0.25)
                division *= 2.0;
            const auto window = static_cast<int>(std::min(division * 0.2, sample.sampleRate * 0.04));
            for (int index = 1; index * division < frames - division * 0.25; ++index)
            {
                auto position = static_cast<int>(std::lround(index * division));
                // Pull the cut onto a nearby hit so beat chops start on the attack.
                int best = -1;
                for (const auto& onset : sample.onsets)
                    if (std::abs(onset.position - position) <= window && onset.strength >= sample.strongest * 0.08f
                        && (best < 0 || std::abs(onset.position - position) < std::abs(best - position)))
                        best = onset.position;
                marks.push_back(best >= 0 ? best : position);
            }
            break;
        }
        case SliceMode::random:
        {
            auto seed = edit.chopSeed;
            const auto count = std::clamp(edit.density, 2, maxSlices);
            const auto spacing = std::max(minimumSpacing, frames / (count * 4));
            for (int attempt = 0; attempt < count * 40 && static_cast<int>(marks.size()) < count - 1; ++attempt)
            {
                const auto position = spacing + static_cast<int>(next(seed) % static_cast<unsigned>(std::max(1, frames - 2 * spacing)));
                if (std::none_of(marks.begin(), marks.end(), [&](int other) { return std::abs(other - position) < spacing; }))
                    marks.push_back(position);
            }
            break;
        }
        case SliceMode::grid:
        case SliceMode::manual:
            break;
    }

    if (marks.empty() && edit.mode != SliceMode::random && edit.mode != SliceMode::beat)
    {
        const auto grid = equalSlices(frames, edit.mode == SliceMode::transient ? std::min(8, edit.density) : edit.density);
        marks.assign(grid.edge.begin() + 1, grid.edge.begin() + grid.count);
    }
    if (edit.snapZero)
        for (auto& mark : marks)
            mark = snapToZero(sample, mark);
    auto result = fromBoundaries(std::move(marks), frames, edit.mode == SliceMode::grid ? 1 : minimumSpacing);
    return validSlices(result, frames) ? result : equalSlices(frames, edit.density);
}

bool insertBoundary(Slices& slices, int position, int minimumSpacing) noexcept
{
    if (slices.count < 1 || slices.count >= maxSlices
        || position <= 0 || position >= slices.edge[slices.count])
        return false;
    auto at = std::lower_bound(slices.edge.begin() + 1, slices.edge.begin() + slices.count + 1, position);
    if (position - *(at - 1) < minimumSpacing || *at - position < minimumSpacing)
        return false;
    std::move_backward(at, slices.edge.begin() + slices.count + 1, slices.edge.begin() + slices.count + 2);
    *at = position;
    ++slices.count;
    return true;
}

bool moveBoundary(Slices& slices, int index, int position, int minimumSpacing) noexcept
{
    if (index <= 0 || index >= slices.count)
        return false;
    const auto low = slices.edge[index - 1] + minimumSpacing;
    const auto high = slices.edge[index + 1] - minimumSpacing;
    if (low > high)
        return false;
    const auto moved = std::clamp(position, low, high);
    if (moved == slices.edge[index])
        return false;
    slices.edge[index] = moved;
    return true;
}

bool removeBoundary(Slices& slices, int index) noexcept
{
    if (index <= 0 || index >= slices.count)
        return false;
    std::move(slices.edge.begin() + index + 1, slices.edge.begin() + slices.count + 1,
              slices.edge.begin() + index);
    --slices.count;
    return true;
}

const char* presetName(int index) noexcept
{
    constexpr std::array<const char*, presetCount> names { "Straight Eighths", "Jersey Bounce", "Drill Space", "Dembow Pocket" };
    return index >= 0 && index < presetCount ? names[static_cast<std::size_t>(index)] : "";
}

Pattern makePreset(int index) noexcept
{
    Pattern pattern {};
    auto hit = [&pattern](int step, int slice, int gate, int ratchet = 1)
    {
        pattern[step] = { slice, gate, ratchet, 0, false };
    };
    switch (index)
    {
        case 1:
            hit(0, 0, 85); hit(3, 1, 55); hit(6, 2, 70); hit(8, 3, 90);
            hit(10, 2, 45); hit(11, 2, 35); hit(14, 5, 85, 2);
            break;
        case 2:
            hit(0, 0, 100); hit(4, 2, 85); hit(7, 3, 40); hit(9, 1, 100);
            hit(12, 4, 75); hit(15, 6, 40, 3);
            break;
        case 3:
            hit(0, 0, 90); hit(3, 1, 70); hit(6, 2, 70); hit(8, 0, 90);
            hit(11, 3, 70); hit(14, 4, 55);
            break;
        default:
            for (int step = 0; step < stepCount; step += 2)
                hit(step, step / 2, 100);
            break;
    }
    return pattern;
}

Pattern inOrder(int slices) noexcept
{
    Pattern pattern {};
    const auto count = std::clamp(slices, 1, stepCount);
    for (int index = 0; index < count; ++index)
        pattern[static_cast<std::size_t>(index * stepCount / count)] = { index, 100, 1, 0, false };
    return pattern;
}

Edit defaultEdit() noexcept
{
    Edit edit;
    edit.patterns[0] = inOrder(16);
    for (int bank = 1; bank < bankCount; ++bank)
        edit.patterns[bank] = makePreset(bank);
    return edit;
}

bool sanitiseStep(Step& step) noexcept
{
    const auto valid = step.slice >= -1 && step.slice < maxSlices && step.gate >= 10 && step.gate <= 100
                       && step.ratchet >= 1 && step.ratchet <= maxRatchet
                       && step.pitch >= -maxPitch && step.pitch <= maxPitch;
    step.slice = std::clamp(step.slice, -1, maxSlices - 1);
    step.gate = std::clamp(step.gate, 10, 100);
    step.ratchet = std::clamp(step.ratchet, 1, maxRatchet);
    step.pitch = std::clamp(step.pitch, -maxPitch, maxPitch);
    return valid;
}

int hitCount(const Pattern& pattern) noexcept
{
    return static_cast<int>(std::count_if(pattern.begin(), pattern.end(), [](const auto& s) { return s.slice >= 0; }));
}

bool randomizePattern(Pattern& pattern, int slices, int flags, int chaos, std::uint32_t& seed,
                      std::vector<Swap>* swaps, bool* fullShuffle) noexcept
{
    if (swaps != nullptr) swaps->clear();
    if (fullShuffle != nullptr) *fullShuffle = false;
    if (slices < 1 || (flags & randomAll) == 0)
        return false;

    const auto amount = std::clamp(chaos, 5, 100) / 100.0f;
    const auto original = pattern;
    auto result = pattern;
    // Downbeats are likelier landing spots than offbeats, so random rhythms still groove.
    constexpr std::array<float, stepCount> weight { 1.0f, .30f, .55f, .40f, .85f, .30f, .55f, .45f,
                                                    .95f, .30f, .55f, .45f, .80f, .35f, .60f, .45f };
    auto pickEmpty = [&](const Pattern& p) -> int
    {
        float total = 0.0f;
        for (int i = 0; i < stepCount; ++i)
            if (p[i].slice < 0) total += weight[i];
        if (total <= 0.0f)
            return -1;
        auto target = chance(seed) * total;
        for (int i = 0; i < stepCount; ++i)
            if (p[i].slice < 0 && (target -= weight[i]) <= 0.0f)
                return i;
        for (int i = stepCount - 1; i >= 0; --i)
            if (p[i].slice < 0) return i;
        return -1;
    };

    // An empty bank gets a fresh phrase so the button never does nothing.
    if (hitCount(result) == 0)
    {
        const auto hits = 5 + draw(seed, 5);
        for (int i = 0; i < hits; ++i)
            if (const auto at = pickEmpty(result); at >= 0)
                result[at] = { draw(seed, slices), 100, 1, 0, false };
    }

    if ((flags & randomRhythm) != 0)
    {
        std::vector<int> occupied;
        for (int i = 0; i < stepCount; ++i)
            if (result[i].slice >= 0) occupied.push_back(i);
        const auto moves = std::max(1, static_cast<int>(std::lround(amount * occupied.size())));
        for (int m = 0; m < moves && !occupied.empty(); ++m)
        {
            const auto which = draw(seed, static_cast<int>(occupied.size()));
            const auto from = occupied[static_cast<std::size_t>(which)];
            auto hit = result[from];
            result[from] = {};
            const auto to = pickEmpty(result);
            result[to >= 0 ? to : from] = hit;
            occupied.erase(occupied.begin() + which);
        }
        // High chaos also adds or drops a hit now and then.
        if (chance(seed) < amount * 0.6f)
        {
            if (chance(seed) < 0.5f && hitCount(result) < 13)
            {
                if (const auto at = pickEmpty(result); at >= 0)
                    result[at] = { draw(seed, slices), 100, 1, 0, false };
            }
            else if (hitCount(result) > 4)
            {
                for (int tries = 0; tries < 16; ++tries)
                    if (auto& s = result[draw(seed, stepCount)]; s.slice >= 0 && &s != &result[0])
                    {
                        s = {};
                        break;
                    }
            }
        }
    }

    // Swap chop positions. Works on the chop map, so every hit of chop A becomes B and
    // every hit of B becomes A; B doesn't have to be in the bank yet.
    std::array<int, maxSlices> map {};
    for (int i = 0; i < maxSlices; ++i)
        map[static_cast<std::size_t>(i)] = i;
    for (auto& step : result)
        if (step.slice >= 0) step.slice %= slices;
    if ((flags & randomChops) != 0 && slices >= 2)
    {
        if (amount >= 0.95f)
        {
            for (int i = slices - 1; i > 0; --i)
                std::swap(map[static_cast<std::size_t>(i)], map[static_cast<std::size_t>(draw(seed, i + 1))]);
            if (fullShuffle != nullptr) *fullShuffle = true;
        }
        else
        {
            std::vector<int> used;
            for (const auto& step : result)
                if (step.slice >= 0 && std::find(used.begin(), used.end(), step.slice) == used.end())
                    used.push_back(step.slice);
            const auto count = std::max(1, static_cast<int>(std::lround(amount * std::max<std::size_t>(2, used.size()) * 0.5)));
            for (int n = 0; n < count && !used.empty(); ++n)
            {
                const auto a = used[static_cast<std::size_t>(draw(seed, static_cast<int>(used.size())))];
                auto b = draw(seed, slices - 1);
                if (b >= a) ++b;
                std::swap(map[static_cast<std::size_t>(a)], map[static_cast<std::size_t>(b)]);
                if (swaps != nullptr) swaps->push_back({ a, b });
            }
        }
    }

    for (auto& step : result)
    {
        if (step.slice < 0)
            continue;
        step.slice = map[static_cast<std::size_t>(step.slice)];
        if ((flags & randomStutter) != 0)
        {
            constexpr std::array<int, 5> ratchets { 2, 2, 3, 4, 2 };
            step.ratchet = chance(seed) < amount * 0.32f ? ratchets[static_cast<std::size_t>(draw(seed, 5))] : 1;
        }
        if ((flags & randomReverse) != 0)
            step.reverse = chance(seed) < amount * 0.3f;
        if ((flags & randomPitch) != 0)
        {
            constexpr std::array<int, 6> intervals { -12, -7, -5, 5, 7, 12 };
            step.pitch = chance(seed) < amount * 0.3f ? intervals[static_cast<std::size_t>(draw(seed, 6))] : 0;
        }
        if ((flags & randomGate) != 0 && chance(seed) < amount)
        {
            constexpr std::array<int, 5> gates { 25, 40, 60, 80, 100 };
            step.gate = gates[static_cast<std::size_t>(draw(seed, 5))];
        }
    }

    if (result == original)
    {
        for (auto& step : result)
            if (step.slice >= 0)
            {
                if (slices == 1)
                {
                    step.reverse = !step.reverse;
                    break;
                }
                // Nothing changed: trade this chop with another one.
                const auto a = step.slice;
                auto b = draw(seed, slices - 1);
                if (b >= a) ++b;
                for (auto& other : result)
                    if (other.slice == a) other.slice = b;
                    else if (other.slice == b) other.slice = a;
                if (swaps != nullptr) swaps->push_back({ a, b });
                break;
            }
    }
    pattern = result;
    return result != original;
}

// ---- Engine ----------------------------------------------------------------

void Engine::prepare(double outputRate, float initialGain)
{
    (void) resamplingTable();
    sampleRate = std::isfinite(outputRate) && outputRate >= 8000.0 ? outputRate : 44100.0;
    microfadeFrames = std::max(1, static_cast<int>(std::lround(sampleRate * 0.002)));
    gain = gainTarget = initialGain;
    gainStep = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.008)));
    reset();
}

void Engine::reset() noexcept
{
    endBlock();
    voices = {};
    currentSample = ~0ull;
    clock = Clock::off;
    previewWasOn = false;
    playStep = playFrame = playSlice = -1;
    lastLeft = lastRight = 0.0f;
    replacementFade = 0;
    internalTick = 0.0;
}

Engine::StepPoint Engine::locate(double tick) const noexcept
{
    const auto pair = std::floor(tick / 2.0);
    const auto within = tick - 2.0 * pair;
    const auto s = static_cast<double>(swing);
    const auto first = static_cast<std::int64_t>(pair) * 2;
    if (within < 1.0 + s)
        return { first, within / (1.0 + s), 1.0 + s };
    return { first + 1, (within - 1.0 - s) / (1.0 - s), 1.0 - s };
}

void Engine::beginBlock(const Session& session, const Transport& transport,
                        const PlaySettings& settings, int samples) noexcept
{
    sample = session.sample.get();
    slices = &session.slices;
    pattern = &session.edit.patterns[static_cast<std::size_t>(session.edit.bank)];
    gainTarget = settings.gain;
    swing = std::clamp(settings.swing, 0.0f, 0.6f);

    if (session.sampleRevision != currentSample)
    {
        currentSample = session.sampleRevision;
        replacedLeft = lastLeft;
        replacedRight = lastRight;
        replacementFade = microfadeFrames;
        voices = {};
        playFrame = playSlice = -1;
        baseRatio = sample != nullptr ? sample->sampleRate / sampleRate : 1.0;
    }

    const auto hostTempo = std::isfinite(transport.bpm) && transport.bpm >= 10.0 && transport.bpm <= 999.0;
    bpm = hostTempo ? transport.bpm : std::clamp(settings.fallbackBpm, 20.0, 400.0);
    speed = settings.tempoFit && sample != nullptr
        ? std::clamp(bpm / std::max(1.0, sampleBpm(*sample, session.edit)), 0.25, 4.0) : 1.0;

    const auto hostRun = sample != nullptr && settings.followHost && transport.playing && transport.hasPosition
                         && hostTempo && std::isfinite(transport.ppq) && std::abs(transport.ppq) < 1.0e9;
    const auto previewRun = sample != nullptr && settings.preview;
    const auto spq = static_cast<double>(std::clamp(settings.stepsPerQuarter, 1, 16));
    tickIncrement = bpm * spq / (60.0 * sampleRate);

    const auto previous = clock;
    clock = hostRun ? Clock::host : (previewRun ? Clock::internal : Clock::off);
    const auto previewStarted = settings.preview && !previewWasOn;
    previewWasOn = settings.preview;

    if (clock == Clock::off)
    {
        if (previous != Clock::off)
            stop(VoiceOrigin::pattern);
        playStep = -1;
        return;
    }

    if (clock == Clock::internal)
    {
        if (previous != Clock::internal || previewStarted)
        {
            stop(VoiceOrigin::pattern);
            internalTick = 0.0;
            previousEvent = noEvent;
            tickStart = 0.0;
            expectedTick = 0.0;
        }
        tickStart = internalTick * spq;
        internalTick += samples * bpm / (60.0 * sampleRate);
        if (std::abs(tickStart - expectedTick) > 2.0 * tickIncrement + 1.0e-6 && previousEvent != noEvent)
            previousEvent = reanchor; // rate changed: re-anchor without firing a stray hit
    }
    else
    {
        tickStart = transport.ppq * spq;
        if (previous != Clock::host || std::abs(tickStart - expectedTick) > 2.0 * tickIncrement)
        {
            stop(VoiceOrigin::pattern);
            previousEvent = reanchor;
        }
    }

    if (previousEvent == reanchor)
    {
        // Anchor to the event just before this block so only a genuine boundary fires.
        const auto point = locate(tickStart - tickIncrement + tickEpsilon);
        const auto& step = (*pattern)[static_cast<std::size_t>((point.step % stepCount + stepCount) % stepCount)];
        const auto ratchet = step.slice >= 0 ? std::clamp(step.ratchet, 1, maxRatchet) : 1;
        previousEvent = point.step * 8 + std::min(ratchet - 1, static_cast<int>(point.local * ratchet));
        if (tickStart <= tickEpsilon && tickStart >= -tickEpsilon)
            previousEvent = noEvent;
    }
    expectedTick = tickStart + samples * tickIncrement;
}

void Engine::release(Voice& voice) const noexcept
{
    if (!voice.active() || voice.releaseLength != 0)
        return;
    voice.release = voice.releaseLength = std::max(1, std::min(microfadeFrames, voice.frames - voice.elapsed));
}

void Engine::stop(VoiceOrigin origin) noexcept
{
    for (auto& voice : voices)
        if (voice.origin == origin)
            release(voice);
}

void Engine::trigger(int slice, float velocity, VoiceOrigin origin, int gateFrames, int pitch, bool reverse) noexcept
{
    if (sample == nullptr || slices == nullptr || slices->count <= 0 || slice < 0 || slice >= slices->count)
        return;
    for (auto& voice : voices)
        release(voice);

    auto voice = std::find_if(voices.begin(), voices.end(), [](const auto& v) { return !v.active(); });
    if (voice == voices.end())
        voice = std::min_element(voices.begin(), voices.end(),
                                [](const auto& a, const auto& b) { return a.release < b.release; });

    const auto ratio = std::clamp(baseRatio * speed * std::exp2(std::clamp(pitch, -maxPitch, maxPitch) / 12.0),
                                  1.0 / 16.0, 16.0);
    const auto first = slices->edge[slice];
    const auto last = slices->edge[slice + 1];
    const auto natural = std::max(1, static_cast<int>(std::ceil((last - first) / ratio)));
    const auto length = gateFrames > 0 ? std::min(natural, gateFrames) : natural;
    Voice v;
    v.position = reverse ? static_cast<double>(last - 1) : static_cast<double>(first);
    v.increment = reverse ? -ratio : ratio;
    v.start = first;
    v.end = last;
    v.frames = length;
    v.fade = std::min(microfadeFrames, (length - 1) / 2);
    v.filter = std::clamp(static_cast<int>(std::ceil(16.0 * std::log2(std::max(1.0, ratio)))), 0, cutoffCount - 1);
    v.slice = slice;
    v.velocity = std::clamp(velocity, 0.0f, 1.0f);
    v.origin = origin;
    *voice = v;
    playFrame = static_cast<int>(v.position);
    playSlice = slice;
}

void Engine::advancePattern(int sampleInBlock) noexcept
{
    if (clock == Clock::off)
        return;
    const auto tick = tickStart + sampleInBlock * tickIncrement + tickEpsilon;
    const auto point = locate(tick);
    const auto index = static_cast<int>((point.step % stepCount + stepCount) % stepCount);
    const auto& step = (*pattern)[static_cast<std::size_t>(index)];
    const auto ratchet = step.slice >= 0 ? std::clamp(step.ratchet, 1, maxRatchet) : 1;
    const auto sub = std::min(ratchet - 1, static_cast<int>(point.local * ratchet));
    const auto event = point.step * 8 + sub;
    if (event == previousEvent)
        return;

    previousEvent = event;
    playStep = index;
    if (step.slice >= 0 && slices->count > 0)
    {
        const auto stepFrames = point.duration / tickIncrement;
        const auto duration = std::max(1, static_cast<int>(std::lround(0.01 * step.gate * stepFrames / ratchet)));
        const auto velocity = sub == 0 ? 1.0f : 0.82f;
        trigger(step.slice % slices->count, velocity, VoiceOrigin::pattern, duration, step.pitch, step.reverse);
    }
}

float Engine::interpolate(const float* pcm, const Voice& voice) const noexcept
{
    const auto position = voice.position;
    const auto integral = static_cast<int>(std::floor(position));
    const auto fraction = position - integral;
    if (std::abs(voice.increment) == 1.0 && fraction == 0.0)
        return pcm[std::clamp(integral, voice.start, voice.end - 1)];

    const auto phase = std::clamp(static_cast<int>(fraction * phases + 0.5), 0, phases);
    const auto& kernel = resamplingTable().bands[static_cast<std::size_t>(voice.filter)][static_cast<std::size_t>(phase)];
    const auto start = integral - (taps / 2 - 1);
    float sum = 0.0f;
    if (start >= voice.start && start + taps <= voice.end)
    {
        const auto* p = pcm + start;
        for (int tap = 0; tap < taps; ++tap)
            sum += kernel[static_cast<std::size_t>(tap)] * p[tap];
    }
    else
    {
        for (int tap = 0; tap < taps; ++tap)
            sum += kernel[static_cast<std::size_t>(tap)] * pcm[std::clamp(start + tap, voice.start, voice.end - 1)];
    }
    return sum;
}

void Engine::render(int sampleInBlock, float& left, float& right) noexcept
{
    advancePattern(sampleInBlock);
    left = right = 0.0f;
    if (sample != nullptr)
    {
        auto reported = false;
        for (auto& voice : voices)
        {
            if (!voice.active())
                continue;

            auto envelope = 1.0f;
            if (voice.fade > 0)
            {
                const auto distance = std::min(voice.elapsed, voice.frames - voice.elapsed - 1);
                envelope = smooth(std::clamp(static_cast<float>(distance) / static_cast<float>(voice.fade), 0.0f, 1.0f));
            }
            if (voice.releaseLength > 0)
            {
                envelope *= smooth(static_cast<float>(voice.release - 1) / static_cast<float>(voice.releaseLength));
                if (--voice.release <= 0)
                    voice.frames = voice.elapsed + 1;
            }

            const auto amp = voice.velocity * envelope;
            const auto l = interpolate(sample->pcm[0].data(), voice);
            const auto r = sample->channels == 2 ? interpolate(sample->pcm[1].data(), voice) : l;
            left += amp * l;
            right += amp * r;
            if (voice.releaseLength == 0 && !reported)
            {
                playFrame = std::clamp(static_cast<int>(voice.position), voice.start, voice.end - 1);
                playSlice = voice.slice;
                reported = true;
            }
            voice.position += voice.increment;
            ++voice.elapsed;
        }
        if (!reported)
            playFrame = playSlice = -1;
    }

    gain += gainStep * (gainTarget - gain);
    left *= gain;
    right *= gain;
    if (replacementFade > 0)
    {
        const auto tail = smooth(static_cast<float>(replacementFade) / static_cast<float>(microfadeFrames));
        left += replacedLeft * tail;
        right += replacedRight * tail;
        --replacementFade;
    }
    lastLeft = left;
    lastRight = right;
}
}
