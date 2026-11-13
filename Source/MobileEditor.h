#pragma once
// Epsilon Chop for iPhone: the same engine and processor, with a touch layout made for a phone held
// upright. Used as the editor on iOS (see EpsilonChopProcessor::createEditor). It also builds on
// desktop so the snapshot tests can draw it at iPhone sizes.
//
//   header   : Hallmark, sample name, MIC and LOAD
//   waveform : colour-coded chops; tap one to hear it
//   chop row : TRANSIENT / BEAT / GRID / RANDOM and how many chops
//   transport: PLAY, TEMPO, RANDOMIZE
//   pages    : PADS (4x4)  CHOP (slices)  PATTERN (16 steps)  SOUND (pitch, vintage)  PROJECT (save/share)

#include "PluginProcessor.h"
#include "Widgets.h"

namespace mobile
{
class Editor;

// Colour-coded waveform of the loaded sample; a tap auditions the chop under the finger.
class Wave final : public juce::Component
{
public:
    explicit Wave(Editor& e) : editor(e) {}
    void contentChanged() { cache = {}; draggedBoundary = -1; repaint(); }
    void setPlaying(int slice, int frame);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    void render(float pixelScale);
    int sliceAt(float x) const;
    Editor& editor;
    juce::Image cache;
    float cacheScale = 0.0f;
    int shownSlice = -1, shownFrame = -1;
    int draggedBoundary = -1, dragFrame = 0;
};

// One finger-drum pad. Touch-down plays the chop (Hit Align, choke apply), lifting the finger
// ends it in GATE mode. Several pads can be held at once.
class Pad final : public juce::Component
{
public:
    explicit Pad(Editor& e) : editor(e) {}
    int chop = 0;
    bool lit = false, exists = false;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void release();

private:
    Editor& editor;
    int heldChop = -1;
};

// The 16-step pattern as two rows of eight cells.
class Steps final : public juce::Component
{
public:
    explicit Steps(Editor& e) : editor(e) {}
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    juce::Rectangle<float> cell(int index) const;
    int playing = -1;

private:
    Editor& editor;
};

class Editor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Editor(EpsilonChopProcessor&);
    ~Editor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void tick() { timerCallback(); }               // snapshot tests
    void showPage(int page);

    // Shared with the parts
    EpsilonChopProcessor& processor;
    EpsilonChopProcessor::EditorState view;
    int selectedStep = 0, selectedChop = 0, padBank = 0;
    const chop::Pattern& pattern() const { return view.session.edit.patterns[(size_t) view.session.edit.bank]; }
    void auditionChop(int chop);
    void playPad(int chop);
    void selectStep(int step);
    void pull();

    static constexpr int designWidth = 390, designHeight = 760;

private:
    friend class Wave;
    class MicCapture;

    void timerCallback() override;
    void syncControls();
    void layoutPage();
    void chooseSample();
    void saveProject();
    void refreshProjects();
    void openProject();
    void toggleMic();
    void startMic();
    void finishMic(bool interrupted);
    void toggleSequenceRecord();
    void finishSequenceRecord();
    void setToast(juce::String);
    bool paintCapture(juce::Graphics&, juce::Rectangle<float>) const;
    void share(const juce::StringArray& files);
    void setParameter(const juce::String& id, float value);
    void editStep(const std::function<void(chop::Step&)>&);
    void paintContent(juce::Graphics&);

    theme::Look look;
    struct Canvas final : juce::Component
    {
        std::function<void(juce::Graphics&)> painter;
        void paint(juce::Graphics& g) override { if (painter) painter(g); }
    } content;

    Wave wave { *this };
    juce::TextButton load { "LOAD" };
    juce::TextButton mic { "MIC" }, sequenceRecord { "REC SEQ" };
    ui::Segmented mode, density, division, tabs;
    ui::PlayButton play;
    ui::ValueBox tempo;
    ui::DiceButton dice;

    // PADS
    std::vector<std::unique_ptr<Pad>> pads;
    ui::Segmented padBanks;
    // PATTERN
    ui::Segmented banks, rate;
    Steps steps { *this };
    juce::TextButton prevChop { "<" }, nextChop { ">" }, rest { "REST" }, pin { "PIN" }, reverse { "REV" },
                     inOrder { "IN ORDER" }, clear { "CLEAR" }, undo { "UNDO" };
    ui::Knob chaos { "CHAOS" }, dovetail { "DOVETAIL" }, swing { "SWING" };
    // SOUND
    ui::Knob pitch { "PITCH" }, output { "OUTPUT" }, drive { "DRIVE" }, align { "HIT ALIGN" };
    ui::Segmented pitchMode, vintage, choke;
    // EXPORT
    juce::TextButton shareLoop { "SHARE LOOP" }, shareChops { "SHARE ALL CHOPS" }, shareChop { "SHARE THIS CHOP" };
    juce::TextButton saveSet { "SAVE PROJECT" }, openSet { "OPEN PROJECT" };
    juce::ComboBox projectList;
    juce::Array<juce::File> projects;
    ui::Segmented loopBars;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pitchAtt, outputAtt, driveAtt, alignAtt, swingAtt;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<MicCapture> capture;
    std::function<void()> stopObservingApp;
    bool micPermissionPending = false;
    bool interruptedTake = false;
    bool sequenceArmed = false;
    chop::Pattern recordedPattern {};
    std::uint64_t recordedSample = 0;
    int recordedBank = 0;
    juce::ScopedMessageBox shareBox;
    juce::Component::SafePointer<juce::AlertWindow> namePrompt;
    juce::String toast;
    int toastTicks = 0, page = 0, extraHeight = 0;
    juce::BorderSize<int> appliedInsets;
    float scale = 1.0f;
    std::uint64_t seenRevision = ~0ull, seenStatus = ~0ull;
    int seenSlice = -2, seenFrame = -2, seenStep = -2;
    bool seenPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Editor)
};
}
