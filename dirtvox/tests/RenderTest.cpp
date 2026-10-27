// Консольный тест: синтетический «вокал» → анализ → обработка → проверка уровней и WAV на выходе.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include <cstdio>

static void writeWav (const juce::AudioBuffer<float>& b, double sr, const juce::File& f)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> os (f.createOutputStream());
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, (unsigned) b.getNumChannels(), 24, {}, 0));
    if (w) { os.release(); w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()); }
}

// Квазивокал: пила с вибрато через форманты, слова с паузами, шипящие, гул 50 Гц, общий уровень ~ −26 dBFS.
static juce::AudioBuffer<float> makeVocal (double sr, double seconds, unsigned seed)
{
    const int n = (int) (sr * seconds);
    juce::AudioBuffer<float> b (2, n);
    juce::Random rnd ((juce::int64) seed);
    dv::Biquad f1, f2, f3, hs;
    f1.set (dv::Biquad::BP, sr, 650, 4); f2.set (dv::Biquad::BP, sr, 1100, 5); f3.set (dv::Biquad::BP, sr, 2700, 6);
    hs.set (dv::Biquad::HP, sr, 5000, 0.7);
    double ph = 0, t = 0;
    int i = 0;
    while (i < n)
    {
        const int word = (int) (sr * (0.25 + rnd.nextFloat() * 0.4));
        const int gap = (int) (sr * (0.06 + rnd.nextFloat() * 0.15));
        const bool sib = rnd.nextFloat() < 0.4f;
        const double f0 = 150 + rnd.nextFloat() * 80;
        for (int k = 0; k < word && i < n; ++k, ++i)
        {
            t = i / sr;
            const double env = std::sin (juce::MathConstants<double>::pi * k / word);
            ph += f0 * (1 + 0.01 * std::sin (2 * juce::MathConstants<double>::pi * 5.5 * t)) / sr;
            const float saw = (float) (2 * (ph - std::floor (ph)) - 1);
            float v = (f1.process (saw) * 1.0f + f2.process (saw) * 0.6f + f3.process (saw) * 0.3f) * (float) env;
            if (sib && k < sr * 0.09) v += hs.process (rnd.nextFloat() * 2 - 1) * 0.35f;
            v += 0.01f * (float) std::sin (2 * juce::MathConstants<double>::pi * 50 * t);
            b.setSample (0, i, v * 0.12f);
        }
        for (int k = 0; k < gap && i < n; ++k, ++i) b.setSample (0, i, 0.0005f * (rnd.nextFloat() * 2 - 1));
    }
    b.copyFrom (1, 0, b, 0, 0, n);
    return b;
}

static void stats (const char* name, const juce::AudioBuffer<float>& b, int from = 0)
{
    double s = 0; float pk = 0; int nan = 0; long cnt = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = from; i < b.getNumSamples(); ++i)
        {
            const float v = b.getSample (c, i);
            if (! std::isfinite (v)) { ++nan; continue; }
            s += (double) v * v; pk = std::max (pk, std::abs (v)); ++cnt;
        }
    std::printf ("%-22s RMS %6.1f dBFS  peak %6.1f dBFS  NaN %d\n", name, 10 * std::log10 (s / std::max (1L, cnt) + 1e-20), dv::gainToDb (pk), nan);
}

// Режим калибровки: прогоняет настоящий WAV через плагин (анализ + обработка) и пишет out.wav.
static int processFile (const juce::String& inPath, const juce::String& outPath, float dirt, int character)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (juce::File (inPath)));
    if (! rd) { std::printf ("не прочитать %s\n", inPath.toRawUTF8()); return 1; }
    const double sr = rd->sampleRate; const int block = 512;
    juce::AudioBuffer<float> buf (2, (int) rd->lengthInSamples);
    rd->read (&buf, 0, (int) rd->lengthInSamples, 0, true, true);
    if (rd->numChannels == 1) buf.copyFrom (1, 0, buf, 0, 0, buf.getNumSamples());

    DirtVoxProcessor proc;
    proc.setPlayConfigDetails (2, 2, sr, block);
    proc.prepareToPlay (sr, block);
    auto set = [&] (const char* id, float v) { auto* p = proc.apvts.getParameter (id); p->setValueNotifyingHost (p->convertTo0to1 (v)); };
    set ("character", (float) character);
    proc.startAnalysis();
    juce::MidiBuffer midi;
    // Первый проход: анализ по всему файлу (несколько секунд набирается с запасом).
    for (int pos = 0; pos < buf.getNumSamples(); pos += block)
    {
        juce::AudioBuffer<float> probe (2, std::min (block, buf.getNumSamples() - pos));
        for (int c = 0; c < 2; ++c) probe.copyFrom (c, 0, buf, c, pos, probe.getNumSamples());
        proc.processBlock (probe, midi);
        proc.pollAnalysis();
        if (! proc.isListening()) break;
    }
    proc.pollAnalysis();
    if (dirt >= 0) set ("dirt", dirt);
    std::printf ("auto: %s\n", proc.getStatus().toRawUTF8());
    // Второй проход: обработка исходника настроенной цепочкой.
    for (int pos = 0; pos < buf.getNumSamples(); pos += block)
    {
        juce::AudioBuffer<float> chunk (buf.getArrayOfWritePointers(), 2, pos, std::min (block, buf.getNumSamples() - pos));
        proc.processBlock (chunk, midi);
    }
    writeWav (buf, sr, juce::File (outPath));
    return 0;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc > 2 && juce::String (argv[1]) == "--file")
    {
        const float dirt = argc > 4 ? (float) juce::String (argv[4]).getDoubleValue() : -1.0f;
        const int character = argc > 5 ? juce::String (argv[5]).getIntValue() : 1;
        return processFile (argv[2], argv[3], dirt, character);
    }
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    const double sr = 48000;
    const int block = 512;

    for (int character = 0; character < 3; ++character)
    {
        DirtVoxProcessor proc;
        proc.setPlayConfigDetails (2, 2, sr, block);
        proc.prepareToPlay (sr, block);
        if (auto* p = proc.apvts.getParameter ("character")) p->setValueNotifyingHost (p->convertTo0to1 ((float) character));

        auto input = makeVocal (sr, 14.0, 7);
        juce::AudioBuffer<float> out (input);
        juce::MidiBuffer midi;
        proc.startAnalysis();
        bool applied = false;
        // Разные размеры блоков, включая больше заявленного (проверка нарезки).
        const int sizes[] = { 512, 128, 4096, 333 };
        int pos = 0, si = 0;
        while (pos < out.getNumSamples())
        {
            const int len = std::min (sizes[si++ % 4], out.getNumSamples() - pos);
            juce::AudioBuffer<float> chunk (out.getArrayOfWritePointers(), 2, pos, len);
            proc.processBlock (chunk, midi);
            pos += len;
            if (! applied && ! proc.isListening()) { proc.pollAnalysis(); applied = true; }
            else proc.pollAnalysis();
        }
        const char* names[] = { "Warm", "Rage", "Blown" };
        std::printf ("\n== %s ==  latency %d samples\n%s\n", names[character], proc.getLatencySamples(), proc.getStatus().toRawUTF8());
        for (auto id : { "trim", "hpf", "mud", "presence", "comp", "deess" })
            std::printf ("  %-9s = %7.2f\n", id, proc.apvts.getRawParameterValue (id)->load());
        stats ("input", input);
        stats ("output (after 7s)", out, (int) (sr * 7));
        writeWav (input, sr, outDir.getChildFile ("in.wav"));
        writeWav (out, sr, outDir.getChildFile (juce::String ("out_") + names[character] + ".wav"));

        // Редактор создаётся и уничтожается без падений
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        if (ed)
        {
            ed->setSize (640, 420);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds());
            juce::PNGImageFormat png;
            juce::FileOutputStream fos (outDir.getChildFile ("editor.png"));
            fos.setPosition (0); fos.truncate();
            png.writeImageToStream (img, fos);
        }
    }
    // Проверка перегруза: синус 200 Гц, доля энергии гармоник (THD) при разном DIRT.
    for (float dirt : { 0.0f, 0.4f, 0.8f })
    {
        DirtVoxProcessor proc;
        proc.setPlayConfigDetails (2, 2, sr, block);
        proc.prepareToPlay (sr, block);
        auto set = [&] (const char* id, float v) { auto* p = proc.apvts.getParameter (id); p->setValueNotifyingHost (p->convertTo0to1 (v)); };
        set ("dirt", dirt); set ("mix", 1.0f); set ("trim", 0.0f); set ("tone", 18000.0f);
        const int n = (int) sr * 2;
        juce::AudioBuffer<float> b (2, n);
        for (int i = 0; i < n; ++i) b.setSample (0, i, 0.12f * (float) std::sin (2 * juce::MathConstants<double>::pi * 200 * i / sr));
        b.copyFrom (1, 0, b, 0, 0, n);
        juce::MidiBuffer midi;
        for (int pos = 0; pos < n; pos += block) { juce::AudioBuffer<float> c (b.getArrayOfWritePointers(), 2, pos, std::min (block, n - pos)); proc.processBlock (c, midi); }
        // DFT по последней секунде: энергия на 200 Гц против гармоник 400..4000 Гц
        const int from = n / 2, len = n / 2;
        auto mag = [&] (double f) { double re = 0, im = 0; for (int i = 0; i < len; ++i) { const double w = 2 * juce::MathConstants<double>::pi * f * i / sr; re += b.getSample (0, from + i) * std::cos (w); im -= b.getSample (0, from + i) * std::sin (w); } return re * re + im * im; };
        const double fund = mag (200);
        double harm = 0; for (int h = 2; h <= 20; ++h) harm += mag (200.0 * h);
        std::printf ("DIRT %3.0f%%  THD %6.1f%%\n", dirt * 100, 100 * std::sqrt (harm / fund));
    }
    return 0;
}
