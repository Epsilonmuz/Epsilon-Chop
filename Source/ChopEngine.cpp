#include "ChopEngine.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <iterator>
#include <limits>
#include <numeric>

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
    // The high bits of an LCG are the well-mixed ones.
    return static_cast<float>((next(seed) >> 8u) & 0xffffu) / 65536.0f;
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

// Moves a cut back onto the nearest zero crossing at or before it (up to 2 ms), so a
// cut placed on an attack never ends up after the attack.
int snapToZero(const Sample& sample, int position) noexcept
{
    const auto frames = sample.length();
    const auto radius = std::max(1, static_cast<int>(sample.sampleRate * 0.002));
    for (int distance = 0; distance <= radius; ++distance)
    {
        const auto candidate = position - distance;
        if (candidate <= 1 || candidate >= frames - 1)
            continue;
        const auto before = mono(sample, candidate - 1);
        const auto after = mono(sample, candidate);
        if ((before <= 0.0f && after > 0.0f) || (before >= 0.0f && after < 0.0f))
            return candidate;
    }
    return position;
}

float magnitude(const Sample& sample, int index) noexcept
{
    const auto left = std::abs(sample.pcm[0][static_cast<std::size_t>(index)]);
    return sample.channels == 2 ? std::max(left, std::abs(sample.pcm[1][static_cast<std::size_t>(index)])) : left;
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
    {
        auto result = validSlices(edit.manual, frames) ? edit.manual : equalSlices(frames, edit.density);
        measureHits(sample, result);
        return result;
    }

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
    if (!validSlices(result, frames))
        result = equalSlices(frames, edit.density);
    measureHits(sample, result);
    return result;
}

void measureHits(const Sample& sample, Slices& slices) noexcept
{
    slices.hit.fill(0);
    const auto frames = sample.length();
    if (frames <= 0 || sample.channels < 1 || static_cast<int>(sample.pcm[0].size()) != frames)
        return;
    // Half-millisecond peak envelope over the first 30 ms of each chop. The attack is
    // the block with the biggest jump over what came before it.
    const auto block = std::max(4, static_cast<int>(std::lround(sample.sampleRate * 0.0005)));
    const auto longest = static_cast<int>(sample.sampleRate * maxHitSeconds);
    constexpr int maxBlocks = 96;
    for (int s = 0; s < slices.count; ++s)
    {
        const auto first = slices.edge[s], last = slices.edge[s + 1];
        const auto blocks = std::min(maxBlocks, std::min(longest + block, (last - first) / 2) / block);
        if (blocks < 4)
            continue;
        std::array<float, maxBlocks> envelope {};
        auto peak = 0.0f;
        for (int b = 0; b < blocks; ++b)
        {
            auto level = 0.0f;
            for (int i = first + b * block, end = i + block; i < end; ++i)
                level = std::max(level, magnitude(sample, i));
            envelope[static_cast<std::size_t>(b)] = level;
            peak = std::max(peak, level);
        }
        if (peak < 1.0e-4f)
            continue;
        int best = 0;
        auto bestJump = 0.0f;
        for (int b = 1; b < blocks; ++b)
        {
            if (envelope[static_cast<std::size_t>(b)] < 0.3f * peak)
                continue;
            auto before = 0.0f;
            for (int k = std::max(0, b - 4); k < b; ++k)
                before = std::max(before, envelope[static_cast<std::size_t>(k)]);
            const auto jump = envelope[static_cast<std::size_t>(b)] / (before + 0.02f * peak + 1.0e-6f);
            if (jump > bestJump)
            {
                bestJump = jump;
                best = b;
            }
        }
        // A chop that already opens on its loudest material starts on its attack.
        if (best == 0 || bestJump < 2.5f || envelope[0] >= 0.5f * envelope[static_cast<std::size_t>(best)])
            continue;
        const auto target = 0.25f * envelope[static_cast<std::size_t>(best)];
        auto at = first + (best - 1) * block;
        for (int i = at, end = first + (best + 1) * block; i < end; ++i)
            if (magnitude(sample, i) >= target)
            {
                at = i;
                break;
            }
        slices.hit[static_cast<std::size_t>(s)] = std::clamp(at - first, 0, longest);
    }
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

// ---- Dovetail ----------------------------------------------------------------

namespace
{
using Chroma = std::array<float, 12>;

void fft(std::vector<std::complex<float>>& data) noexcept
{
    const auto n = data.size();
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        auto bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(data[i], data[j]);
    }
    for (std::size_t length = 2; length <= n; length <<= 1)
    {
        const auto angle = -2.0 * pi / static_cast<double>(length);
        const std::complex<float> step(static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle)));
        for (std::size_t i = 0; i < n; i += length)
        {
            std::complex<float> w(1.0f, 0.0f);
            for (std::size_t k = 0; k < length / 2; ++k)
            {
                const auto u = data[i + k];
                const auto v = data[i + k + length / 2] * w;
                data[i + k] = u + v;
                data[i + k + length / 2] = u - v;
                w *= step;
            }
        }
    }
}

struct WindowPrint
{
    Chroma chroma {};
    float db = -90.0f, bright = 0.0f;
};

void normalise(Chroma& chroma) noexcept
{
    auto total = 0.0f;
    for (const auto value : chroma)
        total += value * value;
    if (total > 1.0e-12f)
        for (auto& value : chroma)
            value /= std::sqrt(total);
}

WindowPrint analyseWindow(const Sample& sample, int first, int length, std::vector<std::complex<float>>& work) noexcept
{
    WindowPrint print;
    const auto size = static_cast<int>(work.size());
    length = std::clamp(length, 0, size);
    if (length < 32)
        return print;
    std::fill(work.begin(), work.end(), std::complex<float>());
    double energy = 0.0;
    for (int i = 0; i < length; ++i)
    {
        const auto x = mono(sample, first + i);
        energy += static_cast<double>(x) * x;
        const auto window = 0.5f - 0.5f * static_cast<float>(std::cos(2.0 * pi * i / (length - 1)));
        work[static_cast<std::size_t>(i)] = { x * window, 0.0f };
    }
    print.db = static_cast<float>(10.0 * std::log10(energy / length + 1.0e-12));
    if (print.db < -70.0f)
        return print;
    fft(work);
    double weighted = 0.0, total = 0.0;
    const auto binHz = sample.sampleRate / size;
    for (int bin = 1; bin < size / 2; ++bin)
    {
        const auto frequency = bin * binHz;
        const auto magnitude = std::abs(work[static_cast<std::size_t>(bin)]);
        weighted += magnitude * frequency;
        total += magnitude;
        if (frequency >= 80.0 && frequency <= 5000.0)
        {
            const auto note = static_cast<int>(std::lround(12.0 * std::log2(frequency / 440.0)));
            print.chroma[static_cast<std::size_t>(((note % 12) + 12 + 9) % 12)] += magnitude;
        }
    }
    print.bright = total > 0.0 ? static_cast<float>(std::log2(std::max(20.0, weighted / total))) : 0.0f;
    normalise(print.chroma);
    return print;
}

Chroma rotate(const Chroma& chroma, int semitones) noexcept
{
    Chroma out {};
    for (int k = 0; k < 12; ++k)
        out[static_cast<std::size_t>(((k + semitones) % 12 + 12) % 12)] = chroma[static_cast<std::size_t>(k)];
    return out;
}

float similarity(const Chroma& a, const Chroma& b) noexcept
{
    auto sum = 0.0f;
    for (int k = 0; k < 12; ++k)
        sum += a[static_cast<std::size_t>(k)] * b[static_cast<std::size_t>(k)];
    return std::clamp(sum, 0.0f, 1.0f);
}

struct JoinTerms { float chroma = 0.0f, loud = 0.0f, bright = 0.0f; };

// The raw differences a listener hears where chop A (as played) hands over to chop B.
JoinTerms rawJoin(const FitTable& table, const Step& from, const Step& to) noexcept
{
    const auto& a = table.chop[static_cast<std::size_t>(from.slice % table.count)];
    const auto& b = table.chop[static_cast<std::size_t>(to.slice % table.count)];
    const auto& endChroma = from.reverse ? a.headChroma : a.tailChroma;
    const auto& startChroma = to.reverse ? b.tailChroma : b.headChroma;
    JoinTerms terms;
    terms.chroma = 1.0f - similarity(rotate(endChroma, from.pitch), rotate(startChroma, to.pitch));
    terms.loud = (to.reverse ? b.tailDb : b.headDb) - (from.reverse ? a.headDb : a.tailDb);
    terms.bright = ((to.reverse ? b.tailBright : b.headBright) + to.pitch / 12.0f)
                 - ((from.reverse ? a.headBright : a.tailBright) + from.pitch / 12.0f);
    return terms;
}
}

std::shared_ptr<FitTable> buildFitTable(const Sample& sample, const Slices& slices)
{
    auto table = std::make_shared<FitTable>();
    table->count = slices.count;
    if (slices.count <= 0 || sample.length() <= 0)
        return table;

    std::vector<std::complex<float>> work(4096);
    const auto headWindow = std::min(4096, static_cast<int>(sample.sampleRate * 0.060));
    const auto tailWindow = std::min(4096, static_cast<int>(sample.sampleRate * 0.080));
    for (int s = 0; s < slices.count; ++s)
    {
        const auto first = slices.edge[static_cast<std::size_t>(s)], last = slices.edge[static_cast<std::size_t>(s + 1)];
        const auto hitAt = std::min(last - 1, first + slices.hit[static_cast<std::size_t>(s)]);
        const auto head = analyseWindow(sample, hitAt, std::min(headWindow, last - hitAt), work);
        const auto tailLength = std::min(tailWindow, last - first);
        const auto tail = analyseWindow(sample, last - tailLength, tailLength, work);
        auto& print = table->chop[static_cast<std::size_t>(s)];
        print.headChroma = head.chroma;
        print.tailChroma = tail.chroma;
        print.headDb = head.db;
        print.tailDb = tail.db;
        print.headBright = head.bright;
        print.tailBright = tail.bright;
        for (int k = 0; k < 12; ++k)
            print.bodyChroma[static_cast<std::size_t>(k)] = head.chroma[static_cast<std::size_t>(k)] + tail.chroma[static_cast<std::size_t>(k)];
        normalise(print.bodyChroma);
    }

    // Learn what a natural join sounds like in this recording: chop i into chop i+1.
    const auto joins = slices.count - 1;
    if (joins >= 1)
    {
        std::vector<JoinTerms> natural;
        float drift = 0.0f;
        for (int s = 0; s < joins; ++s)
        {
            natural.push_back(rawJoin(*table, Step { s, 100, 1, 0, false }, Step { s + 1, 100, 1, 0, false }));
            drift += 1.0f - similarity(table->chop[static_cast<std::size_t>(s)].bodyChroma, table->chop[static_cast<std::size_t>(s + 1)].bodyChroma);
        }
        float loud = 0.0f, bright = 0.0f, chroma = 0.0f;
        for (const auto& t : natural) { loud += t.loud; bright += t.bright; chroma += t.chroma; }
        table->loudBias = loud / joins;
        table->brightBias = bright / joins;
        float loudSpread = 0.0f, brightSpread = 0.0f;
        for (const auto& t : natural)
        {
            loudSpread += std::abs(t.loud - table->loudBias);
            brightSpread += std::abs(t.bright - table->brightBias);
        }
        table->chromaScale = std::max(0.08f, chroma / joins);
        table->loudScale = std::max(1.5f, loudSpread / joins);
        table->brightScale = std::max(0.15f, brightSpread / joins);
        table->driftScale = std::max(0.08f, drift / joins);
    }
    return table;
}

float joinCost(const FitTable& table, const Step& from, const Step& to, const Step* anchor) noexcept
{
    if (table.count <= 0 || from.slice < 0 || to.slice < 0)
        return 0.0f;
    const auto terms = rawJoin(table, from, to);
    auto cost = 0.40f * terms.chroma / table.chromaScale
              + 0.25f * std::abs(terms.loud - table.loudBias) / table.loudScale
              + 0.20f * std::abs(terms.bright - table.brightBias) / table.brightScale;
    if (anchor != nullptr && anchor->slice >= 0)
    {
        // Keep the whole flip in the pinned chop's key.
        const auto& pinned = table.chop[static_cast<std::size_t>(anchor->slice % table.count)].bodyChroma;
        const auto& body = table.chop[static_cast<std::size_t>(to.slice % table.count)].bodyChroma;
        cost += 0.15f * (1.0f - similarity(rotate(body, to.pitch), rotate(pinned, anchor->pitch))) / table.driftScale;
    }
    const auto a = from.slice % table.count, b = to.slice % table.count;
    if (a == b)
        cost += 0.6f;      // a chop straight into itself is a stutter, not a flip
    else if (b == a + 1 && !from.reverse && !to.reverse && from.pitch == to.pitch)
        cost -= 0.35f;     // the recording's own join always fits
    return cost;
}

float joinQuality(const FitTable& table, const Step& from, const Step& to, const Step* anchor) noexcept
{
    return std::clamp(1.2f - 0.6f * joinCost(table, from, to, anchor), 0.0f, 1.0f);
}

bool randomizePattern(Pattern& pattern, int slices, int flags, int chaos, std::uint32_t& seed,
                      std::vector<Swap>* swaps, bool* fullShuffle, const FitContext* fit, std::vector<int>* chain) noexcept
{
    if (swaps != nullptr) swaps->clear();
    if (chain != nullptr) chain->clear();
    if (fullShuffle != nullptr) *fullShuffle = false;
    if (slices < 1 || (flags & randomAll) == 0)
        return false;

    const auto amount = std::clamp(chaos, 5, 100) / 100.0f;
    const auto original = pattern;
    auto result = pattern;
    // Dovetail is active when a step is pinned, it holds a hit, and the amount is up.
    const auto anchor = fit != nullptr && fit->table != nullptr && fit->table->count > 0 && fit->amount > 0
                        && fit->anchor >= 0 && fit->anchor < stepCount && pattern[static_cast<std::size_t>(fit->anchor)].slice >= 0
                        ? fit->anchor : -1;

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
            if (result[i].slice >= 0 && i != anchor) occupied.push_back(i);
        const auto moves = occupied.empty() ? 0 : std::max(1, static_cast<int>(std::lround(amount * occupied.size())));
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
                {
                    const auto at = draw(seed, stepCount);
                    if (auto& s = result[static_cast<std::size_t>(at)]; s.slice >= 0 && at != 0 && at != anchor)
                    {
                        s = {};
                        break;
                    }
                }
            }
        }
    }

    for (auto& step : result)
        if (step.slice >= 0) step.slice %= slices;

    // Step character. Chaos is the chance that each hit is re-rolled at all, so a low
    // Chaos keeps most of what you set by hand. The pinned step is never touched.
    for (int i = 0; i < stepCount; ++i)
    {
        auto& step = result[static_cast<std::size_t>(i)];
        if (step.slice < 0 || i == anchor)
            continue;
        if ((flags & randomStutter) != 0 && chance(seed) < amount)
        {
            constexpr std::array<int, 5> ratchets { 2, 2, 3, 4, 2 };
            step.ratchet = chance(seed) < 0.32f ? ratchets[static_cast<std::size_t>(draw(seed, 5))] : 1;
        }
        if ((flags & randomReverse) != 0 && chance(seed) < amount)
            step.reverse = chance(seed) < 0.3f;
        if ((flags & randomPitch) != 0 && chance(seed) < amount)
        {
            constexpr std::array<int, 6> intervals { -12, -7, -5, 5, 7, 12 };
            step.pitch = chance(seed) < 0.3f ? intervals[static_cast<std::size_t>(draw(seed, 6))] : 0;
        }
        if ((flags & randomGate) != 0 && chance(seed) < amount)
        {
            constexpr std::array<int, 5> gates { 25, 40, 60, 80, 100 };
            step.gate = gates[static_cast<std::size_t>(draw(seed, 5))];
        }
    }

    if (anchor >= 0 && slices >= 2)
    {
        // Dovetail: choose every hit's chop as one chain that starts at the pinned step
        // and wraps back round to it, sampled from p ~ exp(-Fit * cost / temperature)
        // with forward filtering, backward sampling.
        const auto& table = *fit->table;
        const auto choices = std::min(slices, table.count);
        std::vector<int> order;
        for (int k = 1; k < stepCount; ++k)
            if (const auto i = (anchor + k) % stepCount; result[static_cast<std::size_t>(i)].slice >= 0)
                order.push_back(i);
        const auto& pinned = result[static_cast<std::size_t>(anchor)];
        const auto temperature = 0.12f + 1.1f * std::clamp(chaos, 0, 100) / 100.0f;
        const auto strength = std::clamp(fit->amount, 0, 100) / 100.0f;
        auto link = [&](Step from, int fromChop, Step to, int toChop)
        {
            from.slice = fromChop;
            to.slice = toChop;
            return std::exp(-static_cast<double>(strength * joinCost(table, from, to, &pinned) / temperature));
        };
        const auto hits = static_cast<int>(order.size());
        std::vector<std::array<double, maxSlices>> alpha(static_cast<std::size_t>(hits));
        for (int k = 0; k < hits; ++k)
        {
            const auto& here = result[static_cast<std::size_t>(order[static_cast<std::size_t>(k)])];
            auto& a = alpha[static_cast<std::size_t>(k)];
            double total = 0.0;
            for (int c = 0; c < choices; ++c)
            {
                double sum = 0.0;
                if (k == 0)
                    sum = link(pinned, pinned.slice, here, c);
                else
                {
                    const auto& before = result[static_cast<std::size_t>(order[static_cast<std::size_t>(k - 1)])];
                    for (int p = 0; p < choices; ++p)
                        sum += alpha[static_cast<std::size_t>(k - 1)][static_cast<std::size_t>(p)] * link(before, p, here, c);
                }
                a[static_cast<std::size_t>(c)] = sum;
                total += sum;
            }
            for (int c = 0; c < choices; ++c)
                a[static_cast<std::size_t>(c)] = total > 0.0 ? a[static_cast<std::size_t>(c)] / total : 1.0 / choices;
        }
        auto nextStep = pinned;
        auto nextChop = pinned.slice;
        std::vector<int> picked(static_cast<std::size_t>(hits));
        for (int k = hits - 1; k >= 0; --k)
        {
            const auto& here = result[static_cast<std::size_t>(order[static_cast<std::size_t>(k)])];
            std::array<double, maxSlices> weightOf {};
            double total = 0.0;
            for (int c = 0; c < choices; ++c)
            {
                weightOf[static_cast<std::size_t>(c)] = alpha[static_cast<std::size_t>(k)][static_cast<std::size_t>(c)]
                                                        * link(here, c, nextStep, nextChop);
                total += weightOf[static_cast<std::size_t>(c)];
            }
            auto target = static_cast<double>(chance(seed)) * total;
            auto choice = choices - 1;
            for (int c = 0; c < choices; ++c)
                if ((target -= weightOf[static_cast<std::size_t>(c)]) <= 0.0)
                {
                    choice = c;
                    break;
                }
            picked[static_cast<std::size_t>(k)] = choice;
            nextStep = here;
            nextChop = choice;
        }
        for (int k = 0; k < hits; ++k)
            result[static_cast<std::size_t>(order[static_cast<std::size_t>(k)])].slice = picked[static_cast<std::size_t>(k)];
        if (chain != nullptr)
        {
            chain->push_back(pinned.slice);
            chain->insert(chain->end(), picked.begin(), picked.end());
        }
    }
    else if ((flags & randomChops) != 0 && slices >= 2)
    {
        // Swap chop positions. Works on the chop map, so every hit of chop A becomes B and
        // every hit of B becomes A; B doesn't have to be in the bank yet. Pairs never
        // share a chop, so what the readout says is exactly what happened.
        std::array<int, maxSlices> map {};
        std::iota(map.begin(), map.end(), 0);
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
            for (int i = static_cast<int>(used.size()) - 1; i > 0; --i)
                std::swap(used[static_cast<std::size_t>(i)], used[static_cast<std::size_t>(draw(seed, i + 1))]);
            const auto count = std::max(1, static_cast<int>(std::lround(amount * std::max<std::size_t>(2, used.size()) * 0.5)));
            std::array<bool, maxSlices> taken {};
            int made = 0;
            for (const auto a : used)
            {
                if (made >= count)
                    break;
                if (taken[static_cast<std::size_t>(a)])
                    continue;
                int options = 0;
                for (int c = 0; c < slices; ++c)
                    if (c != a && !taken[static_cast<std::size_t>(c)]) ++options;
                if (options == 0)
                    break;
                auto pick = draw(seed, options);
                int b = -1;
                for (int c = 0; c < slices && b < 0; ++c)
                    if (c != a && !taken[static_cast<std::size_t>(c)] && pick-- == 0)
                        b = c;
                taken[static_cast<std::size_t>(a)] = taken[static_cast<std::size_t>(b)] = true;
                std::swap(map[static_cast<std::size_t>(a)], map[static_cast<std::size_t>(b)]);
                if (swaps != nullptr) swaps->push_back({ a, b });
                ++made;
            }
        }
        for (auto& step : result)
            if (step.slice >= 0)
                step.slice = map[static_cast<std::size_t>(step.slice)];
    }

    if (result == original)
    {
        for (int i = 0; i < stepCount; ++i)
        {
            auto& step = result[static_cast<std::size_t>(i)];
            if (step.slice < 0 || i == anchor)
                continue;
            if (slices == 1)
            {
                step.reverse = !step.reverse;
                break;
            }
            const auto a = step.slice;
            auto b = draw(seed, slices - 1);
            if (b >= a) ++b;
            if (anchor >= 0)
            {
                step.slice = b;   // never touch the pinned chop
                break;
            }
            // Nothing changed: trade this chop with another one.
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

void Engine::Biquad::lowpass(double cutoff, double rate, double q) noexcept
{
    const auto w0 = 2.0 * pi * std::clamp(cutoff, 20.0, rate * 0.45) / rate;
    const auto alpha = std::sin(w0) / (2.0 * q);
    const auto cosine = std::cos(w0);
    const auto a0 = 1.0 + alpha;
    b0 = static_cast<float>((1.0 - cosine) * 0.5 / a0);
    b1 = static_cast<float>((1.0 - cosine) / a0);
    b2 = b0;
    a1 = static_cast<float>(-2.0 * cosine / a0);
    a2 = static_cast<float>((1.0 - alpha) / a0);
}

void Engine::Biquad::lowShelf(double frequency, double rate, double gainDb) noexcept
{
    const auto A = std::pow(10.0, gainDb / 40.0);
    const auto w0 = 2.0 * pi * frequency / rate;
    const auto cosine = std::cos(w0);
    const auto alpha = std::sin(w0) / 2.0 * std::sqrt(2.0);
    const auto root = 2.0 * std::sqrt(A) * alpha;
    const auto a0 = (A + 1.0) + (A - 1.0) * cosine + root;
    b0 = static_cast<float>(A * ((A + 1.0) - (A - 1.0) * cosine + root) / a0);
    b1 = static_cast<float>(2.0 * A * ((A - 1.0) - (A + 1.0) * cosine) / a0);
    b2 = static_cast<float>(A * ((A + 1.0) - (A - 1.0) * cosine - root) / a0);
    a1 = static_cast<float>(-2.0 * ((A - 1.0) + (A + 1.0) * cosine) / a0);
    a2 = static_cast<float>(((A + 1.0) + (A - 1.0) * cosine - root) / a0);
}

float Engine::Biquad::process(int channel, float x) noexcept
{
    auto& s1 = z1[static_cast<std::size_t>(channel)];
    auto& s2 = z2[static_cast<std::size_t>(channel)];
    const auto y = b0 * x + s1;
    s1 = b1 * x - a1 * y + s2;
    s2 = b2 * x - a2 * y;
    return y;
}

void Engine::prepare(double outputRate, float initialGain)
{
    (void) resamplingTable();
    sampleRate = std::isfinite(outputRate) && outputRate >= 8000.0 ? outputRate : 44100.0;
    microfadeFrames = std::max(1, static_cast<int>(std::lround(sampleRate * 0.002)));
    fadeInFrames = std::max(1, static_cast<int>(std::lround(sampleRate * 0.0003)));
    leadLimit = std::max(1, static_cast<int>(std::lround(sampleRate * maxLeadSeconds)));
    gain = gainTarget = initialGain;
    gainStep = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.008)));
    configuredDrive = -1.0f;
    reset();
}

void Engine::reset() noexcept
{
    endBlock();
    voices = {};
    pendingCount = 0;
    now = lastDue = 0;
    currentSample = ~0ull;
    clock = Clock::off;
    previewWasOn = false;
    playStep = playFrame = playSlice = -1;
    lastLeft = lastRight = 0.0f;
    replacementFade = 0;
    internalTick = 0.0;
    holdPhase = 1.0;
    heldLeft = heldRight = 0.0f;
    tone.clear();
    body.clear();
    shelf.clear();
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

double Engine::eventTick(std::int64_t step, int sub, int ratchet) const noexcept
{
    const auto pair = static_cast<double>(step >= 0 ? step / 2 : (step - 1) / 2);
    const auto s = static_cast<double>(swing);
    const auto even = step - static_cast<std::int64_t>(pair) * 2 == 0;
    const auto begin = even ? 2.0 * pair : 2.0 * pair + 1.0 + s;
    const auto length = even ? 1.0 + s : 1.0 - s;
    return begin + length * sub / std::max(1, ratchet);
}

void Engine::configureVintage(Vintage mode, float amount) noexcept
{
    if (mode != configuredVintage)
    {
        tone.clear();
        body.clear();
        shelf.clear();
        holdPhase = 1.0;
    }
    configuredVintage = vintage = mode;
    configuredDrive = drive = amount;
    double machineRate = sampleRate, cutoff = 20000.0, bits = 24.0, warmth = 0.0;
    switch (mode)
    {
        case Vintage::sixty:          machineRate = 40000.0; cutoff = 15000.0; bits = 12.0; warmth = 2.0; break;
        case Vintage::twelveHundred:  machineRate = 26040.0; cutoff = 16000.0; bits = 12.0; warmth = 1.0; break;
        case Vintage::threeThousand:  machineRate = 44100.0; cutoff = 18000.0; bits = 16.0; warmth = 1.5; break;
        case Vintage::clean:          break;
    }
    holdStep = machineRate >= sampleRate * 0.999 ? 1.0 : machineRate / sampleRate;
    levels = static_cast<float>(std::exp2(bits - 1.0));
    preGain = static_cast<float>(std::pow(10.0, (0.5 + 13.5 * amount) / 20.0));
    makeup = 1.0f / std::sqrt(preGain);
    tone.lowpass(cutoff, sampleRate, 0.707);
    body.lowpass(cutoff * 1.15, sampleRate, 0.6);
    shelf.lowShelf(90.0, sampleRate, warmth * (0.6 + 0.8 * amount));
}

void Engine::applyVintage(float& left, float& right) noexcept
{
    if (vintage == Vintage::clean)
        return;
    // Analog input stage: soft saturation, then the converter: sample-and-hold at the
    // machine's rate (no anti-alias filter, so the grit folds back) and 12/16-bit steps.
    const auto inputLeft = std::tanh(preGain * left) * makeup;
    const auto inputRight = std::tanh(preGain * right) * makeup;
    holdPhase += holdStep;
    if (holdPhase >= 1.0)
    {
        holdPhase -= std::floor(holdPhase);
        auto quantise = [this](float x)
        {
            return std::round(std::clamp(x, -1.0f, 1.0f) * levels) / levels;
        };
        heldLeft = quantise(inputLeft);
        heldRight = quantise(inputRight);
    }
    // Output stage: reconstruction filter and the low-end weight of the old outputs.
    left = body.process(0, tone.process(0, shelf.process(0, heldLeft)));
    right = body.process(1, tone.process(1, shelf.process(1, heldRight)));
}

void Engine::beginBlock(const Session& session, const Transport& transport,
                        const PlaySettings& settings, int samples) noexcept
{
    sample = session.sample.get();
    slices = &session.slices;
    pattern = &session.edit.patterns[static_cast<std::size_t>(session.edit.bank)];
    gainTarget = settings.gain;
    swing = std::clamp(settings.swing, 0.0f, 0.6f);
    choke = settings.choke;
    align = std::clamp(settings.align, 0.0f, 1.0f);
    const auto wantedDrive = std::clamp(settings.drive, 0.0f, 1.0f);
    if (settings.vintage != configuredVintage || std::abs(wantedDrive - configuredDrive) > 1.0e-4f)
        configureVintage(settings.vintage, wantedDrive);

    if (session.sampleRevision != currentSample)
    {
        currentSample = session.sampleRevision;
        replacedLeft = lastLeft;
        replacedRight = lastRight;
        replacementFade = microfadeFrames;
        voices = {};
        pendingCount = 0;
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
    if (origin == VoiceOrigin::pattern)
        pendingCount = 0;
}

void Engine::noteOff(VoiceOrigin origin, int note) noexcept
{
    if (choke != Choke::gate)
        return;   // CUT and POLY hits are one-shots, like pads with "Cut by" set
    for (auto& voice : voices)
        if (voice.origin == origin && voice.note == note)
            release(voice);
}

int Engine::leadFrames(int slice, int pitch, bool reverse) const noexcept
{
    if (align <= 0.0f || reverse || sample == nullptr || slices == nullptr || slice < 0 || slice >= slices->count)
        return 0;
    const auto ratio = std::clamp(baseRatio * speed * std::exp2(std::clamp(pitch, -maxPitch, maxPitch) / 12.0), 1.0 / 16.0, 16.0);
    return std::min(leadLimit, static_cast<int>(std::lround(align * slices->hit[static_cast<std::size_t>(slice)] / ratio)));
}

void Engine::trigger(int slice, float velocity, VoiceOrigin origin, int gateFrames, int pitch, bool reverse,
                     int note, bool aligned) noexcept
{
    start(slice, velocity, origin, gateFrames, pitch, reverse, note, aligned ? leadFrames(slice, pitch, reverse) : 0);
}

void Engine::start(int slice, float velocity, VoiceOrigin origin, int gateFrames, int pitch, bool reverse,
                   int note, int skip) noexcept
{
    if (sample == nullptr || slices == nullptr || slices->count <= 0 || slice < 0 || slice >= slices->count)
        return;
    for (auto& voice : voices)
        if (choke != Choke::poly || (voice.slice == slice && voice.origin == origin))
            release(voice);   // CUT and GATE: every hit cuts the last. POLY: a pad only cuts itself.

    auto voice = std::find_if(voices.begin(), voices.end(), [](const auto& v) { return !v.active(); });
    if (voice == voices.end())
        voice = std::min_element(voices.begin(), voices.end(),
                                [](const auto& a, const auto& b) { return a.frames - a.elapsed < b.frames - b.elapsed; });

    const auto ratio = std::clamp(baseRatio * speed * std::exp2(std::clamp(pitch, -maxPitch, maxPitch) / 12.0),
                                  1.0 / 16.0, 16.0);
    const auto first = slices->edge[slice];
    const auto last = slices->edge[slice + 1];
    const auto natural = std::max(1, static_cast<int>(std::ceil((last - first) / ratio)));
    // Starting late (Hit Align on a live hit) keeps a short fade-in ahead of the attack.
    skip = std::clamp(skip - fadeInFrames, 0, natural - 1);
    const auto remaining = natural - skip;
    const auto length = gateFrames > 0 ? std::min(remaining, gateFrames) : remaining;
    const auto offset = skip * ratio;
    Voice v;
    v.position = reverse ? static_cast<double>(last - 1) - offset : static_cast<double>(first) + offset;
    v.increment = reverse ? -ratio : ratio;
    v.start = first;
    v.end = last;
    v.frames = length;
    v.fadeIn = std::min(fadeInFrames, (length - 1) / 2);
    v.fadeOut = std::min(microfadeFrames, (length - 1) / 2);
    v.filter = std::clamp(static_cast<int>(std::ceil(16.0 * std::log2(std::max(1.0, ratio)))), 0, cutoffCount - 1);
    v.slice = slice;
    v.note = note;
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
    const auto shown = locate(tick);
    playStep = static_cast<int>((shown.step % stepCount + stepCount) % stepCount);

    // Look ahead so a chop whose attack sits a few ms after its cut can start early and
    // land its hit on the grid. The window stays under half the shortest step.
    const auto lookTicks = std::min(leadLimit * tickIncrement, 0.45 * (1.0 - swing));
    const auto point = locate(tick + lookTicks);
    const auto index = static_cast<int>((point.step % stepCount + stepCount) % stepCount);
    const auto& step = (*pattern)[static_cast<std::size_t>(index)];
    const auto ratchet = step.slice >= 0 ? std::clamp(step.ratchet, 1, maxRatchet) : 1;
    const auto sub = std::min(ratchet - 1, static_cast<int>(point.local * ratchet));
    const auto event = point.step * 8 + sub;
    if (event == previousEvent)
        return;

    previousEvent = event;
    if (step.slice < 0 || slices->count <= 0)
        return;

    const auto slice = step.slice % slices->count;
    const auto window = point.duration / tickIncrement / ratchet;
    // CUT and POLY let a full-gate hit ring until something cuts it.
    const auto gated = choke == Choke::gate || step.gate < 100;
    const auto gateFrames = gated ? std::max(1, static_cast<int>(std::lround(0.01 * step.gate * window))) : 0;
    const auto untilGrid = (eventTick(point.step, sub, ratchet) - (tick - tickEpsilon)) / tickIncrement;
    const auto lead = leadFrames(slice, step.pitch, step.reverse);
    const auto fireIn = untilGrid - lead;
    Pending hit;
    hit.slice = slice;
    hit.pitch = step.pitch;
    hit.reverse = step.reverse;
    hit.velocity = sub == 0 ? 1.0f : 0.82f;
    hit.skip = fireIn < 0.0 ? static_cast<int>(std::lround(-fireIn)) : 0;
    hit.gate = gateFrames > 0 ? gateFrames + lead - hit.skip : 0;
    // Hits keep their order even when a later chop needs more lead than an earlier one.
    hit.due = std::max(lastDue, now + (fireIn > 0.0 ? static_cast<std::int64_t>(std::lround(fireIn)) : 0));
    lastDue = hit.due;
    if (hit.due <= now)
        start(hit.slice, hit.velocity, VoiceOrigin::pattern, std::max(hit.gate, gateFrames > 0 ? 1 : 0), hit.pitch, hit.reverse, -1, hit.skip);
    else if (pendingCount < static_cast<int>(pending.size()))
        pending[static_cast<std::size_t>(pendingCount++)] = hit;
}

float Engine::interpolate(const float* pcm, const Voice& voice) const noexcept
{
    const auto position = voice.position;
    const auto integral = static_cast<int>(std::floor(position));
    const auto fraction = position - integral;
    if (std::abs(std::abs(voice.increment) - 1.0) < 1.0e-12 && fraction < 1.0e-12)
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
    while (pendingCount > 0 && pending[0].due <= now)
    {
        const auto hit = pending[0];
        std::move(pending.begin() + 1, pending.begin() + pendingCount, pending.begin());
        --pendingCount;
        start(hit.slice, hit.velocity, VoiceOrigin::pattern, hit.gate, hit.pitch, hit.reverse, -1, hit.skip);
    }

    left = right = 0.0f;
    if (sample != nullptr)
    {
        auto reported = false;
        for (auto& voice : voices)
        {
            if (!voice.active())
                continue;

            auto envelope = 1.0f;
            if (voice.fadeIn > 0 && voice.elapsed < voice.fadeIn)
                envelope = smooth((static_cast<float>(voice.elapsed) + 0.5f) / static_cast<float>(voice.fadeIn));
            const auto left2 = voice.frames - voice.elapsed - 1;
            if (voice.fadeOut > 0 && left2 < voice.fadeOut)
                envelope *= smooth(static_cast<float>(left2) / static_cast<float>(voice.fadeOut));
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

    applyVintage(left, right);
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
    ++now;
}
}
