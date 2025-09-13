#include "SamuRiserAudioEditor.h"
#include "SamuRiserAudioProcessor.h"

SamuRiserAudioEditor::SamuRiserAudioEditor(SamuRiserAudioProcessor& p, juce::AudioProcessorValueTreeState& s)
    : AudioProcessorEditor(p), processor(p), apvts(s)
{
    using namespace juce;

    // apply a dark theme so the interface looks consistent across hosts
    lookAndFeel.setColour(Slider::thumbColourId, Colours::orange);
    lookAndFeel.setColour(Slider::rotarySliderFillColourId, Colours::darkgrey);
    lookAndFeel.setColour(ProgressBar::foregroundColourId, Colours::yellow);
    setLookAndFeel(&lookAndFeel);

    const float scale = juce::Desktop::getInstance().getGlobalScaleFactor();
    auto setupSlider = [scale](Slider& s, const juce::String& tip)
    {
        s.setSliderStyle(Slider::Rotary);
        s.setTextBoxStyle(Slider::TextBoxBelow, false, int(60 * scale), int(20 * scale));
        s.setTooltip(tip);
    };

    addAndMakeVisible(bpmSlider);
    setupSlider(bpmSlider, "Beats per minute");
    bpmAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(apvts, "bpm", bpmSlider);

    addAndMakeVisible(barsSlider);
    setupSlider(barsSlider, "Loop length in bars");
    barsAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(apvts, "bars", barsSlider);

    addAndMakeVisible(keyNoteBox);
    keyNoteBox.addItemList({"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}, 1);
    keyNoteBox.setTooltip("Key root note");
    keyNoteAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment>(apvts, "keyNote", keyNoteBox);

    addAndMakeVisible(keyModeBox);
    keyModeBox.addItemList({"Major","Minor"}, 1);
    keyModeBox.setTooltip("Scale mode");
    keyModeAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment>(apvts, "keyMode", keyModeBox);

    addAndMakeVisible(flavorBox);
    flavorBox.addItemList({"Granular","Spectral","Hybrid"}, 1);
    flavorBox.setTooltip("Processing flavor");
    flavorAtt = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment>(apvts, "flavor", flavorBox);

    addAndMakeVisible(humanizeToggle);
    humanizeToggle.setTooltip("Adds timing jitter");
    humanizeAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(apvts, "humanize", humanizeToggle);

    addAndMakeVisible(densitySlider);
    setupSlider(densitySlider, "Simultaneous grains");
    densityAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(apvts, "density", densitySlider);

    addAndMakeVisible(textureSlider);
    setupSlider(textureSlider, "Transient vs pad bias");
    textureAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(apvts, "texture", textureSlider);

    addAndMakeVisible(mixSlider);
    setupSlider(mixSlider, "Hybrid blend");
    mixAtt = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(apvts, "hybridMix", mixSlider);

    addAndMakeVisible(rebuildButton);
    addAndMakeVisible(cancelButton);
    addAndMakeVisible(exportButton);
    addAndMakeVisible(saveButton);
    addAndMakeVisible(loadButton);
    rebuildButton.addListener(this);
    cancelButton.addListener(this);
    exportButton.addListener(this);
    saveButton.addListener(this);
    loadButton.addListener(this);
    rebuildButton.setTooltip("Rebuild loop");
    cancelButton.setTooltip("Cancel rebuild");
    exportButton.setTooltip("Export loop to WAV");
    saveButton.setTooltip("Save preset");
    loadButton.setTooltip("Load preset");

    addAndMakeVisible(poolLabel);
    poolLabel.setText("Pool: 0", juce::dontSendNotification);

    addAndMakeVisible(progressBar);
    progressBar.setVisible(false);
    cancelButton.setEnabled(false);
    progressBar.setTooltip("Rebuild progress");
    addAndMakeVisible(levelBar);
    levelBar.setTooltip("Output level");
    addAndMakeVisible(grainBar);
    grainBar.setTooltip("Active grains");
    startTimerHz(20);

    setResizable(true, true);
    setResizeLimits(400, 250, 1200, 900);
    setSize(600, 400);
}

SamuRiserAudioEditor::~SamuRiserAudioEditor()
{
    setLookAndFeel(nullptr);
}

void SamuRiserAudioEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
}

void SamuRiserAudioEditor::resized()
{
    auto area = getLocalBounds().reduced(10);
    auto rowH = area.getHeight() / 3;
    auto top = area.removeFromTop(rowH);
    auto mid = area.removeFromTop(rowH);
    auto bottom = area;

    auto cellWTop = top.getWidth() / 4;
    bpmSlider.setBounds(top.removeFromLeft(cellWTop));
    barsSlider.setBounds(top.removeFromLeft(cellWTop));
    keyNoteBox.setBounds(top.removeFromLeft(cellWTop));
    keyModeBox.setBounds(top.removeFromLeft(cellWTop));

    auto cellWMid = mid.getWidth() / 5;
    densitySlider.setBounds(mid.removeFromLeft(cellWMid));
    textureSlider.setBounds(mid.removeFromLeft(cellWMid));
    mixSlider.setBounds(mid.removeFromLeft(cellWMid));
    flavorBox.setBounds(mid.removeFromLeft(cellWMid));
    humanizeToggle.setBounds(mid.removeFromLeft(cellWMid));

    const float scale = juce::Desktop::getInstance().getGlobalScaleFactor();
    const int bottomHeight = juce::roundToInt(30 * scale);
    bottom.reduce(0, bottom.getHeight() - bottomHeight);
    poolLabel.setBounds(bottom.removeFromLeft(juce::roundToInt(150 * scale)));
    grainBar.setBounds(bottom.removeFromLeft(juce::roundToInt(80 * scale)));
    levelBar.setBounds(bottom.removeFromLeft(juce::roundToInt(80 * scale)));
    exportButton.setBounds(bottom.removeFromRight(juce::roundToInt(100 * scale)));
    rebuildButton.setBounds(bottom.removeFromRight(juce::roundToInt(100 * scale)));
    saveButton.setBounds(bottom.removeFromRight(juce::roundToInt(80 * scale)));
    loadButton.setBounds(bottom.removeFromRight(juce::roundToInt(80 * scale)));
    cancelButton.setBounds(bottom.removeFromRight(juce::roundToInt(100 * scale)));
    progressBar.setBounds(bottom);
}

void SamuRiserAudioEditor::filesDropped(const juce::StringArray& files, int, int)
{
    for (const auto& f : files)
    {
        poolLabel.setText("Importing...", juce::dontSendNotification);
        if (auto r = processor.addFile(juce::File(f)); r.failed())
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::Warning,
                                                  "Import failed",
                                                  r.getErrorMessage());
    }

    poolLabel.setText("Pool: " + juce::String(processor.getPoolSize()), juce::dontSendNotification);
}

void SamuRiserAudioEditor::buttonClicked(juce::Button* b)
{
    if (b == &rebuildButton)
    {
        poolLabel.setText("Rebuilding...", juce::dontSendNotification);
        rebuildButton.setEnabled(false);
        cancelButton.setEnabled(true);
        progressValue = 0.0;
        progressBar.setVisible(true);
        const double bpm = *apvts.getRawParameterValue("bpm");
        const int bars = (int)*apvts.getRawParameterValue("bars");
        const int keyNote = (int)*apvts.getRawParameterValue("keyNote");
        const bool minor = ((int)*apvts.getRawParameterValue("keyMode")) == 1;
        processor.rebuildLoopAsync(bpm, bars, 2, minor, keyNote);
    }
    else if (b == &cancelButton)
    {
        processor.cancelRebuild();
        progressBar.setVisible(false);
        rebuildButton.setEnabled(true);
        cancelButton.setEnabled(false);
        poolLabel.setText("Pool: " + juce::String(processor.getPoolSize()), juce::dontSendNotification);
    }
    else if (b == &exportButton)
    {
        juce::FileChooser chooser("Export Loop", juce::File(), "*.wav");
        if (chooser.browseForFileToSave(true))
        {
            auto r = processor.exportLoopToFile(chooser.getResult());
            if (r.failed())
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::Warning,
                                                      "Export failed",
                                                      r.getErrorMessage());
        }
    }
    else if (b == &saveButton)
    {
        juce::FileChooser chooser("Save Preset", juce::File(), "*.xml");
        if (chooser.browseForFileToSave(true))
            processor.savePreset(chooser.getResult());
    }
    else if (b == &loadButton)
    {
        juce::FileChooser chooser("Load Preset", juce::File(), "*.xml");
        if (chooser.browseForFileToOpen())
            processor.loadPreset(chooser.getResult());
    }
}

void SamuRiserAudioEditor::timerCallback()
{
    progressValue = processor.getRebuildProgress();
    if (! processor.isRebuilding())
    {
        progressBar.setVisible(false);
        rebuildButton.setEnabled(true);
        cancelButton.setEnabled(false);
        poolLabel.setText("Pool: " + juce::String(processor.getPoolSize()), juce::dontSendNotification);
    }
    levelValue = processor.getOutputLevel();
    grainValue = (double)processor.getActiveGrains() / 64.0;
}
