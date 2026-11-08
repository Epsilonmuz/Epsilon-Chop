#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace chop
{
constexpr int maxSlices = 32;
constexpr int stepCount = 16;
constexpr int bankCount = 4;
constexpr int defaultRootNote = 60;   // C5 in FL Studio, which is what its step sequencer plays
constexpr int waveformBins = 2048;
constexpr int maxOnsets = 256;
constexpr int maxSampleFrames = 8'000'000;
constexpr int maxSampleSeconds = 120;
constexpr int maxRatchet = 4;
constexpr int maxPitch = 12;
constexpr int maxVoices = 16;
constexpr double maxHitSeconds = 0.030;     // Hit Align looks this far past a cut for the attack
constexpr double maxLeadSeconds = 0.060;    // and never starts a chop more than this early

// Numeric values 0-2 are kept from v0.1.0 so saved sessions keep their mode.
enum class SliceMode : int { grid = 0, transient = 1, manual = 2, beat = 3, random = 4 };
constexpr int sliceModeCount = 5;
enum class VoiceOrigin : int { midi, pattern, preview, keys };

// How hits end. CUT works like FL Studio's "Cut by 1" on every pad: each hit cuts
// the last one and rings until the next. GATE stops each hit at its step gate
// (v0.2.0 behaviour, and MIDI note lengths are honoured). POLY lets hits overlap.
enum class Choke : int { cut = 0, gate = 1, poly = 2 };
constexpr int chokeCount = 3;

// Vintage sampler character on the output.
enum class Vintage : int { clean = 0, sixty = 1, twelveHundred = 2, threeThousand = 3 };
constexpr int vintageCount = 4;

enum RandomFlag : int
{
    randomChops   = 1 << 0,
    randomRhythm  = 1 << 1,
    randomStutter = 1 << 2,
    randomReverse = 1 << 3,
    randomPitch   = 1 << 4,
    randomGate    = 1 << 5,
    randomAll     = (1 << 6) - 1
};
constexpr int defaultRandomFlags = randomChops | randomRhythm | randomStutter;

struct Peak { float low = 0.0f, high = 0.0f; };
struct Onset { int position = 0; float strength = 0.0f; };

struct Sample
{
    std::string name;
    double sampleRate = 44100.0;
    int channels = 0;
    std::array<std::vector<float>, 2> pcm;
    std::array<Peak, waveformBins> waveform {};
    std::vector<Onset> onsets;          // sorted by position
    float strongest = 0.0f;
    double estimatedBpm = 120.0;

    int length() const noexcept { return static_cast<int>(pcm[0].size()); }
};

struct Slices
{
    std::array<int, maxSlices + 1> edge {};
    int count = 0;
    std::array<int, maxSlices> hit {};   // frames from each cut to its attack (Hit Align)
};

struct Step
{
    int slice = -1;     // -1 = rest
    int gate = 100;     // 10..100 % of a step
    int ratchet = 1;    // 1..4 repeats inside the step
    int pitch = 0;      // -12..+12 semitones
    bool reverse = false;
    bool operator==(const Step& o) const noexcept
    {
        return slice == o.slice && gate == o.gate && ratchet == o.ratchet && pitch == o.pitch && reverse == o.reverse;
    }
    bool operator!=(const Step& o) const noexcept { return !(*this == o); }
};

using Pattern = std::array<Step, stepCount>;

struct Edit
{
    SliceMode mode = SliceMode::transient;
    int density = 16;          // Grid / Random: chop count. Transient: maximum chops.
    int sensitivity = 60;      // Transient: 0..100
    int beatDivision = 2;      // Beat: chops per beat (1, 2, 4)
    int sampleBpmTenths = 0;   // 0 = use the estimate
    bool snapZero = true;
    std::uint32_t chopSeed = 0x2b7e1516u;
    int bank = 0;
    int randomFlags = defaultRandomFlags;
    int chaos = 50;
    Slices manual;
    std::array<Pattern, bankCount> patterns;
    int fitAmount = 70;                                      // Dovetail: 0 = off
    std::array<int, bankCount> anchor { { -1, -1, -1, -1 } }; // pinned step per bank, -1 = none
};

// ---- Dovetail ---------------------------------------------------------------
// Each chop's start (head) and end (tail) are fingerprinted once per chop layout:
// pitch-class profile (chroma), loudness and brightness. A join from chop A to chop B
// is scored against the joins the sample itself makes between neighbouring chops, so
// "fits" means "fits the way this recording flows".
struct ChopPrint
{
    std::array<float, 12> headChroma {}, tailChroma {}, bodyChroma {};
    float headDb = -90.0f, tailDb = -90.0f;
    float headBright = 0.0f, tailBright = 0.0f;   // log2 of the spectral centroid
};

struct FitTable
{
    int count = 0;
    std::array<ChopPrint, maxSlices> chop {};
    float chromaScale = 1.0f, driftScale = 1.0f, loudScale = 1.0f, brightScale = 1.0f;
    float loudBias = 0.0f, brightBias = 0.0f;   // the sample's own typical join
};

struct FitContext
{
    const FitTable* table = nullptr;
    int anchor = -1;    // pinned step
    int amount = 0;     // 0..100
};

struct Session
{
    std::shared_ptr<const Sample> sample;
    Edit edit;
    Slices slices;
    std::shared_ptr<const FitTable> fit;   // message thread only
    std::uint64_t sampleRevision = 0;
    std::uint64_t revision = 0;
    std::uint32_t seed = 0x7f3259ac;
};

// ---- slicing ---------------------------------------------------------------
bool validSampleSize(double rate, int channels, std::int64_t frames) noexcept;
Slices equalSlices(int frames, int density) noexcept;
Slices makeSlices(const Sample&, const Edit&) noexcept;
bool validSlices(const Slices&, int frames) noexcept;
bool insertBoundary(Slices&, int position, int minimumSpacing) noexcept;
bool moveBoundary(Slices&, int index, int position, int minimumSpacing) noexcept;
bool removeBoundary(Slices&, int index) noexcept;
double sampleBpm(const Sample&, const Edit&) noexcept;
double estimateLoopBpm(int frames, double sampleRate) noexcept;
void measureHits(const Sample&, Slices&) noexcept;

// ---- patterns --------------------------------------------------------------
constexpr int presetCount = 4;
const char* presetName(int index) noexcept;
Pattern makePreset(int index) noexcept;
Pattern inOrder(int slices) noexcept;
Edit defaultEdit() noexcept;
bool sanitiseStep(Step&) noexcept;
// CHOPS swaps chops' places: a swap of 11 and 13 turns every 11 into 13 and every
// 13 into 11, timing untouched. Chaos sets how many swaps; 100% shuffles every chop.
// The swaps made are reported in `swaps` (empty after a full shuffle).
using Swap = std::pair<int, int>;
// With a FitContext whose anchor step holds a hit and amount > 0, Dovetail chooses
// the chops instead: every hit after the pinned one (wrapping back round to it) gets a
// chop that joins well, sampled as a whole chain. Chaos sets how adventurous it is.
// The pinned step itself never changes. The chosen chops are reported in `chain`.
bool randomizePattern(Pattern&, int slices, int flags, int chaos, std::uint32_t& seed,
                      std::vector<Swap>* swaps = nullptr, bool* fullShuffle = nullptr,
                      const FitContext* fit = nullptr, std::vector<int>* chain = nullptr) noexcept;
int hitCount(const Pattern&) noexcept;

std::shared_ptr<FitTable> buildFitTable(const Sample&, const Slices&);
float joinCost(const FitTable&, const Step& from, const Step& to, const Step* anchor) noexcept;
float joinQuality(const FitTable&, const Step& from, const Step& to, const Step* anchor) noexcept; // 0..1

// ---- playback --------------------------------------------------------------
struct Transport
{
    double ppq = 0.0;
    double bpm = 0.0;
    bool playing = false;
    bool hasPosition = false;
};

struct PlaySettings
{
    bool followHost = false;    // run the pattern when the host transport plays
    bool preview = false;       // run the pattern on the internal clock
    int stepsPerQuarter = 4;    // 2 = 1/8, 4 = 1/16, 8 = 1/32
    float swing = 0.0f;         // 0..0.6 of a step pair offset
    bool tempoFit = false;      // repitch the sample so its loop BPM matches the tempo
    double fallbackBpm = 120.0; // used when the host gives no tempo
    float gain = 1.0f;
    Choke choke = Choke::gate;
    float align = 0.0f;         // Hit Align 0..1: how much of each chop's lead-in is compensated
    Vintage vintage = Vintage::clean;
    float drive = 0.0f;         // 0..1
};

class Engine
{
public:
    void prepare(double outputRate, float initialGain = 1.0f);
    void reset() noexcept;
    void beginBlock(const Session&, const Transport&, const PlaySettings&, int samples) noexcept;
    // `aligned` starts the chop at its attack (Hit Align) for live playing, where it
    // can't start early. `note` lets a later noteOff find the voice in GATE mode.
    void trigger(int slice, float velocity, VoiceOrigin, int gateFrames = 0, int pitch = 0, bool reverse = false,
                 int note = -1, bool aligned = false) noexcept;
    void noteOff(VoiceOrigin, int note) noexcept;
    void stop(VoiceOrigin) noexcept;
    int leadFrames(int slice, int pitch, bool reverse) const noexcept;
    void render(int sampleInBlock, float& left, float& right) noexcept;
    void endBlock() noexcept { sample = nullptr; slices = nullptr; pattern = nullptr; }

    int currentStep() const noexcept { return playStep; }
    int currentFrame() const noexcept { return playFrame; }
    int currentSlice() const noexcept { return playSlice; }
    bool hostSynced() const noexcept { return clock == Clock::host; }
    bool running() const noexcept { return clock != Clock::off; }
    double tempo() const noexcept { return bpm; }

private:
    enum class Clock { off, host, internal };

    struct Voice
    {
        double position = 0.0, increment = 1.0;
        int start = 0, end = 0;
        int frames = 0, elapsed = 0;
        int fadeIn = 0, fadeOut = 0, release = 0, releaseLength = 0;
        int filter = 0, slice = -1, note = -1;
        float velocity = 0.0f;
        VoiceOrigin origin = VoiceOrigin::midi;

        bool active() const noexcept { return elapsed < frames; }
    };

    struct StepPoint { std::int64_t step; double local; double duration; };
    struct Pending
    {
        std::int64_t due = 0;
        int slice = -1, gate = 0, pitch = 0, skip = 0;
        float velocity = 0.0f;
        bool reverse = false;
    };
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        std::array<float, 2> z1 {}, z2 {};
        void lowpass(double cutoff, double rate, double q) noexcept;
        void lowShelf(double frequency, double rate, double gainDb) noexcept;
        float process(int channel, float x) noexcept;
        void clear() noexcept { z1 = {}; z2 = {}; }
    };

    StepPoint locate(double tick) const noexcept;
    double eventTick(std::int64_t step, int sub, int ratchet) const noexcept;
    void release(Voice&) const noexcept;
    void advancePattern(int sampleInBlock) noexcept;
    void start(int slice, float velocity, VoiceOrigin, int gateFrames, int pitch, bool reverse, int note, int skip) noexcept;
    float interpolate(const float*, const Voice&) const noexcept;
    void configureVintage(Vintage, float drive) noexcept;
    void applyVintage(float& left, float& right) noexcept;

    const Sample* sample = nullptr;
    const Slices* slices = nullptr;
    const Pattern* pattern = nullptr;
    std::array<Voice, maxVoices> voices {};
    std::array<Pending, 16> pending {};
    int pendingCount = 0;
    std::int64_t now = 0, lastDue = 0;
    Choke choke = Choke::gate;
    float align = 0.0f;
    int leadLimit = 2646, fadeInFrames = 14;
    Vintage vintage = Vintage::clean, configuredVintage = Vintage::clean;
    float drive = 0.0f, configuredDrive = -1.0f, preGain = 1.0f, makeup = 1.0f, levels = 0.0f;
    double holdStep = 1.0, holdPhase = 1.0;
    float heldLeft = 0.0f, heldRight = 0.0f;
    Biquad tone, body, shelf;
    std::uint64_t currentSample = ~0ull;
    double sampleRate = 44100.0, baseRatio = 1.0, speed = 1.0, bpm = 120.0;
    double tickStart = 0.0, tickIncrement = 0.0, expectedTick = 0.0, internalTick = 0.0;
    std::int64_t previousEvent = 0;
    float swing = 0.0f;
    Clock clock = Clock::off;
    bool previewWasOn = false;
    int microfadeFrames = 88, playStep = -1, playFrame = -1, playSlice = -1;
    int replacementFade = 0;
    float gain = 1.0f, gainTarget = 1.0f, gainStep = 1.0f;
    float lastLeft = 0.0f, lastRight = 0.0f, replacedLeft = 0.0f, replacedRight = 0.0f;
};
}
