#pragma once

#include "PluginProcessor.h"
#include "Widgets.h"

class EpsilonChopEditor;

struct Canvas final : juce::Component
{
    std::function<void(juce::Graphics&)> painter, overPainter;
    void paint(juce::Graphics& g) override { if (painter) painter(g); }
    void paintOverChildren(juce::Graphics& g) override { if (overPainter) overPainter(g); }
};

// The hero: colour-coded waveform. Each chop owns a colour, and that colour
// follows it into the arrangement ribbon.
class WaveformView final : public juce::Component
{
public:
    explicit WaveformView(EpsilonChopEditor& owner) : editor(owner) {}

    void contentChanged() { cacheValid = false; repaint(); }
    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    void renderCache(juce::Graphics&, int width, int height) const;
    float xAt(int frame) const;
    int frameAt(float x) const;
    int markerNear(float x) const;
    int sliceAt(float x) const;

    EpsilonChopEditor& editor;
    juce::Image cache;
    float cacheScale = 0.0f;
    bool cacheValid = false;
    int hoverSlice = -1, hoverMarker = -1;
    int dragMarker = -1, dragFrame = 0, downSlice = -1;
    std::uint64_t dragRevision = 0;
    bool dragStarted = false;
};

// Sixteen steps, each showing the chop it plays as a little re-cut waveform.
class RibbonView final : public juce::Component, public juce::DragAndDropTarget
{
public:
    explicit RibbonView(EpsilonChopEditor& owner) : editor(owner) {}

    void contentChanged() { cacheValid = false; repaint(); }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragMove(const SourceDetails&) override;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;

    juce::Rectangle<float> cell(int index) const;

private:
    void renderCache(juce::Graphics&) const;
    void paintCell(juce::Graphics&, int index, juce::Rectangle<float>) const;
    int cellAt(juce::Point<float>) const;

    EpsilonChopEditor& editor;
    juce::Image cache;
    float cacheScale = 0.0f;
    bool cacheValid = false;
    int dropTarget = -1;
    double wheelAccumulator = 0.0;
};

class EpsilonChopEditor final : public juce::AudioProcessorEditor,
                               public juce::DragAndDropContainer,
                               public juce::FileDragAndDropTarget,
                               private juce::Timer
{
public:
    explicit EpsilonChopEditor(EpsilonChopProcessor&);
    ~EpsilonChopEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void fileDragEnter(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;

    // Shared with the views
    EpsilonChopProcessor& processor;
    EpsilonChopProcessor::EditorState view;
    int selectedStep = 0, selectedChop = 0;
    const chop::Pattern& pattern() const { return view.session.edit.patterns[static_cast<std::size_t>(view.session.edit.bank)]; }
    void selectStep(int index);
    void selectChop(int index);
    void setStep(int index, chop::Step, bool remember = true);
    void pull();
    void chooseSample();
    void tick() { timerCallback(); }   // used by the snapshot tests

    static constexpr int designWidth = 1120, designHeight = 752;

private:
    void timerCallback() override;
    void syncControls();
    void layoutContext();
    void editSelected(const std::function<void(chop::Step&)>&, bool remember = true);
    void paintHeader(juce::Graphics&);
    void paintPanels(juce::Graphics&);
    void paintFooter(juce::Graphics&);
    void setParameter(const juce::String& id, float plainValue);

    theme::Look look;
    Canvas content;            // laid out at design size and scaled as a whole
    juce::TooltipWindow tooltips { this, 600 };

    WaveformView waveform { *this };
    RibbonView ribbon { *this };

    juce::TextButton load { "LOAD SAMPLE" };
    ui::Knob output { "OUTPUT" };

    ui::Segmented mode, density, division;
    juce::Slider sensitivity;
    juce::TextButton rechop { "RE-CHOP" }, snap { "ZERO-X" };
    ui::ValueBox loopBpm;

    ui::Segmented banks;
    juce::ComboBox grooves;
    juce::TextButton inOrderButton { "IN ORDER" }, clearButton { "CLEAR" };

    ui::PlayButton play;
    ui::ValueBox tempo;
    ui::Segmented rate;
    juce::TextButton hostSync { "HOST SYNC" }, tempoFit { "TEMPO FIT" };
    ui::Knob swing { "SWING" };

    ui::DiceButton dice;
    ui::Knob chaos { "CHAOS" };
    std::array<juce::TextButton, 6> flags;
    juce::TextButton undoButton { "UNDO" };

    juce::TextButton previousChop { "<" }, nextChop { ">" };
    ui::Knob gate { "GATE" }, pitch { "PITCH" };
    ui::Segmented ratchet;
    juce::TextButton reverse { "REVERSE" }, rest { "REST" };

    juce::ComboBox root;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment, swingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> hostAttachment, fitAttachment;
    std::unique_ptr<juce::FileChooser> chooser;

    std::uint64_t seenRevision = ~0ull, seenStatus = ~0ull;
    int seenStep = -2, seenFrame = -2, seenSlice = -2;
    bool seenPlaying = false, seenSynced = false, seenHostTempo = false;
    bool draggingFile = false, firstGateChange = false, firstPitchChange = false, firstSensitivityChange = false;
    float scale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EpsilonChopEditor)
};
