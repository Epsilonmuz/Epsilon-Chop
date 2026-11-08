#include "SampleAnalysis.h"
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace chop
{
void buildWaveform(Sample& sample) noexcept
{
    const auto frames = sample.length();
    if (frames == 0)
        return;

    for (int bin = 0; bin < waveformBins; ++bin)
    {
        const auto first = static_cast<int>((static_cast<std::int64_t>(frames) * bin) / waveformBins);
        const auto last = std::max(first + 1, static_cast<int>((static_cast<std::int64_t>(frames) * (bin + 1)) / waveformBins));
        Peak peak { sample.pcm[0][first], sample.pcm[0][first] };
        for (int channel = 0; channel < sample.channels; ++channel)
            for (int frame = first; frame < last; ++frame)
            {
                const auto value = sample.pcm[channel][frame];
                peak.low = std::min(peak.low, value);
                peak.high = std::max(peak.high, value);
            }
        sample.waveform[bin] = peak;
    }
}

bool analyzeTransients(Sample& sample, const std::function<bool()>& cancelled)
{
    const auto frames = sample.length();
    const auto rate = sample.sampleRate;
    sample.onsets.clear();
    sample.strongest = 0.0f;
    sample.estimatedBpm = estimateLoopBpm(frames, rate);
    if (frames < 64)
        return !cancelled();
    int order = 8;
    while ((1 << order) < rate * 0.016)
        ++order;

    const auto size = 1 << order;
    const auto hop = size / 4;
    const auto bins = size / 2 + 1;
    const auto columns = (frames + hop - 1) / hop;
    juce::dsp::FFT fft(order);
    std::vector<float> work(2 * size), window(size), previous(bins), current(bins), flux(columns);
    for (int index = 0; index < size; ++index)
        window[index] = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * index / (size - 1));

    for (int column = 0; column < columns; ++column)
    {
        if (column % 64 == 0 && cancelled())
            return false;
        std::fill(current.begin(), current.end(), 0.0f);
        const auto start = column * hop - size / 2;
        for (int channel = 0; channel < sample.channels; ++channel)
        {
            std::fill(work.begin(), work.end(), 0.0f);
            for (int index = std::max(0, -start); index < std::min(size, frames - start); ++index)
                work[index] = window[index] * sample.pcm[channel][start + index];

            fft.performFrequencyOnlyForwardTransform(work.data(), true);
            for (int bin = 1; bin < bins; ++bin)
                current[bin] = std::max(current[bin], std::log1p(work[bin] * (32.0f / size)));
        }
        // Adjacent-bin maxima tolerate small spectral motion on sustained notes.
        for (int bin = 2; bin + 1 < bins; ++bin)
        {
            const auto baseline = std::max({ previous[bin - 1], previous[bin], previous[bin + 1] });
            flux[column] += std::max(0.0f, current[bin] - baseline);
        }
        flux[column] /= static_cast<float>(bins);
        previous.swap(current);
    }

    const auto strongest = flux.empty() ? 0.0f : *std::max_element(flux.begin(), flux.end());
    std::vector<double> sum(flux.size() + 1);
    std::partial_sum(flux.begin(), flux.end(), sum.begin() + 1);
    struct Candidate { int centre; float strength; };
    std::vector<Candidate> peaks;
    for (int column = 1; column + 1 < columns; ++column)
    {
        const auto first = std::max(0, column - 10);
        const auto last = std::min(columns, column + 11);
        const auto mean = (sum[last] - sum[first] - flux[column]) / (last - first - 1);
        const auto strength = flux[column];
        // A low floor keeps weak candidates; the Sensitivity control chooses among them later.
        if (strength > flux[column - 1] && strength >= flux[column + 1]
            && strength > 1.25 * mean + std::max(0.00002f, strongest * 0.004f))
            peaks.push_back({ column * hop, static_cast<float>(strength - mean) });
    }
    std::stable_sort(peaks.begin(), peaks.end(),
                     [](const auto& a, const auto& b) { return a.strength > b.strength; });

    const auto edgeMargin = static_cast<int>(rate * 0.010);
    const auto separation = static_cast<int>(rate * 0.035);
    const auto leadIn = static_cast<int>(rate * 0.001);
    auto absoluteAt = [&sample](int index)
    {
        const auto left = std::abs(sample.pcm[0][static_cast<std::size_t>(index)]);
        return sample.channels == 2 ? std::max(left, std::abs(sample.pcm[1][static_cast<std::size_t>(index)])) : left;
    };
    std::vector<Onset> onsets;
    for (const auto& peak : peaks)
    {
        if (static_cast<int>(onsets.size()) == maxOnsets)
            break;

        const auto first = std::max(0, peak.centre - hop);
        const auto last = std::min(frames, peak.centre + size / 2);
        float high = 0.0f;
        for (int index = first; index < last; ++index)
            high = std::max(high, absoluteAt(index));
        const auto threshold = high * 0.15f;
        auto position = first;
        while (position < last && absoluteAt(position) < threshold)
            ++position;
        position = std::max(0, position - leadIn);
        if (position < edgeMargin || position >= frames - edgeMargin)
            continue;
        const auto found = std::any_of(onsets.begin(), onsets.end(),
                                       [position, separation](const Onset& other) { return std::abs(other.position - position) < separation; });
        if (!found)
            onsets.push_back({ position, peak.strength });
    }
    sample.strongest = 0.0f;
    for (const auto& onset : onsets)
        sample.strongest = std::max(sample.strongest, onset.strength);
    std::sort(onsets.begin(), onsets.end(), [](const auto& a, const auto& b) { return a.position < b.position; });
    sample.onsets = std::move(onsets);
    sample.estimatedBpm = estimateLoopBpm(frames, rate);
    return !cancelled();
}
}
