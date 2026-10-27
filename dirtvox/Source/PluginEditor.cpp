#include "PluginEditor.h"

static juce::String ru (const char* s) { return juce::String::fromUTF8 (s); }

namespace col
{
const juce::Colour bg   { 0xff121014 };
const juce::Colour panel { 0xff1d1a20 };
const juce::Colour red  { 0xffe0353f };
const juce::Colour text { 0xffe8e2e6 };
const juce::Colour dim  { 0xff8a8290 };
}

DirtLookAndFeel::DirtLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, col::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, col::text);
    setColour (juce::ComboBox::backgroundColourId, col::panel);
    setColour (juce::ComboBox::outlineColourId, col::red.withAlpha (0.5f));
    setColour (juce::ComboBox::textColourId, col::text);
    setColour (juce::PopupMenu::backgroundColourId, col::panel);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::red);
    setColour (juce::TextButton::textColourOffId, juce::Colours::white);
}

void DirtLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&)
{
    const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4);
    const float rad = std::min (r.getWidth(), r.getHeight()) / 2;
    const auto c = r.getCentre();
    const float ang = start + pos * (end - start);

    juce::Path track;
    track.addCentredArc (c.x, c.y, rad - 2, rad - 2, 0, start, end, true);
    g.setColour (juce::Colour (0xff2c2830));
    g.strokePath (track, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path val;
    val.addCentredArc (c.x, c.y, rad - 2, rad - 2, 0, start, ang, true);
    g.setColour (col::red);
    g.strokePath (val, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float capR = rad * 0.68f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a343e), c.x - capR, c.y - capR, juce::Colour (0xff151217), c.x + capR, c.y + capR, false));
    g.fillEllipse (c.x - capR, c.y - capR, capR * 2, capR * 2);
    juce::Path ind;
    ind.addRoundedRectangle (-1.5f, -capR, 3.0f, capR * 0.55f, 1.5f);
    g.setColour (juce::Colours::white);
    g.fillPath (ind, juce::AffineTransform::rotation (ang).translated (c.x, c.y));
}

void DirtLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1);
    auto base = b.getToggleState() ? juce::Colour (0xff7a1d24) : col::red;
    if (over) base = base.brighter (0.15f);
    if (down) base = base.darker (0.2f);
    g.setGradientFill (juce::ColourGradient (base.brighter (0.1f), 0, r.getY(), base.darker (0.35f), 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, 6);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (r, 6, 1);
}

DirtVoxEditor::DirtVoxEditor (DirtVoxProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    analyzeButton.setButtonText (ru ("АНАЛИЗ + ОБРАБОТКА"));
    analyzeButton.onClick = [this]
    {
        if (proc.isListening()) proc.cancelAnalysis(); else proc.startAnalysis();
    };
    addAndMakeVisible (analyzeButton);

    status.setJustificationType (juce::Justification::centredLeft);
    status.setFont (juce::FontOptions (13.0f));
    status.setColour (juce::Label::textColourId, col::text);
    addAndMakeVisible (status);

    character.addItemList ({ "Warm", "Rage", "Blown" }, 1);
    addAndMakeVisible (character);
    characterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "character", character);

    const char* ids[10]    = { "dirt", "mix", "tone", "output", "trim", "hpf", "mud", "presence", "comp", "deess" };
    const char* titles[10] = { "DIRT", "MIX", "TONE", "OUTPUT", "TRIM", "LOW CUT", "MUD", "PRESENCE", "COMP", "DE-ESS" };
    for (int i = 0; i < 10; ++i)
    {
        auto& k = knobs[(size_t) i];
        k.big = i == 0;
        k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 16);
        k.slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        addAndMakeVisible (k.slider);
        k.label.setText (titles[i], juce::dontSendNotification);
        k.label.setJustificationType (juce::Justification::centred);
        k.label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        k.label.setColour (juce::Label::textColourId, i < 4 ? col::text : col::dim);
        addAndMakeVisible (k.label);
        k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, ids[i], k.slider);
    }
    setSize (640, 420);
    startTimerHz (20);
}

DirtVoxEditor::~DirtVoxEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void DirtVoxEditor::timerCallback()
{
    status.setText (proc.getStatus(), juce::dontSendNotification);
    const bool listening = proc.isListening();
    analyzeButton.setToggleState (listening, juce::dontSendNotification);
    analyzeButton.setButtonText (listening ? ru ("СТОП") : ru ("АНАЛИЗ + ОБРАБОТКА"));
    repaint (progressArea.getUnion (grArea));
}

void DirtVoxEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    auto r = getLocalBounds();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a0d10), 0, 0, col::bg, 0, 90, false));
    g.fillRect (r.removeFromTop (90));

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
    g.drawText ("DIRT VOX", 20, 14, 300, 34, juce::Justification::left);
    g.setColour (col::red);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (ru ("грязный вокал · автосведение в одну кнопку"), 22, 46, 400, 16, juce::Justification::left);

    // Полоса прогресса анализа
    g.setColour (col::panel);
    g.fillRoundedRectangle (progressArea.toFloat(), 3);
    g.setColour (col::red);
    g.fillRoundedRectangle (progressArea.toFloat().withWidth (progressArea.getWidth() * proc.getProgress()), 3);

    // Секции
    g.setColour (col::panel);
    g.fillRoundedRectangle (20, 168, 600, 120, 8);
    g.fillRoundedRectangle (20, 296, 600, 110, 8);
    g.setColour (col::dim);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText (ru ("ГРЯЗЬ"), 30, 172, 100, 12, juce::Justification::left);
    g.drawText (ru ("АВТО (выставляет анализ, можно крутить)"), 30, 300, 300, 12, juce::Justification::left);
    g.drawText (ru ("ХАРАКТЕР"), 410, 136, 85, 12, juce::Justification::right);

    // Индикаторы подавления: компрессор и де-эссер
    const float gr[2] = { proc.getCompGr(), proc.getDeessGr() };
    const char* nm[2] = { "COMP GR", "DE-ESS GR" };
    for (int i = 0; i < 2; ++i)
    {
        auto a = grArea.withHeight (8).translated (0, i * 16);
        g.setColour (col::dim);
        g.drawText (nm[i], a.withWidth (70).translated (0, -4).withHeight (14), juce::Justification::left);
        auto bar = a.withTrimmedLeft (72).toFloat();
        g.setColour (col::panel); g.fillRoundedRectangle (bar, 2);
        g.setColour (juce::Colour (0xffffb04a));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, gr[i] / 18.0f)), 2);
    }
}

void DirtVoxEditor::resized()
{
    analyzeButton.setBounds (340, 18, 280, 44);
    progressArea = { 20, 74, 600, 6 };
    status.setBounds (16, 84, 608, 36);
    grArea = { 20, 128, 300, 30 };
    character.setBounds (500, 130, 120, 24);

    // Ряд «Грязь»: большая DIRT + MIX, TONE, OUTPUT
    knobs[0].slider.setBounds (40, 184, 110, 100);
    knobs[0].label.setBounds (150, 214, 60, 16);
    for (int i = 1; i < 4; ++i)
    {
        const int x = 230 + (i - 1) * 130;
        knobs[(size_t) i].label.setBounds (x, 186, 90, 14);
        knobs[(size_t) i].slider.setBounds (x, 200, 90, 84);
    }
    // Авто-ряд
    for (int i = 4; i < 10; ++i)
    {
        const int x = 28 + (i - 4) * 98;
        knobs[(size_t) i].label.setBounds (x, 316, 90, 14);
        knobs[(size_t) i].slider.setBounds (x + 5, 330, 80, 72);
    }
}
