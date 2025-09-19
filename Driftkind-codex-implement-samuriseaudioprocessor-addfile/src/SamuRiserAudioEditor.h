#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

class SamuRiserAudioProcessor;

class SamuRiserAudioEditor : public juce::AudioProcessorEditor,
                            private juce::FileDragAndDropTarget,
                            private juce::Button::Listener,
                            private juce::Timer
{
public:
    SamuRiserAudioEditor(SamuRiserAudioProcessor&, juce::AudioProcessorValueTreeState&);
    ~SamuRiserAudioEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
    void filesDropped(const juce::StringArray& files, int, int) override;
    void buttonClicked(juce::Button*) override;
    void timerCallback() override;

private:
    SamuRiserAudioProcessor& processor;
    juce::AudioProcessorValueTreeState& apvts;

    juce::Slider bpmSlider, barsSlider, densitySlider, textureSlider, mixSlider;
    juce::ComboBox keyNoteBox, keyModeBox, flavorBox;
    juce::ToggleButton humanizeToggle {"Humanize"};
    juce::TextButton rebuildButton {"Rebuild"}, cancelButton {"Cancel"},
                     exportButton {"Export"},
                     saveButton {"Save"}, loadButton {"Load"};
    juce::Label poolLabel;
    double progressValue = 0.0;
    juce::ProgressBar progressBar {progressValue};
    double levelValue = 0.0;
    juce::ProgressBar levelBar {levelValue};
    double grainValue = 0.0;
    juce::ProgressBar grainBar {grainValue};

    // simple dark look and feel for consistent theming
    juce::LookAndFeel_V4 lookAndFeel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bpmAtt, barsAtt, densityAtt, textureAtt, mixAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyNoteAtt, keyModeAtt, flavorAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> humanizeAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamuRiserAudioEditor)
};
