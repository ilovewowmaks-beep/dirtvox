#include "PluginProcessor.h"
#include "PluginEditor.h"

static juce::String ru (const char* s) { return juce::String::fromUTF8 (s); }

DirtVoxProcessor::DirtVoxProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "DirtVox", createLayout())
{
    pTrim  = apvts.getRawParameterValue ("trim");
    pHpf   = apvts.getRawParameterValue ("hpf");
    pMud   = apvts.getRawParameterValue ("mud");
    pPres  = apvts.getRawParameterValue ("presence");
    pComp  = apvts.getRawParameterValue ("comp");
    pDeess = apvts.getRawParameterValue ("deess");
    pDirt  = apvts.getRawParameterValue ("dirt");
    pTone  = apvts.getRawParameterValue ("tone");
    pMix   = apvts.getRawParameterValue ("mix");
    pOut   = apvts.getRawParameterValue ("output");
    pChar  = apvts.getRawParameterValue ("character");
    status = ru ("Включи трек с вокалом и нажми АНАЛИЗ");
    startTimerHz (20);
}

DirtVoxProcessor::~DirtVoxProcessor() { stopTimer(); }

juce::AudioProcessorValueTreeState::ParameterLayout DirtVoxProcessor::createLayout()
{
    using P = juce::AudioParameterFloat;
    using R = juce::NormalisableRange<float>;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> v;
    auto db = [] (float x, int) { return juce::String (x, 1) + " dB"; };
    auto hz = [] (float x, int) { return x >= 1000 ? juce::String (x / 1000.0f, 1) + " kHz" : juce::String ((int) x) + " Hz"; };
    auto pc = [] (float x, int) { return juce::String (juce::roundToInt (x * 100)) + "%"; };
    auto attr = [] (auto fn) { return juce::AudioParameterFloatAttributes().withStringFromValueFunction (fn); };

    R hpfRange (20.0f, 300.0f); hpfRange.setSkewForCentre (90.0f);
    R toneRange (2000.0f, 18000.0f); toneRange.setSkewForCentre (8000.0f);

    v.push_back (std::make_unique<P> (juce::ParameterID { "trim", 1 },     "Trim",     R (-24, 24, 0.1f), 0.0f, attr (db)));
    v.push_back (std::make_unique<P> (juce::ParameterID { "hpf", 1 },      "Low Cut",  hpfRange, 90.0f, attr (hz)));
    v.push_back (std::make_unique<P> (juce::ParameterID { "mud", 1 },      "Mud Cut",  R (0, 10, 0.1f), 2.0f, attr (db)));
    v.push_back (std::make_unique<P> (juce::ParameterID { "presence", 1 }, "Presence", R (-3, 9, 0.1f), 3.0f, attr (db)));
    v.push_back (std::make_unique<P> (juce::ParameterID { "comp", 1 },     "Comp Thr", R (-40, 0, 0.1f), -24.0f, attr (db)));
    v.push_back (std::make_unique<P> (juce::ParameterID { "deess", 1 },    "De-Ess",   R (-50, 0, 0.1f), -30.0f, attr (db)));
    v.push_back (std::make_unique<P> (juce::ParameterID { "dirt", 1 },     "Dirt",     R (0, 1, 0.001f), 0.5f, attr (pc)));
    v.push_back (std::make_unique<P> (juce::ParameterID { "tone", 1 },     "Tone",     toneRange, 6000.0f, attr (hz)));
    v.push_back (std::make_unique<P> (juce::ParameterID { "mix", 1 },      "Dirt Mix", R (0, 1, 0.001f), 0.8f, attr (pc)));
    v.push_back (std::make_unique<P> (juce::ParameterID { "output", 1 },   "Output",   R (-24, 18, 0.1f), 0.0f, attr (db)));
    v.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "character", 1 }, "Character",
                                                               juce::StringArray { "Warm", "Rage", "Blown" }, 1));
    return { v.begin(), v.end() };
}

bool DirtVoxProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return l.getMainInputChannelSet() == out;
}

void DirtVoxProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    chain.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    analyzer.prepare (sampleRate);
    setLatencySamples (chain.latency());
}

void DirtVoxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, n);

    // Анализ идёт по сырому входу, до обработки.
    int st = state.load();
    if (st == 1) { analyzer.reset(); state.store (2); st = 2; }
    if (st == 2 && buffer.getNumChannels() > 0)
    {
        const float* L = buffer.getReadPointer (0);
        const float* R = buffer.getReadPointer (buffer.getNumChannels() > 1 ? 1 : 0);
        analyzer.push (L, R, n);
        const double sec = analyzer.seconds();
        progress.store ((float) std::min (1.0, sec / listenSeconds));
        if (sec >= listenSeconds) state.store (3);
    }

    dv::Params p;
    p.trim = pTrim->load(); p.hpf = pHpf->load(); p.mud = pMud->load(); p.presence = pPres->load();
    p.compThr = pComp->load(); p.deess = pDeess->load(); p.dirt = pDirt->load(); p.tone = pTone->load();
    p.mix = pMix->load(); p.output = pOut->load(); p.character = (int) pChar->load();
    chain.process (buffer, p);
    deGr.store (chain.getDeessGr());
    compGr.store (chain.getCompGr());
}

void DirtVoxProcessor::startAnalysis()
{
    progress.store (0);
    listenStartedMs = juce::Time::getMillisecondCounterHiRes();
    state.store (1);
    status = ru ("Слушаю… включи воспроизведение, нужно ~6 с вокала");
}

void DirtVoxProcessor::cancelAnalysis()
{
    state.store (0);
    progress.store (0);
    status = ru ("Анализ отменён");
}

void DirtVoxProcessor::pollAnalysis()
{
    const int st = state.load();
    if (st == 1 || st == 2)
    {
        const float pr = progress.load();
        if (pr <= 0.0f && juce::Time::getMillisecondCounterHiRes() - listenStartedMs > 1500)
            status = ru ("Жду голос… включи воспроизведение в FL (пробел)");
        else if (pr > 0.0f)
            status = ru ("Слушаю вокал… ") + juce::String (juce::roundToInt (pr * 100)) + "%";
    }
    else if (st == 3)
    {
        applyAnalysis();
        state.store (0);
    }
}

void DirtVoxProcessor::setParam (const juce::String& id, float value)
{
    if (auto* prm = apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
        prm->endChangeGesture();
    }
}

static float percentile (const float* data, int n, float q)
{
    std::vector<float> v (data, data + n);
    std::sort (v.begin(), v.end());
    return v[(size_t) juce::jlimit (0, n - 1, (int) std::floor (q * (n - 1)))];
}

// Эвристики автонастройки. Все уровни — относительно общей энергии голоса.
void DirtVoxProcessor::applyAnalysis()
{
    const auto& a = analyzer;
    if (a.n <= 0 || a.nWin < 4)
    {
        status = ru ("Слишком мало голоса — включи трек и нажми ещё раз");
        return;
    }
    const auto rel = [&] (double band) { return (float) (10.0 * std::log10 (band / a.tot + 1.0e-20)); };
    const float rms     = (float) (10.0 * std::log10 (a.tot / (double) a.n + 1.0e-20));
    const float lowRel  = rel (a.lo);
    const float mudRel  = rel (a.md);
    const float presRel = rel (a.pr);

    const float trim = juce::jlimit (-24.0f, 24.0f, -18.0f - rms);
    const float hpf  = lowRel > -10.0f ? 140.0f : lowRel > -16.0f ? 115.0f : lowRel > -22.0f ? 95.0f : 80.0f;
    const float mud  = juce::jlimit (1.0f, 8.0f, (mudRel + 7.0f) * 0.8f + 2.5f);
    const float pres = juce::jlimit (0.0f, 8.0f, (-17.0f - presRel) * 0.5f + 3.0f);
    // Порог компрессора по медиане громких окон: ловит тело голоса, но не душит до тишины.
    const float p60  = percentile (a.winDb.data(), a.nWin, 0.6f) + trim;
    const float comp = juce::jlimit (-36.0f, -8.0f, p60 - 2.0f);
    const float sib  = percentile (a.sibDb.data(), a.nWin, 0.8f) + trim;
    const float de   = juce::jlimit (-40.0f, -12.0f, sib + 11.0f);
    // Тон умеренно-тёмный (референс EsDeeKid мид-ориентированный, но не глухой).
    const float sibRel = rel (a.sb);
    const float tone = sibRel > -14.0f ? 8500.0f : sibRel > -20.0f ? 10000.0f : 12000.0f;
    // Выход: гоним активный RMS к ~-11 dBFS (плотная посадка). K учитывает сжатие динамики.
    const float targetRms = -11.0f;
    const float output = juce::jlimit (-6.0f, 18.0f, targetRms - (rms + trim) + 6.0f);

    setParam ("trim", trim);
    setParam ("hpf", hpf);
    setParam ("mud", mud);
    setParam ("presence", pres);
    setParam ("comp", comp);
    setParam ("deess", de);
    setParam ("tone", tone);
    setParam ("output", output);

    auto f1 = [] (float x) { return juce::String (x, 1); };
    status = ru ("Готово: trim ") + (trim >= 0 ? "+" : "") + f1 (trim) + " dB, " + ru ("срез ") + juce::String ((int) hpf)
           + " Hz, " + ru ("каша −") + f1 (mud) + " dB, " + ru ("присутствие +") + f1 (pres) + ru (" dB. Крути DIRT по вкусу");
}

void DirtVoxProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void DirtVoxProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* DirtVoxProcessor::createEditor() { return new DirtVoxEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DirtVoxProcessor(); }
