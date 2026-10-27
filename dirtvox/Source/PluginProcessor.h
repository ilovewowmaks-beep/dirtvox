#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "VocalDSP.h"

class DirtVoxProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    DirtVoxProcessor();
    ~DirtVoxProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Dirt Vox"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- Анализ «одной кнопкой» (вызывать из message thread) ----
    void startAnalysis();
    void cancelAnalysis();
    void pollAnalysis();          // вызывается таймером; в тестах — вручную
    bool isListening() const      { const int s = state.load(); return s == 1 || s == 2; }
    float getProgress() const     { return progress.load(); }
    juce::String getStatus() const { return status; }
    float getDeessGr() const      { return deGr.load(); }
    float getCompGr() const       { return compGr.load(); }

    static constexpr double listenSeconds = 6.0;

    juce::AudioProcessorValueTreeState apvts;

private:
    void timerCallback() override { pollAnalysis(); }
    void applyAnalysis();
    void setParam (const juce::String& id, float value);
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    dv::VocalChain chain;
    dv::Analyzer analyzer;
    std::atomic<int> state { 0 };   // 0 — ждём, 1 — запрошен старт, 2 — слушаем, 3 — готово к расчёту
    std::atomic<float> progress { 0 }, deGr { 0 }, compGr { 0 };
    juce::String status;
    double listenStartedMs = 0;

    std::atomic<float>* pTrim = nullptr; std::atomic<float>* pHpf = nullptr; std::atomic<float>* pMud = nullptr;
    std::atomic<float>* pPres = nullptr; std::atomic<float>* pComp = nullptr; std::atomic<float>* pDeess = nullptr;
    std::atomic<float>* pDirt = nullptr; std::atomic<float>* pTone = nullptr; std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pOut = nullptr; std::atomic<float>* pChar = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DirtVoxProcessor)
};
