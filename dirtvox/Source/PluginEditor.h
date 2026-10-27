#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class DirtLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DirtLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
};

class DirtVoxEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DirtVoxEditor (DirtVoxProcessor&);
    ~DirtVoxEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        bool big = false;
    };

    DirtVoxProcessor& proc;
    DirtLookAndFeel lnf;
    juce::TextButton analyzeButton;
    juce::Label status;
    juce::ComboBox character;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> characterAttachment;
    std::array<Knob, 10> knobs;
    juce::Rectangle<int> progressArea, grArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DirtVoxEditor)
};
